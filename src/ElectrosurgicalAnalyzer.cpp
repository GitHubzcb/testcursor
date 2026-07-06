#include "stdafx.h"
#include "ElectrosurgicalAnalyzer.h"
#include <math.h>

//==============================================================================
// 修改要点见 ElectrosurgicalAnalyzer.h 顶部注释。
//==============================================================================

CElectrosurgicalAnalyzer::CElectrosurgicalAnalyzer()
{
	m_bConnectElectrSurgAnalyzerCOM = FALSE;

	bsetPulsedMode = false;
	bGetAll = false;
	bGetContIPV = false;
	bGetContPCf = false;
	bGetPULIPV = false;

	m_strContinueIPV = "";
	m_strContinePCF = "";
	m_strPULIPV = "";

	m_Model = MODEL_ESU2400; // 默认型号

	m_dwRespSeq = 0;
	m_strLastResponse = "";
	m_strLineBuf = "";
	InitializeCriticalSection(&m_csResp);
}

CElectrosurgicalAnalyzer::~CElectrosurgicalAnalyzer()
{
	if (m_bConnectElectrSurgAnalyzerCOM)
	{
		closeElectrSurgAnalyzerCOM();
	}
	DeleteCriticalSection(&m_csResp);
}

void CElectrosurgicalAnalyzer::setElectrSurgAnalyzerCOM(const CString &strNewCom, DWORD dwNewBandRate)//115200
{
	m_SerialPort.SetCom(strNewCom);
	m_SerialPort.SetBand(dwNewBandRate);
}

//开启
BOOL CElectrosurgicalAnalyzer::openElectrSurgAnalyzerCOM()
{
	if (m_bConnectElectrSurgAnalyzerCOM)
	{
		return TRUE;   // 防止重复打开、重复起线程
	}
	if (m_SerialPort.OpenConnection())
	{
		// 复位应答状态，避免解析上一次连接的残留应答
		EnterCriticalSection(&m_csResp);
		m_strLastResponse = "";
		m_strLineBuf = "";
		m_dwRespSeq = 0;
		LeaveCriticalSection(&m_csResp);

		Start(0);

		if (m_Model == MODEL_ESXTRA)
		{
			//初始化
			SendESXTRACommand("*RST\r");
			Sleep(1000);
			SendESXTRACommand("CONF:RFM:INP:MODE CONT\r");  // 连续模式
			SendESXTRACommand("CONF:RFM:INP:RANG AUTO\r");  // 自动量程
			SendESXTRACommand("CONF:RFM:DISP:SCRE 1\r");    // 显示屏幕1
		}

		m_bConnectElectrSurgAnalyzerCOM = TRUE;
	}
	else
	{
		return FALSE;
	}
	return TRUE;
}

//断连
BOOL CElectrosurgicalAnalyzer::closeElectrSurgAnalyzerCOM()
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return FALSE;
	}

	if (m_Model == MODEL_ESXTRA)
	{
		SendESXTRACommand("SYST:LOC\r");
	}

	m_bConnectElectrSurgAnalyzerCOM = FALSE;
	// 修复顺序：先停接收线程，再关串口（原版先关串口，
	// 接收线程仍会对已关闭句柄调用 ReadComm）
	Stop(0, 800);
	m_SerialPort.CloseConnection();
	return TRUE;
}

DWORD CElectrosurgicalAnalyzer::ThreadMethodBla()
{
	return 0;
}

DWORD CElectrosurgicalAnalyzer::ThreadMethodFrock()
{
	return 0;
}

//------------------------------------------------------------------------------
// 接收线程：按行组装应答。
// 修复原版三处问题：
//   a) 300ms 才读一次且直接把整包当完整应答（分包时只取前半）；
//   b) bGetAll 跨线程无同步；
//   c) 应答与请求不匹配（解析到的是上一次的应答）。
//------------------------------------------------------------------------------
DWORD CElectrosurgicalAnalyzer::ThreadMethod()
{
	if (!m_bConnectElectrSurgAnalyzerCOM) return 0;

	unsigned char pBuf[BUFFERSIZE] = { 0 };
	while (m_bRunThread)
	{
		Sleep(100);   // 原为 300ms，缩短以降低应答等待延迟
		if (!m_SerialPort.IsOpen())
		{
			continue;
		}
		memset(pBuf, 0, BUFFERSIZE);
		int nCount = (int)m_SerialPort.ReadComm(pBuf, BUFFERSIZE);
		if (nCount < 1) continue;

		// 按行组装：SCPI 应答以 \r 或 \n 结尾，分包时在此拼接
		for (int i = 0; i < nCount; i++)
		{
			char ch = (char)pBuf[i];
			if (ch == '\r' || ch == '\n')
			{
				if (!m_strLineBuf.IsEmpty())
				{
					OnResponseLine(m_strLineBuf);
					m_strLineBuf = "";
				}
			}
			else
			{
				m_strLineBuf += ch;
				// 防御：异常长应答直接丢弃，防止无限增长
				if (m_strLineBuf.GetLength() > 4096)
				{
					m_strLineBuf = "";
				}
			}
		}
	}
	return 0;
}

