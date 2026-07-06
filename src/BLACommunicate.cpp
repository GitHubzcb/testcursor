#include "stdafx.h"
#include "BLACommunicate.h"
#include <math.h>

//==============================================================================
// 修改要点见 BLACommunicate.h 顶部注释。
// 帧格式：AA 55 | flag(1) | lenL lenH | payload(len) | crc32(4) | CC 33
// 最小帧长 = 11 字节。
//==============================================================================

#define BLA_FRAME_MIN_LEN     11
#define BLA_FRAME_MAX_PAYLOAD 2048

CBLACommunicate::CBLACommunicate()
{
	m_bConnectBlaCOM = FALSE;

	// 修复：原版 data[250] = {0} 是对下标 250 的越界写，不是初始化
	memset(data, 0, sizeof(data));
	memset(&ct_rf_param, 0, sizeof(ct_rf_param));
	memset(&stim_param, 0, sizeof(stim_param));
	memset(&DeviceInfo, 0, sizeof(DeviceInfo));
	memset(m_sendBuf, 0, sizeof(m_sendBuf));

	curPage = PC_TEST_ELEC_SELECT_PAGE;

	memset(&pc_test_status, 0xFF, sizeof(t_pc_test_status));
	bGetBlaState = FALSE;

	hGetBlaStateThread = NULL;
	m_dwLastStatusTick = 0;
	m_bStatusEverReceived = FALSE;

	InitializeCriticalSection(&m_csSend);
	InitializeCriticalSection(&m_csStatus);

	// 修复：m_ExitBlaThreadEvent 原来从未创建就被 Set/Reset/Wait（野句柄）
	m_ExitBlaThreadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hDeviceInfoEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CBLACommunicate::~CBLACommunicate()
{
	// 先按正常流程断开（内部会等待轮询线程退出并停接收线程）
	if (m_bConnectBlaCOM)
	{
		closeBlaCOM();
	}
	else if (hGetBlaStateThread != NULL)
	{
		SetEvent(m_ExitBlaThreadEvent);
		WaitForSingleObject(hGetBlaStateThread, 3000);
		CloseHandle(hGetBlaStateThread);
		hGetBlaStateThread = NULL;
	}

	if (m_ExitBlaThreadEvent != NULL)
	{
		CloseHandle(m_ExitBlaThreadEvent);
		m_ExitBlaThreadEvent = NULL;
	}
	if (m_hDeviceInfoEvent != NULL)
	{
		CloseHandle(m_hDeviceInfoEvent);
		m_hDeviceInfoEvent = NULL;
	}

	DeleteCriticalSection(&m_csSend);
	DeleteCriticalSection(&m_csStatus);
}

void CBLACommunicate::setBlaCOM(const CString &strNewCom, DWORD dwNewBandRate)//115200
{
	m_BlaSerialPort.SetCom(strNewCom);
	m_BlaSerialPort.SetBand(dwNewBandRate);
}

BOOL CBLACommunicate::openBlaCOM()
{
	if (m_bConnectBlaCOM)
	{
		return TRUE;   // 已打开，防止重复开线程
	}
	if (!m_BlaSerialPort.OpenConnection())
	{
		AfxMessageBox(_T("BLA串口连接失败"));
		return FALSE;
	}

	// 复位状态数据，避免用上一块板子的残留数据判定
	EnterCriticalSection(&m_csStatus);
	memset(&pc_test_status, 0xFF, sizeof(t_pc_test_status));
	m_dwLastStatusTick = 0;
	m_bStatusEverReceived = FALSE;
	LeaveCriticalSection(&m_csStatus);

	m_bConnectBlaCOM = TRUE;
	Start(1);   // 启动接收线程 ThreadMethodBla（CBaseThread）

	// 修复：确保上一个轮询线程已退出，防止多个轮询线程并存
	if (hGetBlaStateThread != NULL)
	{
		SetEvent(m_ExitBlaThreadEvent);
		WaitForSingleObject(hGetBlaStateThread, 3000);
		CloseHandle(hGetBlaStateThread);
		hGetBlaStateThread = NULL;
	}
	ResetEvent(m_ExitBlaThreadEvent);
	DWORD IDSendThread = 0;
	hGetBlaStateThread = CreateThread(NULL, 0, CommGetBlaStateThread, (LPVOID)this, 0, &IDSendThread);
	if (hGetBlaStateThread == NULL)
	{
		closeBlaCOM();
		return FALSE;
	}
	return TRUE;
}

BOOL CBLACommunicate::closeBlaCOM()
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	m_bConnectBlaCOM = FALSE;
	bGetBlaState = FALSE;

	// 修复顺序：1) 通知并真正等待轮询线程退出（原版等待 0ms 即关串口，
	// 线程仍在向已关闭句柄写数据）
	SetEvent(m_ExitBlaThreadEvent);
	if (hGetBlaStateThread != NULL)
	{
		WaitForSingleObject(hGetBlaStateThread, 3000);
		CloseHandle(hGetBlaStateThread);
		hGetBlaStateThread = NULL;
	}

	// 2) 停接收线程
	Stop(1, 800);

	// 3) 最后关串口
	m_BlaSerialPort.CloseConnection();
	return TRUE;
}

