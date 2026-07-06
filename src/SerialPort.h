
#pragma once
#include "BaseThread.h"
#include <memory>

#define MAX_BLOCK			4096

//==============================================================================
// CSerialPort 修改说明（对照原版）：
// 1. ReadComm/WriteComm 原来每次调用 CreateEvent 且从不 CloseHandle，
//    每小时泄漏十几万内核句柄，长时间运行后程序卡死。
//    → 事件改为类成员 m_hReadEvent/m_hWriteEvent，打开串口时创建、关闭时释放。
// 2. 原 ReadComm 中 GetOverlappedResult 被注释掉，IO 进入 pending 时
//    dwByteOfRead 恒为 0，这批数据被无声丢弃（数据传输异常的来源之一）。
//    → 恢复 GetOverlappedResult，超时时 CancelIo 后再取结果。
// 3. 原 ClearCommError 的判断 (!ClearCommError(...) && dwErrorFlags > 0)
//    逻辑写反（调用成功且确有错误时反而不清理）。→ 修正。
// 4. 原构造函数 m_btParity(ONESTOPBIT), m_btStopBits(NOPARITY) 两个初值
//    互相写反，导致默认以 8E1（偶校验）打开串口而不是 8N1。→ 修正为
//    NOPARITY / ONESTOPBIT。若某台设备确实依赖偶校验，请在打开前显式
//    调用 SetParity(1)。
// 5. 读、写各加一把临界区：写路径被"状态轮询线程 + UI 线程"并发调用，
//    原来两次 WriteFile 的字节流会在驱动层交织成坏帧。
// 6. CloseConnection 先置 m_bConnected=FALSE 并 CancelIo，再关句柄，
//    避免关闭时仍有挂起 overlapped IO 造成的未定义行为。
//==============================================================================
class CSerialPort //:public CBaseThread
{
public:
	CSerialPort(void);
	~CSerialPort(void);

	BOOL OpenConnection();
	BOOL CloseConnection();

	DWORD ReadComm(unsigned char *buf, DWORD dwLength);
	DWORD ReadComm(char *buf, DWORD dwLength);
	DWORD WriteComm(LPCVOID buf, DWORD dwLength);
	DWORD WriteComm(unsigned char *buf, DWORD dwLength);
	DWORD WriteComm(unsigned char *buf);
	DWORD WriteComm(char *buf, DWORD dwLength);
	BOOL IsOpen() const {return m_bConnected;}
	HANDLE GetHandle()const {return m_hCom;}

	void SetCom(const CString &strNewCom){m_strFileName = strNewCom;}
	void SetBand(DWORD dwNewBandRate){m_dwBandRate = dwNewBandRate;}
	void SetDataBits(BYTE btNewDataBits){m_btDataBits = btNewDataBits;}
	void SetParity(BYTE btNewParity){m_btParity = btNewParity;}
	void SetStopBits(BYTE btNewStopBits){m_btStopBits = btNewStopBits;}

	DWORD ThreadMethod();
	char recedata;
	CString str;

private:
	BOOL ConfigConnection();
	DWORD ReadCommInternal(void *buf, DWORD dwLength);      // 统一的读实现
	DWORD WriteCommInternal(LPCVOID buf, DWORD dwLength);   // 统一的写实现

public:
	BOOL m_bConnected;				// 串口连接状态
	HANDLE m_hCom;					// 串口句柄
	CString m_strFileName;			// 要打开的串口逻辑名，如 "COM1"
	DWORD m_dwBandRate;				// 波特率
	BYTE m_btDataBits;				// 数据位
	BYTE m_btParity;				// 校验位 0=无 1=偶 2=奇
	BYTE m_btStopBits;				// 停止位 1 / 2

private:
	HANDLE m_hReadEvent;			// 复用的 overlapped 读事件（修复句柄泄漏）
	HANDLE m_hWriteEvent;			// 复用的 overlapped 写事件（修复句柄泄漏）
	CRITICAL_SECTION m_csRead;		// 读互斥
	CRITICAL_SECTION m_csWrite;		// 写互斥（防止多线程并发写造成帧交织）
};