// 接收线程收到完整一行应答：按型号解析后存储并递增序号
void CElectrosurgicalAnalyzer::OnResponseLine(const CString &strLine)
{
	CString strParsed = strLine;
	if (m_Model == MODEL_ESXTRA)
	{
		// ESXTRA 应答格式转换为与 ESU-2400 一致的 "P,I,V" 顺序
		ParseESXTRAResponse(strLine);
		EnterCriticalSection(&m_csResp);
		m_strLastResponse = m_strContinueIPV;
		m_dwRespSeq++;
		LeaveCriticalSection(&m_csResp);
	}
	else
	{
		EnterCriticalSection(&m_csResp);
		m_strLastResponse = strParsed;
		m_strContinueIPV = strParsed;   // 兼容仍读取该成员的旧代码
		m_dwRespSeq++;
		LeaveCriticalSection(&m_csResp);
	}
	bGetAll = false;
}

DWORD CElectrosurgicalAnalyzer::GetRespSeq()
{
	EnterCriticalSection(&m_csResp);
	DWORD seq = m_dwRespSeq;
	LeaveCriticalSection(&m_csResp);
	return seq;
}

// 等待比 seqBefore 新的应答到达
BOOL CElectrosurgicalAnalyzer::WaitResponse(DWORD seqBefore, DWORD timeoutMs, CString *strResp)
{
	DWORD dwStart = GetTickCount();
	while ((GetTickCount() - dwStart) < timeoutMs)
	{
		EnterCriticalSection(&m_csResp);
		if (m_dwRespSeq != seqBefore)
		{
			if (strResp != NULL)
			{
				*strResp = m_strLastResponse;
			}
			LeaveCriticalSection(&m_csResp);
			return TRUE;
		}
		LeaveCriticalSection(&m_csResp);
		Sleep(20);
	}
	return FALSE;
}

void interceptCStringLR(CString src, CString *strRetLeft, CString *strRetRight)
{
	if (src == "")
	{
		return;
	}
	int pos = src.Find(',');
	if (pos < 0)
	{
		return;
	}
	*strRetLeft = src.Left(pos);
	*strRetRight = src.Right(src.GetLength() - pos - 1);
}

//------------------------------------------------------------------------------
// 读取功率/电流/负载电压。
// 修复：原版发查询后 Sleep(100) 就解析旧字符串（接收线程 300ms 才读一次，
// 拿到的是上一次应答）。现在等待"本次"应答到达后才解析；
// 超时（通讯异常）时输出置 0 并返回 FALSE，绝不用旧数据。
//------------------------------------------------------------------------------
BOOL CElectrosurgicalAnalyzer::getContIPVEx(float *fPWatt, float *fImA, float *VLoat, DWORD timeoutMs)
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return FALSE;
	}
	DWORD seqBefore = GetRespSeq();
	getAllValue();

	CString strResp = "";
	if (!WaitResponse(seqBefore, timeoutMs, &strResp))
	{
		// 本次查询无应答：输出置 0，让上层判定为无效而不是沿用旧值
		*fPWatt = 0.0f;
		*fImA = 0.0f;
		*VLoat = 0.0f;
		return FALSE;
	}
	interceptContAllIP(strResp, fPWatt, fImA, VLoat);
	return TRUE;
}

void CElectrosurgicalAnalyzer::getContIPV(float *fPWatt, float *fImA, float *VLoat)
{
	getContIPVEx(fPWatt, fImA, VLoat, ESU_RESPONSE_TIMEOUT_MS);
}

void CElectrosurgicalAnalyzer::interceptContAllIP(CString strAll, float *fPWatt, float *fImA, float *VLoat)
{
	CString strLeft = "";
	CString strRight = "";
	interceptCStringLR(strAll, &strLeft, &strRight);
	CString strImA = "";
	interceptCStringLR(strRight, &strImA, &strRight);
	CString strPWatt = "";
	interceptCStringLR(strRight, &strPWatt, &strRight);
	*fPWatt = (float)atof(strPWatt);
	*fImA = (float)atof(strImA);

	if (fabs(*fImA) <= 1e-6)
	{
		*VLoat = 0.0;
	}
	else
	{
		*VLoat = *fPWatt / *fImA * 1000;
	}
}