DWORD CBLACommunicate::ThreadMethod()
{
	return 0;
}

DWORD CBLACommunicate::ThreadMethodFrock()
{
	return 0;
}

//------------------------------------------------------------------------------
// 环形缓冲逐帧提取。
// 返回：>0 提取到一帧（返回帧总长，帧已拷贝到 out）；0 暂无完整帧。
// 特性：容忍任意分包/粘包、帧前噪声字节、假帧头（帧尾不符时跳过重扫）。
// 修复原版三处问题：
//   a) 校验 sticky_buf、解析 pBuf 的错位 bug；
//   b) 只支持两段拼接，三段及以上或帧头不在 buf[0] 时整帧丢弃；
//   c) data_lenth 无上限校验导致越界读。
//------------------------------------------------------------------------------
int CBLACommunicate::TryExtractFrame(std::vector<unsigned char> &rx, unsigned char *out, int outCap)
{
	while (true)
	{
		// 1. 丢弃帧头 AA 55 之前的噪声字节
		size_t i = 0;
		while (i + 1 < rx.size() && !(rx[i] == 0xAA && rx[i + 1] == 0x55))
		{
			++i;
		}
		if (i > 0)
		{
			rx.erase(rx.begin(), rx.begin() + i);
		}
		if (rx.size() < BLA_FRAME_MIN_LEN)
		{
			return 0;                       // 不足最小帧长，等待更多数据
		}

		// 2. 解析负载长度并做上限校验
		unsigned int len = rx[3] | ((unsigned int)rx[4] << 8);
		if (len > BLA_FRAME_MAX_PAYLOAD)
		{
			rx.erase(rx.begin(), rx.begin() + 2);   // 假帧头，跳过重扫
			continue;
		}
		size_t total = (size_t)len + BLA_FRAME_MIN_LEN;
		if (rx.size() < total)
		{
			return 0;                       // 帧未收全，等待更多数据
		}

		// 3. 校验帧尾（CRC 校验见 pc_m2_protocol 的 PC_M2_ENABLE_CRC32 开关）
		if (rx[total - 2] == 0xCC && rx[total - 1] == 0x33)
		{
#if PC_M2_ENABLE_CRC32
			unsigned long crcCalc = crc32_get(rx.data(), (unsigned int)(len + 5));
			unsigned long crcRecv = (unsigned long)rx[len + 5]
				| ((unsigned long)rx[len + 6] << 8)
				| ((unsigned long)rx[len + 7] << 16)
				| ((unsigned long)rx[len + 8] << 24);
			if (crcCalc != crcRecv)
			{
				rx.erase(rx.begin(), rx.begin() + 2);
				continue;
			}
#endif
			int n = (int)min((int)total, outCap);
			memcpy(out, rx.data(), n);
			rx.erase(rx.begin(), rx.begin() + total);
			return n;
		}

		// 帧尾不符：这是个假帧头，跳过 AA 55 继续扫描
		rx.erase(rx.begin(), rx.begin() + 2);
	}
}

