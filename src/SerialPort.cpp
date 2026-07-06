#include "StdAfx.h"
#include "SerialPort.h"
#include <assert.h>

//==============================================================================
// 修改要点见 SerialPort.h 顶部注释。
// 对外接口（函数名/参数/返回值含义）与原版保持一致：
//   ReadComm / WriteComm 返回实际读/写的字节数，0 表示失败或无数据。
//==============================================================================

CSerialPort::CSerialPort(void):m_bConnected(FALSE), m_strFileName("COM1"), m_dwBandRate(9600),
	m_btDataBits(8),
	m_btParity(NOPARITY),      // 修复：原版误写为 ONESTOPBIT(=0)，语义上应为"无校验"
	m_btStopBits(ONESTOPBIT),  // 修复：原版误写为 NOPARITY(=0)，此处 ONESTOPBIT 与原枚举值不同
	m_hCom(INVALID_HANDLE_VALUE),
	m_hReadEvent(NULL),
	m_hWriteEvent(NULL)
{
	InitializeCriticalSection(&m_csRead);
	InitializeCriticalSection(&m_csWrite);
}

CSerialPort::~CSerialPort(void)
{
	CloseConnection();
	DeleteCriticalSection(&m_csRead);
	DeleteCriticalSection(&m_csWrite);
}

BOOL CSerialPort::OpenConnection()
{
	if (m_bConnected)
	{
		return FALSE;
	}
	// 使用 "\\.\COMx" 形式，支持 COM10 及以上
	CString strPath = _T("\\\\.\\") + m_strFileName;
	m_hCom = CreateFileA(strPath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, NULL);
	if (m_hCom == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	SetupComm(m_hCom, MAX_BLOCK, MAX_BLOCK);
	SetCommMask(m_hCom, EV_RXCHAR);

	COMMTIMEOUTS TimeOuts;
	TimeOuts.ReadIntervalTimeout = 55;
	TimeOuts.ReadTotalTimeoutMultiplier = 0;
	TimeOuts.ReadTotalTimeoutConstant = 55;
	TimeOuts.WriteTotalTimeoutMultiplier = 0;
	TimeOuts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(m_hCom, &TimeOuts);

	if (!ConfigConnection())
	{
		CloseHandle(m_hCom);
		m_hCom = INVALID_HANDLE_VALUE;
		return FALSE;
	}

	// 修复句柄泄漏：overlapped 事件只在打开串口时创建一次，读写时复用
	m_hReadEvent  = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_hReadEvent == NULL || m_hWriteEvent == NULL)
	{
		CloseConnection();
		return FALSE;
	}

	PurgeComm(m_hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	m_bConnected = TRUE;
	return TRUE;
}

BOOL CSerialPort::CloseConnection()
{
	if (!m_bConnected && m_hCom == INVALID_HANDLE_VALUE)
	{
		return TRUE;
	}
	// 先置标志，让仍在循环里的收发线程从 IsOpen() 判断后自然退出
	m_bConnected = FALSE;

	// 取消挂起的 overlapped IO 后再关句柄，避免未定义行为
	if (m_hCom != INVALID_HANDLE_VALUE)
	{
		CancelIo(m_hCom);
	}

	// 与正在进行的读/写互斥，确保没有线程还骑在句柄上
	EnterCriticalSection(&m_csRead);
	EnterCriticalSection(&m_csWrite);

	if (m_hCom != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hCom);
		m_hCom = INVALID_HANDLE_VALUE;
	}
	if (m_hReadEvent != NULL)
	{
		CloseHandle(m_hReadEvent);
		m_hReadEvent = NULL;
	}
	if (m_hWriteEvent != NULL)
	{
		CloseHandle(m_hWriteEvent);
		m_hWriteEvent = NULL;
	}

	LeaveCriticalSection(&m_csWrite);
	LeaveCriticalSection(&m_csRead);
	return TRUE;
}

BOOL CSerialPort::ConfigConnection()
{
	DCB dcb;
	if (!GetCommState(m_hCom, &dcb))
	{
		return FALSE;
	}
	dcb.fBinary = TRUE;
	dcb.BaudRate = m_dwBandRate;
	dcb.ByteSize = m_btDataBits;
	dcb.fParity = TRUE;
	switch (m_btParity) // 校验设置
	{
	case 0:
		dcb.Parity = NOPARITY;
		break;
	case 1:
		dcb.Parity = EVENPARITY;
		break;
	case 2:
		dcb.Parity = ODDPARITY;
		break;
	default:
		break;
	}

	switch (m_btStopBits) // 停止位设置
	{
	case 1:
		dcb.StopBits = ONESTOPBIT;
		break;
	case 2:
		dcb.StopBits = TWOSTOPBITS;
		break;
	default:
		break;
	}

	if (!SetCommState(m_hCom, &dcb))
	{
		AfxMessageBox(_T("Can not set DCB"));
		return FALSE;
	}
	return TRUE;
}