void CElectrosurgicalAnalyzer::getContPCf(float *fPWatt, float *fCF)
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	DWORD seqBefore = GetRespSeq();
	getAllValue();

	CString strResp = "";
	if (WaitResponse(seqBefore, ESU_RESPONSE_TIMEOUT_MS, &strResp))
	{
		m_strContinePCF = strResp;
		interceptContAllPCf(strResp, fPWatt, fCF);
	}
	else
	{
		*fPWatt = 0.0f;
		*fCF = 0.0f;
	}
}

void CElectrosurgicalAnalyzer::interceptContAllPCf(CString strAll, float *fPWatt, float *fCF)
{
	CString strLeft = "";
	CString strRight = "";
	interceptCStringLR(strAll, &strLeft, &strRight);
	interceptCStringLR(strRight, &strLeft, &strRight);
	CString strPWatt = "";
	interceptCStringLR(strRight, &strPWatt, &strRight);
	for (int i = 0; i < 4; i++)
	{
		interceptCStringLR(strRight, &strLeft, &strRight);
	}
	*fPWatt = (float)atof(strPWatt);
	*fCF = (float)atof(strRight);
}

void CElectrosurgicalAnalyzer::getPULIPV(float *fPWatt, float *fImA, float *VLoat)
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	DWORD seqBefore = GetRespSeq();
	getAllValue();

	CString strResp = "";
	if (WaitResponse(seqBefore, ESU_RESPONSE_TIMEOUT_MS, &strResp))
	{
		m_strPULIPV = strResp;
		interceptPULAllIP(strResp, fPWatt, fImA, VLoat);
	}
	else
	{
		*fPWatt = 0.0f;
		*fImA = 0.0f;
		*VLoat = 0.0f;
	}
}

void CElectrosurgicalAnalyzer::interceptPULAllIP(CString strAll, float *fPWatt, float *fImA, float *VLoat)
{
	CString strLeft = "";
	CString strRight = "";
	interceptCStringLR(strAll, &strLeft, &strRight);
	for (int i = 0; i < 14; i++)
	{
		interceptCStringLR(strRight, &strLeft, &strRight);
	}
	*fPWatt = (float)atof(strLeft);
	*fImA = (float)atof(strRight);
	// 修复：增加除零保护（原版 fImA=0 时得到 inf）
	if (fabs(*fImA) <= 1e-6)
	{
		*VLoat = 0.0;
	}
	else
	{
		*VLoat = *fPWatt / *fImA * 1000;
	}
}

//屏幕
void CElectrosurgicalAnalyzer::setDisplayMeasureRFEnergy(int sreen)
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	CString strWbuf = "";
	switch (sreen)
	{
	case 0:
		strWbuf = "CONFigure:MODE MAIN\r";
		break;
	case 1:
		strWbuf = "CONFigure:MODE RFMeasure\r";
		break;
	case 2:
		strWbuf = "CONFigure:MODE RFLeakage\r";
		break;
	case 3:
		strWbuf = "CONFigure:MODE CQM\r";
		break;
	case 4:
		strWbuf = "CONFigure:MODE LCURve\r";
		break;
	case 5:
		strWbuf = "CONFigure:MODE ASEQuence\r";
		break;
	case 6:
		strWbuf = "CONFigure:MODE SYStools\r";
		break;

	default:
		break;
	}
	m_SerialPort.WriteComm(strWbuf, strWbuf.GetLength());
}

void CElectrosurgicalAnalyzer::setDisplayScreenNumber(int numberValue)
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	CString strWbuf = "";
	strWbuf.Format("CONFigure:RFMeasure:DISPlay:SCreen %d\r", numberValue);
	m_SerialPort.WriteComm(strWbuf, strWbuf.GetLength());
}

void CElectrosurgicalAnalyzer::setPulsedMode()
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	CString strWbuf = "CONFigure:RFMeasure:INPut:MODE:PULsed\r";
	m_SerialPort.WriteComm(strWbuf, strWbuf.GetLength());
	bsetPulsedMode = true;
}

void CElectrosurgicalAnalyzer::setContinuousMode()
{
	if (!m_bConnectElectrSurgAnalyzerCOM)
	{
		return;
	}
	CString strWbuf = "CONFigure:RFMeasure:INPut:MODE:CONTinuous\r";
	m_SerialPort.WriteComm(strWbuf, strWbuf.GetLength());
	bsetPulsedMode = false;
}

