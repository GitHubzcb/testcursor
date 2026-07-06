#pragma once
#include "visatype.h"
#define MAX_REC_SIZE 200
#define SETTIMESTEP 200 //操作间隔

//==============================================================================
// CPowerStation 修改说明（对照原版）：
// 1. 原版每一次读/写都完整执行 viOpenDefaultRM→viOpen→IO→viClose×2，
//    采集线程 10ms 循环反复做，同时 UI 线程按钮处理也在做——两个线程
//    并发高频开关 VISA 会话，容易造成 NI-VISA 内部死锁与资源泄漏。
//    → 改为持久会话：ConnectInstr 成功后保持 defaultRM/instr 打开，
//      读写直接复用；DisconnectInstr()/析构时统一释放。
// 2. 全部 VISA 访问加临界区互斥，禁止两个线程并发访问同一台仪器。
// 3. 修复 ConnectInstr 中 new unsigned long 的两处内存泄漏（改为局部变量），
//    并补上 viClose(defaultRM)。
// 4. 对外接口保持不变（InstrWrite/InstrRead 仍接收地址参数，内部会在
//    会话未建立时按该地址自动建立会话）。
//==============================================================================
class CPowerStation//电源台
{
private:
	int curDPChannel;//当前使用通道
	CString	m_strPowerStationInstrAddr;//需要显示名称的话，用返回接口

	// 新增：持久 VISA 会话 + 互斥
	ViSession m_defaultRM;
	ViSession m_instr;
	bool m_bSessionOpen;
	CRITICAL_SECTION m_csVisa;

	// 会话未建立时按 strAddr 建立（调用方须已持有 m_csVisa）
	bool EnsureSession(const CString &strAddr);
public:
	CPowerStation();
	~CPowerStation();
	bool InstrRead(CString strAddr, CString *pstrResult);
	bool InstrWrite(CString strAddr, CString strContent);
	void setCurrentChannel(int setValue);//设置当前通道
	bool ConnectInstr();//查找设备
	void DisconnectInstr();//新增：释放持久会话
	bool getSetV(float *fV);
	bool setLimitVA(float fVlimit, float fAlimit);//设置电源台限制电压和电流
	bool openChannel();//打开当前通道
	bool closeChannel();//关闭当前通道
	bool getAllValue(float *fV,float *fA, float *fP);//读取电压 电流 功率
};
