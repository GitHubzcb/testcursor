#include "stdafx.h"
#include "PowerStation.h"
#include "visa.h"

//==============================================================================
// 修改要点见 PowerStation.h 顶部注释。
//==============================================================================

CPowerStation::CPowerStation()
{
	m_strPowerStationInstrAddr = "";
	curDPChannel = 1;
	m_defaultRM = 0;
	m_instr = 0;
	m_bSessionOpen = false;
	InitializeCriticalSection(&m_csVisa);
}

CPowerStation::~CPowerStation()
{
	DisconnectInstr();
	DeleteCriticalSection(&m_csVisa);
}

void CPowerStation::setCurrentChannel(int setValue)
{
	curDPChannel = setValue;
}

// 会话未建立时按地址建立持久会话（调用方须已持有 m_csVisa）
bool CPowerStation::EnsureSession(const CString &strAddr)
{
	if (m_bSessionOpen)
	{
		return true;
	}
	if (strAddr == "")
	{
		return false;
	}
	ViStatus status = viOpenDefaultRM(&m_defaultRM);
	if (status < VI_SUCCESS)
	{
		return false;
	}
	CString strAddrCopy = strAddr;
	status = viOpen(m_defaultRM, (ViRsrc)(LPCSTR)strAddrCopy, VI_NULL, VI_NULL, &m_instr);
	if (status < VI_SUCCESS)
	{
		viClose(m_defaultRM);
		m_defaultRM = 0;
		return false;
	}
	m_bSessionOpen = true;
	return true;
}

void CPowerStation::DisconnectInstr()
{
	EnterCriticalSection(&m_csVisa);
	if (m_bSessionOpen)
	{
		viClose(m_instr);
		viClose(m_defaultRM);
		m_instr = 0;
		m_defaultRM = 0;
		m_bSessionOpen = false;
	}
	LeaveCriticalSection(&m_csVisa);
}

bool CPowerStation::ConnectInstr()		//Connect to the instrument 只要搜到第一个设备退出查找循环
{
	ViStatus status;
	ViSession defaultRM;
	ViString expr = "?*";
	// 修复：原版 ViPFindList/ViPUInt32 用 new unsigned long 分配且从不释放
	// （每次连接泄漏两块内存），改为局部变量
	ViFindList findList = 0;
	ViUInt32 retcnt = 0;
	ViChar instrDesc[1000];
	CString strSrc = "";
	CString strInstr = "";
	unsigned long i = 0;
	bool bFindDSA = false;

	// 先释放可能存在的旧会话
	DisconnectInstr();

	status = viOpenDefaultRM(&defaultRM);
	if (status < VI_SUCCESS)
	{
		return bFindDSA;
	}
	memset(instrDesc, 0, 1000);
	//Find resource
	status = viFindRsrc(defaultRM, expr, &findList, &retcnt, instrDesc);
	if (status >= VI_SUCCESS)
	{
		for (i = 0; i < retcnt; i++)
		{
			//Get instrument name
			strSrc.Format("%s", instrDesc);
			InstrWrite(strSrc, "*IDN?");
			::Sleep(200);
			InstrRead(strSrc, &strInstr);

			//If the instrument(resource) belongs to the DP series then jump out from the loop
			strInstr.MakeUpper();
			if (strInstr.Find("DP") >= 0 || strInstr.Find("DP811A") >= 0 || strInstr.Find("DP811") >= 0)
			{
				bFindDSA = true;
				m_strPowerStationInstrAddr = strSrc;
				break;
			}
			// 未命中：断开为探测建立的临时会话，继续找下一个
			DisconnectInstr();

			//Find next resource
			status = viFindNext(findList, instrDesc);
			if (status < VI_SUCCESS)
			{
				break;
			}
		}
		if (findList != 0)
		{
			viClose(findList);
		}
	}
	// 修复：补上 viClose(defaultRM)（枚举用的资源管理器）
	viClose(defaultRM);

	// 找到设备：建立持久会话
	if (bFindDSA)
	{
		EnterCriticalSection(&m_csVisa);
		bFindDSA = EnsureSession(m_strPowerStationInstrAddr);
		LeaveCriticalSection(&m_csVisa);
	}
	return bFindDSA;
}