void CElectrosurgicalAnalyzer::setImpedace(int impedanceVal)
{
	if (!m_bConnectElectrSurgAnalyzerCOM) return;
	if (impedanceVal < 0 || impedanceVal > 6400)
	{
		AfxMessageBox(_T("阻抗值超出范围 (0-6400欧姆)"));
		return;
	}

	if (m_Model == MODEL_ESU2400)
	{
		// ESU-2400 原有逻辑
		CString m_strImpedane = "";
		m_strImpedane.Format("CONFigure:RFMeasure:LOAD:INTernal %d\r", impedanceVal);
		m_SerialPort.WriteComm(m_strImpedane, m_strImpedane.GetLength());
		Sleep(200);

		if (bsetPulsedMode)
		{
			int setA = (int)round(sqrt(10.0 / impedanceVal) * 1000);
			CString strWbuf = "CONFigure:RFMeasure:PULsed:MODE 1\r";
			m_SerialPort.WriteComm(strWbuf, strWbuf.GetLength());
			Sleep(200);
			CString strSetA = "";
			strSetA.Format("CONFigure:RFMeasure:PULsed:PULSET1 %d\r", setA);
			m_SerialPort.WriteComm(strSetA, strSetA.GetLength());
		}
	}
	else if (m_Model == MODEL_ESXTRA)
	{
		// ESXTRA 阻抗设置命令
		CString cmdMode = "CONFigure:RFMeasure:LOAD:MODE INTernal\r";
		SendESXTRACommand(cmdMode);

		CString cmdImpedance;
		cmdImpedance.Format("CONFigure:RFMeasure:LOAD:INTernal %d\r", impedanceVal);
		SendESXTRACommand(cmdImpedance);
		if (bsetPulsedMode)
		{
			//脉冲模式目前只设置范围自动
			CString cmdPulsedMode = "CONFigure:RFMeasure:INPut:MODE PULsed\r";
			SendESXTRACommand(cmdPulsedMode);

			CString cmdRange = "CONFigure:RFMeasure:INPut:RANGE AUTo\r";
			SendESXTRACommand(cmdRange);
		}
	}
}

void CElectrosurgicalAnalyzer::getAllValue()
{
	if (!m_bConnectElectrSurgAnalyzerCOM) return;

	if (m_Model == MODEL_ESU2400)
	{
		CString strWbufAll = "READ:ALL?\r";
		m_SerialPort.WriteComm(strWbufAll, strWbufAll.GetLength());
	}
	else if (m_Model == MODEL_ESXTRA)
	{
		CString cmd = "READ:MVrms?;READ:MArms?;READ:WArms?\r";
		m_SerialPort.WriteComm(cmd, cmd.GetLength());
	}
	bGetAll = true;
}

// 新增：ESXTRA命令发送
void CElectrosurgicalAnalyzer::SendESXTRACommand(const CString& command)
{
	if (!m_SerialPort.IsOpen()) return;

	CString cmd = command;
	m_SerialPort.WriteComm(cmd, cmd.GetLength());
	Sleep(100); // ESXTRA 命令间需要等待
}

// 新增：ESXTRA模式设置
void CElectrosurgicalAnalyzer::SetESXTRAMode(int mode)
{
	if (m_Model != MODEL_ESXTRA) return;

	CString cmd;
	cmd.Format("MODE %d\r", mode);
	SendESXTRACommand(cmd);
}

// 新增：ESXTRA频率设置
void CElectrosurgicalAnalyzer::SetESXTRAFrequency(float freq)
{
	if (m_Model != MODEL_ESXTRA) return;

	CString cmd;
	cmd.Format("FREQ %.1f\r", freq);
	SendESXTRACommand(cmd);
}

// 新增：ESXTRA校准
void CElectrosurgicalAnalyzer::CalibrateESXTRA()
{
	if (m_Model != MODEL_ESXTRA) return;

	SendESXTRACommand("CAL:ALL\r");
}

// 新增：ESXTRA响应解析
void CElectrosurgicalAnalyzer::ParseESXTRAResponse(CString strResponse)
{
	if (m_Model == MODEL_ESXTRA)
	{
		// ESXTRA的READ:ALL?返回格式：17个值序列
		// mVrms, mArms, Watts, mVPeak, mVPP, CF, kHz, TON, TOFF, TCYC, DCYC, MVCyc, MACyc, WCyc, 温度, 电压列表
		CStringArray values;
		int pos = 0;
		CString token = strResponse.Tokenize(",", pos);

		int index = 0;
		while (!token.IsEmpty() && index < 17) {
			values.Add(token);
			token = strResponse.Tokenize(",", pos);
			index++;
		}
		// 提取需要的参数（依据协议第6页）
		if (values.GetSize() >= 3) {
			// 电压(mVrms), 电流(mArms), 功率(Watts)
			m_strContinueIPV.Format("%s,%s,%s",
				values[2],  // Watts RMS
				values[1],  // mA RMS
				values[0]); // mV RMS
		}
	}
}