//------------------------------------------------------------------------------
// 接收线程：读串口 → 环形缓冲 → 逐帧提取 → 锁内解析 + 记时间戳
//------------------------------------------------------------------------------
DWORD CBLACommunicate::ThreadMethodBla()
{
	if (!m_bConnectBlaCOM)
	{
		return 0;
	}

	unsigned char pBuf[BUFFERSIZE] = { 0 };
	unsigned char frame[BLA_FRAME_MAX_PAYLOAD + BLA_FRAME_MIN_LEN] = { 0 };
	std::vector<unsigned char> rxBuffer;
	rxBuffer.reserve(4 * BUFFERSIZE);

	while (m_bRunThreadBla)
	{
		Sleep(30);
		if (!m_BlaSerialPort.IsOpen())
		{
			continue;
		}
		int nCount = (int)m_BlaSerialPort.ReadComm(pBuf, BUFFERSIZE);
		if (nCount <= 0)
		{
			continue;
		}
		rxBuffer.insert(rxBuffer.end(), pBuf, pBuf + nCount);
		// 防御：异常情况下缓冲无限增长
		if (rxBuffer.size() > 16 * BUFFERSIZE)
		{
			rxBuffer.clear();
		}

		int frameLen = 0;
		while ((frameLen = TryExtractFrame(rxBuffer, frame, sizeof(frame))) > 0)
		{
			unsigned char flag = frame[2];
			unsigned int  payloadLen = frame[3] | ((unsigned int)frame[4] << 8);
			unsigned char *payload = frame + 5;

			if (flag == pc_com_test_get_status && payloadLen >= sizeof(t_pc_test_status))
			{
				// 修复撕裂读：锁内整体更新 + 时间戳
				EnterCriticalSection(&m_csStatus);
				bla_pc_test_get_status_unpack(payload, &pc_test_status);
				m_dwLastStatusTick = GetTickCount();
				m_bStatusEverReceived = TRUE;
				LeaveCriticalSection(&m_csStatus);
			}
			else if (flag == pc_com_device_info && payloadLen >= 150)
			{
				EnterCriticalSection(&m_csStatus);
				pc_com_device_info_unpack(payload, &DeviceInfo);
				LeaveCriticalSection(&m_csStatus);
				SetEvent(m_hDeviceInfoEvent);
			}
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
// 状态轮询线程：周期发送状态查询帧。
// 原版靠野句柄 WaitForSingleObject 失败"碰巧"实现轮询；现在事件真实存在，
// 用 400ms 等待做节拍，收到退出事件立即返回。
//------------------------------------------------------------------------------
DWORD WINAPI CBLACommunicate::CommGetBlaStateThread(LPVOID lparamter)
{
	CBLACommunicate* pCom = (CBLACommunicate*)lparamter;
	while (TRUE)
	{
		if (WaitForSingleObject(pCom->m_ExitBlaThreadEvent, 400) == WAIT_OBJECT_0)
		{
			break;
		}
		if (pCom->bGetBlaState && pCom->m_bConnectBlaCOM)
		{
			pCom->GetBlaStatue();
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
// 统一发送入口：组包 + 互斥 + 一次性写出。
// 返回 TRUE 表示整帧写出成功（写出字节数 == 帧长）。
// 替代原来的 Pc_Com_Pack(全局 dat) + WriteComm 组合，消除并发覆盖。
//------------------------------------------------------------------------------
BOOL CBLACommunicate::SendFrame(PC_PROTOCOL_COMM_FLAG flag, const unsigned char *payload, unsigned int payloadLen)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	BOOL bRet = FALSE;
	EnterCriticalSection(&m_csSend);
	unsigned int frameLen = Pc_Com_Pack_Buf(flag, payload, payloadLen, m_sendBuf, sizeof(m_sendBuf));
	if (frameLen > 0)
	{
		DWORD written = m_BlaSerialPort.WriteComm((LPCVOID)m_sendBuf, frameLen);
		bRet = (written == frameLen) ? TRUE : FALSE;
	}
	LeaveCriticalSection(&m_csSend);
	return bRet;
}

//------------------------------------------------------------------------------
// 状态快照与数据新鲜度
//------------------------------------------------------------------------------
BOOL CBLACommunicate::GetStatusSnapshot(t_pc_test_status *dst, DWORD maxAgeMs)
{
	if (dst == NULL)
	{
		return FALSE;
	}
	BOOL bFresh = FALSE;
	EnterCriticalSection(&m_csStatus);
	memcpy(dst, &pc_test_status, sizeof(t_pc_test_status));
	if (m_bStatusEverReceived && (GetTickCount() - m_dwLastStatusTick) <= maxAgeMs)
	{
		bFresh = TRUE;
	}
	LeaveCriticalSection(&m_csStatus);
	return bFresh;
}

BOOL CBLACommunicate::IsStatusFresh(DWORD maxAgeMs)
{
	BOOL bFresh = FALSE;
	EnterCriticalSection(&m_csStatus);
	if (m_bStatusEverReceived && (GetTickCount() - m_dwLastStatusTick) <= maxAgeMs)
	{
		bFresh = TRUE;
	}
	LeaveCriticalSection(&m_csStatus);
	return bFresh;
}

u16 CBLACommunicate::GetMcuPageID()
{
	u16 page = 0xFFFF;
	EnterCriticalSection(&m_csStatus);
	if (m_bStatusEverReceived)
	{
		page = pc_test_status.pageID;
	}
	LeaveCriticalSection(&m_csStatus);
	return page;
}

// 页面门禁：curPage（上位机侧记账）或 MCU 状态帧回传的 pageID 命中其一即放行。
// 修复原版 curPage 因返回值语义反转而失步后，所有 Get* 永久拒收数据的问题。
BOOL CBLACommunicate::IsOnPage(unsigned char page)
{
	if (curPage == page)
	{
		return TRUE;
	}
	u16 mcuPage = GetMcuPageID();
	return (mcuPage != 0xFFFF && mcuPage == (u16)page);
}

//------------------------------------------------------------------------------
// 命令发送类接口（返回值统一：TRUE=发送成功）
//------------------------------------------------------------------------------
BOOL CBLACommunicate::ExitBlaConnect()
{
	return SendFrame(pc_com_exit_connect, NULL, 0);
}

BOOL CBLACommunicate::PCSetBlaPageDisPlay(unsigned char page)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	unsigned char payload[1];
	bla_pc_test_page_display_pack(payload, page);
	BOOL bRet = SendFrame(pc_com_test_set_page, payload, 1);
	// 修复：原版 length==0（失败）才更新 curPage，语义反转；
	// 现在仅在整帧发送成功后更新
	if (bRet)
	{
		curPage = page;
	}
	return bRet;
}

BOOL CBLACommunicate::PCControlBlaKey(u16 key_value)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	unsigned char payload[2];
	bla_pc_test_set_key_value_pack(payload, key_value);
	return SendFrame(pc_com_test_set_key_value, payload, 2);
}

BOOL CBLACommunicate::ElectrodeSelect(eletrodeMode eletrodeMode)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	tELModeStat ELModeStat;
	memset(&ELModeStat, 0xff, sizeof(ELModeStat));
	switch (eletrodeMode)
	{
	case electrode1_monpolar:
		ELModeStat.mode = MonPole;
		ELModeStat.set_port[0] = LinkActive;
		ELModeStat.set_port[1] = Unlink;
		break;
	case electrode2_monpolar:
		ELModeStat.mode = MonPole;
		ELModeStat.set_port[0] = Unlink;
		ELModeStat.set_port[1] = LinkActive;
		break;
	case electrode1_electrode2_monpolar:
		ELModeStat.mode = MonPole;
		ELModeStat.set_port[0] = LinkActive;
		ELModeStat.set_port[1] = LinkActive;
		break;
	case electrode1_electrode2_bipolar:
		ELModeStat.mode = BipTwo;
		ELModeStat.set_port[0] = LinkActive;
		ELModeStat.set_port[1] = LinkActive;
		break;
	case electrode1_bipolar:
		ELModeStat.mode = BipTwo;
		ELModeStat.set_port[0] = LinkActive;
		ELModeStat.set_port[1] = Unlink;
		break;
	case electrode2_bipolar:
		ELModeStat.mode = BipTwo;
		ELModeStat.set_port[0] = Unlink;
		ELModeStat.set_port[1] = LinkActive;
		break;
	default:
		break;
	}
	unsigned char payload[sizeof(tELModeStat)];
	bla_pc_test_elec_select_pack(payload, &ELModeStat);
	return SendFrame(pc_com_test_elec_select, payload, sizeof(ELModeStat));
}

BOOL CBLACommunicate::BlaStartandStop(unsigned char flag)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	return SendFrame(pc_com_test_start_stop, &flag, 1);
}

BOOL CBLACommunicate::GetBlaStatue()
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	return SendFrame(pc_com_test_get_status, NULL, 0);
}