bool CPowerStation::InstrWrite(CString strAddr, CString strContent)	//write function
{
	ViStatus status;
	ViUInt32 retCount;
	bool bWriteOK = false;

	// 修复：全部 VISA 访问加互斥；复用持久会话，
	// 不再每次 viOpenDefaultRM/viOpen/viClose（原版高频开关会话）
	EnterCriticalSection(&m_csVisa);
	do
	{
		if (!EnsureSession(strAddr))
		{
			break;
		}
		status = viWrite(m_instr, (ViBuf)(LPCSTR)strContent, strContent.GetLength(), &retCount);
		if (status < VI_SUCCESS)
		{
			// 写失败可能是会话失效：释放，下次自动重建
			viClose(m_instr);
			viClose(m_defaultRM);
			m_instr = 0;
			m_defaultRM = 0;
			m_bSessionOpen = false;
			bWriteOK = false;
		}
		else
		{
			bWriteOK = true;
		}
	} while (0);
	LeaveCriticalSection(&m_csVisa);
	return bWriteOK;
}

bool CPowerStation::InstrRead(CString strAddr, CString *pstrResult)	//Read from the instrument
{
	ViStatus status;
	ViUInt32 retCount = 0;
	unsigned char RecBuf[MAX_REC_SIZE];
	bool bReadOK = false;

	memset(RecBuf, 0, MAX_REC_SIZE);

	EnterCriticalSection(&m_csVisa);
	do
	{
		if (!EnsureSession(strAddr))
		{
			break;
		}
		status = viRead(m_instr, RecBuf, MAX_REC_SIZE - 1, &retCount);
		if (status < VI_SUCCESS)
		{
			bReadOK = false;
		}
		else
		{
			bReadOK = true;
		}
	} while (0);
	LeaveCriticalSection(&m_csVisa);

	RecBuf[MAX_REC_SIZE - 1] = '\0';
	(*pstrResult).Format("%s", RecBuf);
	return bReadOK;
}

bool CPowerStation::getSetV(float *fV)
{
	bool bRet = false;
	if (m_strPowerStationInstrAddr == "")
	{
		return false;
	}
	CString m_strGetDV = "";
	m_strGetDV.Format(":APPL? CH%d,VOLT", curDPChannel);
	bRet = InstrWrite(m_strPowerStationInstrAddr, m_strGetDV);
	if (!bRet)
	{
		return false;
	}
	Sleep(SETTIMESTEP);//操作间隔
	CString strGetSetVVal = "";
	bool bGetAllVal = InstrRead(m_strPowerStationInstrAddr, &strGetSetVVal);
	if (!bGetAllVal)
	{
		return false;
	}
	*fV = (float)atof(strGetSetVVal);
	return bRet;
}

bool CPowerStation::setLimitVA(float fVlimit, float fAlimit)
{
	bool bRet = false;
	if (m_strPowerStationInstrAddr == "")
	{
		return false;
	}
	CString m_strSetDPVA = "";
	m_strSetDPVA.Format(":APPL CH%d,%f,%f", curDPChannel, fVlimit, fAlimit);
	bRet = InstrWrite(m_strPowerStationInstrAddr, m_strSetDPVA);
	Sleep(SETTIMESTEP);
	return bRet;
}

bool CPowerStation::openChannel()
{
	if (m_strPowerStationInstrAddr == "")
	{
		return false;
	}
	CString m_strOpen = "";
	m_strOpen.Format(":OUTPut CH%d,ON", curDPChannel);
	bool bRet = InstrWrite(m_strPowerStationInstrAddr, m_strOpen);
	return bRet;
}

bool CPowerStation::closeChannel()
{
	if (m_strPowerStationInstrAddr == "")
	{
		return false;
	}
	CString m_strClose = "";
	m_strClose.Format(":OUTP CH%d,OFF", curDPChannel);
	bool bRet = InstrWrite(m_strPowerStationInstrAddr, m_strClose);
	return bRet;
}

void interceptCString(CString src, CString *strRetLeft, CString *strRetRight)
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

bool CPowerStation::getAllValue(float *fV, float *fA, float *fP)
{
	if (m_strPowerStationInstrAddr == "")
	{
		return false;
	}
	CString m_strGetALL = "";
	m_strGetALL.Format(":MEASure:ALL:DC? CH%d", curDPChannel);
	if (!InstrWrite(m_strPowerStationInstrAddr, m_strGetALL))
	{
		return false;
	}
	//:MEAS:ALL ? CH1 /*查询在CH1输出端子上测得的电压、电流和功率值，例：2.0000,0.0500,0.100*/
	CString strGetAllVal = "";
	bool bGetAllVal = InstrRead(m_strPowerStationInstrAddr, &strGetAllVal);
	if (!bGetAllVal)
	{
		return false;
	}
	int pos = strGetAllVal.Find(',');
	if (pos < 0)
	{
		return false;
	}
	CString strV = "";
	CString strAP = "";
	interceptCString(strGetAllVal, &strV, &strAP);
	*fV = (float)atof(strV);
	CString strA = "";
	CString strP = "";
	interceptCString(strAP, &strA, &strP);
	*fA = (float)atof(strA);
	*fP = (float)atof(strP);
	return true;
}
