# OscilloscopeCommunication.cpp 修改指南

配套的 `OscilloscopeCommunication.h` 已给出完整修改版（见 `src/`）。
`.cpp` 文件只需**替换 5 个函数**：构造函数、析构函数、`ConnectInstr`、`InstrWrite`、`InstrRead`，
并**新增 2 个函数**：`EnsureSession`、`DisconnectInstr`。
其余所有测量函数（`setScale`/`getVMAX`/`getRTime`/双通道测量等）全部经由
`InstrWrite`/`InstrRead` 访问仪器，**无需任何改动**，自动获得会话复用与互斥保护。

修改目的（对应分析报告 3.6 节）：
- 原版每次读写都执行 `viOpenDefaultRM → viOpen → IO → viClose ×2`，中间夹 4 次 `Sleep(50)`，
  单次写耗时 200ms+；采集线程与 UI 线程并发高频开关 VISA 会话，是卡死来源之一；
- `ConnectInstr` 中 `new unsigned long` 两处内存泄漏、`viClose(defaultRM)` 缺失；
- 构造函数 `strmodeValue[5]` 漏赋值、`strmodeValue[6]` 被赋两次（"RS232" 被 "IIC" 覆盖）。

---

## 修改 1：替换构造函数与析构函数

原代码（第 5~27 行）：

```cpp
COscilloscopeCommunication::COscilloscopeCommunication()
{
	m_strDSOInstrAddr = "";
	m_strDSOResult = "";
	m_curChannel = 1;
	curChannelScale = 1;
	strmodeValue[0] = "EDGE";
	strmodeValue[1] = "PULSe";
	strmodeValue[2] = "SLOPe";
	strmodeValue[3] = "VIDeo";
	strmodeValue[4] = "PATTern";
	strmodeValue[6] = "RS232";
	strmodeValue[6] = "IIC";
	strmodeValue[7] = "SPI";
	strmodeValue[8] = "CAN";
	strmodeValue[9] = "FLEXray";
	strmodeValue[10] = "USB";
}


COscilloscopeCommunication::~COscilloscopeCommunication()
{
}
```

替换为：

```cpp
COscilloscopeCommunication::COscilloscopeCommunication()
{
	m_strDSOInstrAddr = "";
	m_strDSOResult = "";
	m_curChannel = 1;
	curChannelScale = 1;
	strmodeValue[0] = "EDGE";
	strmodeValue[1] = "PULSe";
	strmodeValue[2] = "SLOPe";
	strmodeValue[3] = "VIDeo";
	strmodeValue[4] = "PATTern";
	strmodeValue[5] = "RS232";   // 修复：原版漏写下标 5，且下标 6 被赋两次
	strmodeValue[6] = "IIC";
	strmodeValue[7] = "SPI";
	strmodeValue[8] = "CAN";
	strmodeValue[9] = "FLEXray";
	strmodeValue[10] = "USB";

	// 新增：持久会话成员初始化
	m_defaultRM = 0;
	m_instr = 0;
	m_bSessionOpen = false;
	InitializeCriticalSection(&m_csVisa);
}


COscilloscopeCommunication::~COscilloscopeCommunication()
{
	DisconnectInstr();
	DeleteCriticalSection(&m_csVisa);
}
```

## 修改 2：新增 EnsureSession / DisconnectInstr（放在析构函数之后）

```cpp
// 会话未建立时按地址建立持久会话（调用方须已持有 m_csVisa）
bool COscilloscopeCommunication::EnsureSession(const CString &strAddr)
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

void COscilloscopeCommunication::DisconnectInstr()
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
```

## 修改 3：替换 ConnectInstr（第 28~76 行）

```cpp
bool COscilloscopeCommunication::ConnectInstr()		//Connect to the instrument
{
	ViStatus status;
	ViSession defaultRM;
	ViString expr = "?*";
	// 修复：原版 ViPFindList/ViPUInt32 用 new unsigned long 分配且从不释放，改为局部变量
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
			::Sleep(TIME_STEP);
			InstrRead(strSrc, &strInstr);
			//If the instrument(resource) belongs to the DS/MSO/DHO series then jump out
			strInstr.MakeUpper();
			if (strInstr.Find("DS") >= 0 || strInstr.Find("MSO") >= 0 || strInstr.Find("DHO") >= 0)
			{
				bFindDSA = true;
				m_strDSOInstrAddr = strSrc;
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
	// 修复：补上 viClose(defaultRM)
	viClose(defaultRM);

	// 找到设备：建立持久会话
	if (bFindDSA)
	{
		EnterCriticalSection(&m_csVisa);
		bFindDSA = EnsureSession(m_strDSOInstrAddr);
		LeaveCriticalSection(&m_csVisa);
	}
	return bFindDSA;
}
```

## 修改 4：替换 InstrWrite（第 77~128 行）

```cpp
bool COscilloscopeCommunication::InstrWrite(CString strAddr, CString strContent)	//write function
{
	ViStatus status;
	ViUInt32 retCount;
	bool bWriteOK = false;

	// 修复：复用持久会话 + 互斥；不再每次开关会话、不再夹 4 次 Sleep(50)
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
```

## 修改 5：替换 InstrRead（第 130 行起的整个函数）

```cpp
bool COscilloscopeCommunication::InstrRead(CString strAddr, CString *pstrResult)	//Read from the instrument
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
```

## 注意事项

- 原 `InstrWrite`/`InstrRead` 内部的 `GetBuffer/strcpy/ReleaseBuffer` 转换代码随函数体一并删除
  （新实现直接用 `(LPCSTR)` 转换，且不再修改入参字符串）。
- 若文件中其他函数在 `InstrWrite` 之后依赖 `Sleep(TIME_STEP)` 保证仪器处理时间，
  该等待仍在各测量函数内部（原有 `Sleep` 未动），行为不变；因为去掉了会话开关的
  隐性延时（约 150ms/次），如个别机型出现命令过快，可在对应测量函数内补一个
  `Sleep(TIME_STEP)`，不要恢复开关会话的写法。
- `DisconnectInstr()` 应在程序退出（对话框 `OnDestroy`）时调用一次；不调用也会由析构兜底。