//------------------------------------------------------------------------------
// 统一读实现：
// * 修复 ClearCommError 的错误判断逻辑；
// * 恢复 GetOverlappedResult（原版被注释，pending 时返回 0 丢数据）；
// * 复用成员事件，不再泄漏句柄；
// * 加读互斥，防止并发读把一帧拆给两个调用者。
//------------------------------------------------------------------------------
DWORD CSerialPort::ReadCommInternal(void *buf, DWORD dwLength)
{
	if (!IsOpen() || buf == NULL || dwLength < 2)
	{
		return 0;
	}

	EnterCriticalSection(&m_csRead);
	DWORD dwByteOfRead = 0;
	do
	{
		if (!IsOpen() || m_hCom == INVALID_HANDLE_VALUE)
		{
			break;
		}

		COMSTAT comStat;
		DWORD dwErrorFlags = 0;
		// 修复：原判断 (!ClearCommError(...) && dwErrorFlags > 0) 写反。
		// 正确逻辑：调用成功且线路确有错误标志时清接收缓冲。
		if (!ClearCommError(m_hCom, &dwErrorFlags, &comStat))
		{
			break;
		}
		if (dwErrorFlags != 0)
		{
			PurgeComm(m_hCom, PURGE_RXABORT | PURGE_RXCLEAR);
			break;
		}
		if (comStat.cbInQue == 0)
		{
			break;
		}

		DWORD dwBytesToRead = min(dwLength - 1, (DWORD)comStat.cbInQue);

		OVERLAPPED osRead;
		memset(&osRead, 0, sizeof(OVERLAPPED));
		ResetEvent(m_hReadEvent);
		osRead.hEvent = m_hReadEvent;

		BOOL bReadStat = ReadFile(m_hCom, buf, dwBytesToRead, &dwByteOfRead, &osRead);
		if (!bReadStat)
		{
			if (GetLastError() == ERROR_IO_PENDING)
			{
				DWORD dwWait = WaitForSingleObject(osRead.hEvent, 2000);
				if (dwWait == WAIT_OBJECT_0)
				{
					// 修复：原版此调用被注释，pending 完成后拿不到实际长度
					if (!GetOverlappedResult(m_hCom, &osRead, &dwByteOfRead, FALSE))
					{
						dwByteOfRead = 0;
					}
				}
				else
				{
					// 超时：取消本次 IO，避免 OVERLAPPED 出栈后驱动仍写它
					CancelIo(m_hCom);
					GetOverlappedResult(m_hCom, &osRead, &dwByteOfRead, TRUE);
				}
			}
			else
			{
				dwByteOfRead = 0;
			}
		}
	} while (0);

	((unsigned char *)buf)[dwByteOfRead] = '\0';
	LeaveCriticalSection(&m_csRead);
	return dwByteOfRead;
}

DWORD CSerialPort::ReadComm(unsigned char *buf, DWORD dwLength)
{
	return ReadCommInternal(buf, dwLength);
}

DWORD CSerialPort::ReadComm(char *buf, DWORD dwLength)
{
	return ReadCommInternal(buf, dwLength);
}

//------------------------------------------------------------------------------
// 统一写实现：
// * 加写互斥——这是修复"两个线程并发写串口导致发送帧交织"的关键；
// * 修复原版把 WriteFile 的输入长度和输出长度共用一个变量的问题
//   （pending 时 dwLength 未被写入，返回值不可信）；
// * 复用成员事件；超时 CancelIo。
//------------------------------------------------------------------------------
DWORD CSerialPort::WriteCommInternal(LPCVOID buf, DWORD dwLength)
{
	if (!IsOpen() || buf == NULL || dwLength == 0)
	{
		return 0;
	}

	EnterCriticalSection(&m_csWrite);
	DWORD dwByteOfWritten = 0;
	do
	{
		if (!IsOpen() || m_hCom == INVALID_HANDLE_VALUE)
		{
			break;
		}

		COMSTAT comStat;
		DWORD dwErrorFlags = 0;
		if (!ClearCommError(m_hCom, &dwErrorFlags, &comStat))
		{
			break;
		}
		if (dwErrorFlags != 0)
		{
			PurgeComm(m_hCom, PURGE_TXABORT | PURGE_TXCLEAR);
		}

		OVERLAPPED osWrite;
		memset(&osWrite, 0, sizeof(OVERLAPPED));
		ResetEvent(m_hWriteEvent);
		osWrite.hEvent = m_hWriteEvent;

		BOOL bWriteStat = WriteFile(m_hCom, buf, dwLength, &dwByteOfWritten, &osWrite);
		if (!bWriteStat)
		{
			if (GetLastError() == ERROR_IO_PENDING)
			{
				DWORD dwWait = WaitForSingleObject(osWrite.hEvent, 2000);
				if (dwWait == WAIT_OBJECT_0)
				{
					if (!GetOverlappedResult(m_hCom, &osWrite, &dwByteOfWritten, FALSE))
					{
						dwByteOfWritten = 0;
					}
				}
				else
				{
					CancelIo(m_hCom);
					GetOverlappedResult(m_hCom, &osWrite, &dwByteOfWritten, TRUE);
				}
			}
			else
			{
				dwByteOfWritten = 0;
			}
		}
	} while (0);

	LeaveCriticalSection(&m_csWrite);
	return dwByteOfWritten;
}

DWORD CSerialPort::WriteComm(LPCVOID buf, DWORD dwLength)
{
	return WriteCommInternal(buf, dwLength);
}

DWORD CSerialPort::WriteComm(unsigned char *buf, DWORD dwLength)
{
	return WriteCommInternal(buf, dwLength);
}

DWORD CSerialPort::WriteComm(char *buf, DWORD dwLength)
{
	return WriteCommInternal(buf, dwLength);
}

DWORD CSerialPort::WriteComm(unsigned char *buf)
{
	assert(buf != NULL);
	return WriteCommInternal(buf, (DWORD)strlen((char *)buf));
}

DWORD CSerialPort::ThreadMethod()
{
	return 0;
}