BOOL CBLACommunicate::SetEleStimulationTestVVValue(int setVVValue)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_STIM_PAGE))
	{
		return FALSE;
	}
	tStimParam stim_param_local;
	memset(&stim_param_local, 0xFF, sizeof(tStimParam));
	stim_param_local.stim_workmode = SenseMode;
	stim_param_local.stim_type = comVoltageStim;
	stim_param_local.ramp_out = SecOff;
	stim_param_local.clr_range = NoClear;
	stim_param_local.VoltageGears = HighLeveV;
	stim_param_local.SimOutPolar = comBiphasic;
	stim_param_local.sense_volt = (float)setVVValue;
	stim_param_local.sens_freq = 200;
	stim_param_local.pulse_width = 2;

	unsigned char payload[sizeof(tStimParam)];
	bla_pc_test_stim_param_pack(payload, &stim_param_local);
	// 修复：原版忽略发送结果恒 return TRUE
	return SendFrame(pc_com_test_set_stim_param, payload, sizeof(stim_param_local));
}

BOOL CBLACommunicate::SetEleStimulationTestImAValue(int setImAValue)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_STIM_PAGE))
	{
		return FALSE;
	}
	tStimParam stim_param_local;
	memset(&stim_param_local, 0xFF, sizeof(tStimParam));
	stim_param_local.stim_workmode = SenseMode;
	stim_param_local.stim_type = comCurrentStim;
	stim_param_local.ramp_out = SecOff;
	stim_param_local.CurrentGears = HighLeveC;
	stim_param_local.SimOutPolar = comBiphasic;
	stim_param_local.sense_curr = (float)setImAValue;
	stim_param_local.sens_freq = 200;
	stim_param_local.pulse_width = 2;

	unsigned char payload[sizeof(tStimParam)];
	bla_pc_test_stim_param_pack(payload, &stim_param_local);
	return SendFrame(pc_com_test_set_stim_param, payload, sizeof(stim_param_local));
}

BOOL CBLACommunicate::SetRFTestPageParameter(tRfTestParam setParam)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_TEST_PAGE))
	{
		return FALSE;
	}
	unsigned char payload[sizeof(tRfTestParam)];
	bla_pc_test_rf_test_param_pack(payload, &setParam);
	// 修复：原版误用 sizeof(stim_param)（别的结构体的大小）
	return SendFrame(pc_com_test_set_rf_test_param, payload, sizeof(setParam));
}

BOOL CBLACommunicate::SetRFPageParameter(tCtRfParam setParam)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_RF_PAGE))
	{
		return FALSE;
	}
	unsigned char payload[sizeof(tCtRfParam)];
	bla_pc_test_rf_param_pack(payload, &setParam);
	return SendFrame(pc_com_test_set_rf_param, payload, sizeof(setParam));
}

BOOL CBLACommunicate::SetStimParameter(tStimParam setParam)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_STIM_PAGE))
	{
		return FALSE;
	}
	unsigned char payload[sizeof(tStimParam)];
	bla_pc_test_stim_param_pack(payload, &setParam);
	return SendFrame(pc_com_test_set_stim_param, payload, sizeof(setParam));
}

BOOL CBLACommunicate::SetSEEGParams(tELModeStat param)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	unsigned char payload[sizeof(tELModeStat)];
	bla_pc_test_elec_select_pack(payload, &param);
	return SendFrame(pc_com_test_elec_select, payload, sizeof(param));
}

BOOL CBLACommunicate::SetPurfParameter(tPuRfParam setParam)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	if (!IsOnPage(PC_TEST_PURF_PAGE))
	{
		return FALSE;
	}
	unsigned char payload[sizeof(tPuRfParam)];
	bla_pc_test_pu_rf_param_pack(payload, &setParam);
	return SendFrame(pc_com_test_set_pu_rf_param, payload, sizeof(setParam));
}

BOOL CBLACommunicate::setM2Version(t_sys_info sys_info)
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	unsigned char payload[sizeof(t_sys_info)];
	bla_pc_test_set_sys_info_pack(payload, &sys_info);
	return SendFrame(pc_com_test_set_sys_info, payload, sizeof(t_sys_info));
}

BOOL CBLACommunicate::shutPCProgram()
{
	if (!m_bConnectBlaCOM)
	{
		return FALSE;
	}
	return SendFrame(pc_com_exit_connect, NULL, 0);
}

//------------------------------------------------------------------------------
// 数据读取类接口。
// 统一模式：页面门禁 → 取快照（含新鲜度检查）→ 判 NaN → 输出。
// 快照过期（>1s 未收到状态帧）一律返回 FALSE，
// 上层由此可以显示"通讯中断"而不是拿旧值误判不合格。
//------------------------------------------------------------------------------
BOOL CBLACommunicate::GetEleRFElectrode1Temp(float * fElectrode1Temp)
{
	if (!IsOnPage(PC_TEST_RF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.temp[0]))
	{
		return FALSE;
	}
	*fElectrode1Temp = snap.com_m0_back.temp[0];
	return TRUE;
}

BOOL CBLACommunicate::GetPurfTempEle1(float* temp)
{
	if (!IsOnPage(PC_TEST_PURF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.temp[0]))
	{
		return FALSE;
	}
	*temp = snap.com_m0_back.temp[0];
	return TRUE;
}

BOOL CBLACommunicate::GetEleRFElectrode2Temp(float* fElectrode2Temp)
{
	if (!IsOnPage(PC_TEST_RF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.temp[1]))
	{
		return FALSE;
	}
	*fElectrode2Temp = snap.com_m0_back.temp[1];
	return TRUE;
}

BOOL CBLACommunicate::GetRFOutput(float* fVoltage, float* fCurrent, float* fPower)
{
	if (!IsOnPage(PC_TEST_PURF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	// 修复：原版电压电流都有效时 bRet 也没被置 TRUE，成功路径返回失败
	if (isnan(snap.com_m0_back.rf_voltage[0]) || isnan(snap.com_m0_back.rf_current[0]))
	{
		return FALSE;
	}
	*fVoltage = snap.com_m0_back.rf_voltage[0];
	*fCurrent = snap.com_m0_back.rf_current[0];
	*fPower = *fVoltage * *fCurrent;
	return TRUE;
}

BOOL CBLACommunicate::GetPurfVoltage(float* volt)
{
	if (!IsOnPage(PC_TEST_PURF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.rf_voltage[0]))
	{
		return FALSE;
	}
	*volt = snap.com_m0_back.rf_voltage[0];
	return TRUE;
}

BOOL CBLACommunicate::GetPurfWidth(float* width)
{
	if (!IsOnPage(PC_TEST_PURF_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.duration[0]))
	{
		return FALSE;
	}
	*width = snap.com_m0_back.duration[0];
	return TRUE;
}

BOOL CBLACommunicate::GetEleStimulationMesFeedbackElectrode1IV(float * fElectrode1FeedIV)
{
	if (!IsOnPage(PC_TEST_STIMULATOR_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.stim_fb[0]))
	{
		return FALSE;
	}
	*fElectrode1FeedIV = snap.com_m0_back.stim_fb[0];
	return TRUE;
}

BOOL CBLACommunicate::GetEleStimulationEleImpedanceValue(float *fEleImpedanceValue)
{
	if (!IsOnPage(PC_TEST_STIM_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	if (isnan(snap.com_m0_back.stim_mes[0]))
	{
		return FALSE;
	}
	// 新增：除零保护（原版电流为 0 时得到 inf）
	if (isnan(snap.com_m0_back.rf_voltage[0]) || isnan(snap.com_m0_back.rf_current[0]) ||
		fabsf(snap.com_m0_back.rf_current[0]) < 1e-6f)
	{
		return FALSE;
	}
	*fEleImpedanceValue = snap.com_m0_back.rf_voltage[0] / (snap.com_m0_back.rf_current[0] / 1000);
	return TRUE;
}

BOOL CBLACommunicate::GetEleStimulationelElectrodeTempValue(float fEletrodeTemp[2])
{
	if (!IsOnPage(PC_TEST_STIMULATOR_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	BOOL bRet = FALSE;
	if (!isnan(snap.com_m0_back.temp[0]))
	{
		fEletrodeTemp[0] = snap.com_m0_back.temp[0];
		bRet = TRUE;
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.temp[1]))
	{
		fEletrodeTemp[1] = snap.com_m0_back.temp[1];
		bRet = TRUE;
	}
	else
	{
		bRet = FALSE;
	}
	return bRet;
}

BOOL CBLACommunicate::GetRFTestVWImAPW(float *fVmeV, float *fImemA, float *fPW)
{
	if (!IsOnPage(PC_TEST_TEST_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	BOOL bRet = TRUE;
	if (!isnan(snap.com_m0_back.rf_voltage[0]))
	{
		*fVmeV = snap.com_m0_back.rf_voltage[0];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.rf_current[0]))
	{
		*fImemA = snap.com_m0_back.rf_current[0];
	}
	else
	{
		bRet = FALSE;
	}
	*fPW = *fVmeV * *fImemA / 1000;
	return bRet;
}

BOOL CBLACommunicate::GetMonitorVVImAPW(float *fVmoV, float *fImomA, float *fPW)
{
	if (!IsOnPage(PC_TEST_TEST_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	BOOL bRet = TRUE;
	if (!isnan(snap.vidata.rfvi_v))
	{
		*fVmoV = snap.vidata.rfvi_v;
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.vidata.rfvi_i))
	{
		*fImomA = snap.vidata.rfvi_i;
	}
	else
	{
		bRet = FALSE;
	}
	*fPW = *fVmoV * *fImomA;
	return bRet;
}

BOOL CBLACommunicate::GetVinVIINATTTITH(float *fVinV, float *fIinA, float *fTT, float *fTI, float *fTH)
{
	if (!IsOnPage(PC_TEST_TEST_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	BOOL bRet = TRUE;
	*fVinV = snap.com_m0_back.board_voltage / 10.0f;
	*fIinA = (float)snap.com_m0_back.board_current;
	if (!isnan(snap.com_m0_back.temp_18b20[2]))
	{
		*fTT = snap.com_m0_back.temp_18b20[2];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.temp_18b20[1]))
	{
		*fTI = snap.com_m0_back.temp_18b20[1];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.temp_18b20[0]))
	{
		*fTH = snap.com_m0_back.temp_18b20[0];
	}
	else
	{
		bRet = FALSE;
	}
	return bRet;
}

BOOL CBLACommunicate::GetCaliRFIVPowerDetectElectrodeTemp(CaliRFIVPowerDetectElectrodeTemp *dst)
{
	if (!IsOnPage(PC_TEST_TEST_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	BOOL bRet = TRUE;
	if (!isnan(snap.com_m0_back.rf_voltage[0]))
	{
		dst->fVmeV = snap.com_m0_back.rf_voltage[0];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.rf_current[0]))
	{
		dst->fImemA = snap.com_m0_back.rf_current[0];
	}
	else
	{
		bRet = FALSE;
	}
	dst->fPW = dst->fVmeV * dst->fImemA / 1000;

	dst->fVinV = snap.com_m0_back.board_voltage / 10.0f;
	dst->fIinA = snap.com_m0_back.board_current / 1000.0f;

	if (!isnan(snap.com_m0_back.temp_18b20[2]))
	{
		dst->fTT = snap.com_m0_back.temp_18b20[2];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.temp_18b20[1]))
	{
		dst->fTI = snap.com_m0_back.temp_18b20[1];
	}
	else
	{
		bRet = FALSE;
	}
	if (!isnan(snap.com_m0_back.temp_18b20[0]))
	{
		dst->fTH = snap.com_m0_back.temp_18b20[0];
	}
	else
	{
		bRet = FALSE;
	}
	return bRet;
}

bool CBLACommunicate::GetCQM(unsigned short* CQMval)
{
	if (!IsOnPage(PC_TEST_RELAY_SWITCH_PAGE))
	{
		return false;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return false;
	}
	*CQMval = snap.com_m0_back.cqm;
	return true;
}

BOOL CBLACommunicate::GetRFAccuacy(RFAccuacy *getBlaVal)
{
	return FALSE;
}

BOOL CBLACommunicate::GetInformation(CString* picture)
{
	if (!IsOnPage(PC_TEST_DEVICE_INFO_PAGE))
	{
		return FALSE;
	}
	t_pc_test_status snap;
	if (!GetStatusSnapshot(&snap))
	{
		return FALSE;
	}
	// 防御：确保字符串以 '\0' 结束再使用
	snap.picture_verson[sizeof(snap.picture_verson) - 1] = '\0';
	if (snap.picture_verson[0] != '\0' && strlen(snap.picture_verson) > 0)
	{
		*picture = snap.picture_verson;
		return TRUE;
	}
	return FALSE;
}
