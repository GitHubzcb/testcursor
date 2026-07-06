#pragma once
#include "SerialPort.h"
#include "pc_m2_protocol.h"
#include <queue>
#include <string>
#include <vector>
#include <iostream>
using namespace std;
#define  BUFFERSIZE 1024

//==============================================================================
// CBLACommunicate 修改说明（对照原版）：
// 1. m_ExitBlaThreadEvent 原来从未 CreateEvent 就被 Set/Reset/Wait（野句柄，
//    未定义行为，也导致关串口时轮询线程收不到退出信号）。
//    → 构造函数中创建，析构中关闭。
// 2. 发送路径：原来所有命令通过全局缓冲 dat[2100]/send_len 组包，
//    "状态轮询线程 + UI 线程"并发调用时组包互相覆盖、串口字节流交织，
//    是数据传输异常的首要来源。
//    → 新增 SendFrame()：组包进成员缓冲 + 临界区互斥 + 一次性写出，
//      所有 Set*/Get 命令统一走 SendFrame。
// 3. 接收路径：原粘包逻辑"校验用 sticky_buf、解析却用 pBuf"，分包帧被
//    丢弃或解析成垃圾；data_lenth 无上限校验可越界读。
//    → ThreadMethodBla 改为环形缓冲逐帧提取（TryExtractFrame），
//      支持任意分包/粘包/噪声字节，并做长度上限校验。
// 4. pc_test_status 原来被接收线程 memcpy 覆写、判定线程同时裸读（撕裂读）。
//    → 写入加锁并记录时间戳；读取一律通过 GetStatusSnapshot() 取快照，
//      数据超过 maxAgeMs（默认 1000ms）视为过期返回 FALSE——
//      从根上消除"通讯中断后拿旧值判定为不合格"的问题。
// 5. WriteComm 返回值语义：原多处把"返回 0"当成功（写反），curPage 因此
//    在发送失败时被更新、发送成功时不更新，页面门禁失步后所有 Get* 拒收
//    新数据。→ 统一为 SendFrame 返回 TRUE=成功，curPage 仅在成功后更新。
// 6. 修复 GetRFOutput 成功路径 bRet 未置 TRUE、SetRFTestPageParameter 用错
//    sizeof(stim_param)、若干 Set* 忽略 bRet 恒返回 TRUE 等问题。
// 7. 修复 data[250] = {0}（对下标 250 的越界写）。
// 8. openBlaCOM/closeBlaCOM 生命周期：关闭时先通知并等待轮询线程退出、
//    再停接收线程、最后关串口；防止对已关闭句柄读写及轮询线程堆积。
//==============================================================================

typedef enum 
{
	electrode1_monpolar = 1,
	electrode2_monpolar,
	electrode1_electrode2_monpolar,
	electrode1_electrode2_bipolar,
	electrode1_bipolar,
	electrode2_bipolar,
}eletrodeMode;

typedef struct
{
	float fVmeV;
	float fImemA;
	float fPW;
	float fVinV;    // 输入电压
	float fIinA;    // 输入电流
	float fTT;      // 变压器温度
	float fTI;      // 电感温度
	float fTH;      // 散热器温度
	float fEletrodeTemp[2];
}CaliRFIVPowerDetectElectrodeTemp;

typedef struct
{
	float fPW;
	float fImA;
	float fVV;
	float fVmeV;
	float fImemA;
	float fPmeW;
}RFAccuacy;   // 射频准确度

class CBLACommunicate :public CBaseThread
{
public:
	CBLACommunicate();
	~CBLACommunicate();
public:
	CSerialPort m_BlaSerialPort;
	DWORD ThreadMethodBla();
	DWORD ThreadMethod();
	DWORD ThreadMethodFrock();
	void setBlaCOM(const CString &strNewCom, DWORD dwNewBandRate);
	BOOL m_bConnectBlaCOM;
	BOOL openBlaCOM();
	BOOL closeBlaCOM();
	BOOL ExitBlaConnect();
	BOOL PCSetBlaPageDisPlay(unsigned char page);   // 页面设置
	BOOL PCControlBlaKey(u16 key_value);            // 模拟按键
	BOOL ElectrodeSelect(eletrodeMode eletrodeMode);
	BOOL BlaStartandStop(unsigned char flag);       // 0 启动 1 停止
	BOOL GetBlaStatue();                            // 发送状态查询帧

	t_pc_test_status pc_test_status;   // 最近一帧状态（仅接收线程在锁内写）
	unsigned char curPage;             // 上位机认为的 MCU 当前页面

	HANDLE hGetBlaStateThread;
	HANDLE m_ExitBlaThreadEvent;       // 修复：现在真正创建了
	static DWORD WINAPI CommGetBlaStateThread(LPVOID lparamter); // 状态轮询线程

	//0818q
	tCom_Device_Info DeviceInfo;
	HANDLE m_hDeviceInfoEvent;         // 设备信息到达事件

	//--------------------------------------------------------------------------
	// 新增：线程安全的数据访问接口
	//--------------------------------------------------------------------------
	// 取状态快照；数据龄超过 maxAgeMs 返回 FALSE（视为通讯中断/数据过期）
	BOOL GetStatusSnapshot(t_pc_test_status *dst, DWORD maxAgeMs = 1000);
	// 数据是否新鲜（供上层显示"通讯中断"提示）
	BOOL IsStatusFresh(DWORD maxAgeMs = 1000);
	// MCU 实际回传的页面号（用于与 curPage 对账），无有效数据返回 0xFFFF
	u16  GetMcuPageID();

private:
	u8 data[250];                      // 命令负载临时区（仅锁内使用）
	tCtRfParam ct_rf_param;
	tStimParam stim_param;
	queue<string> isis_uart_send_queue;

	// 新增：发送互斥 + 成员发送缓冲（替代全局 dat/send_len）
	CRITICAL_SECTION m_csSend;
	unsigned char m_sendBuf[2100];
	// 新增：状态数据锁 + 时间戳
	CRITICAL_SECTION m_csStatus;
	DWORD m_dwLastStatusTick;          // 最近一次成功解析状态帧的 GetTickCount
	BOOL  m_bStatusEverReceived;

	// 新增：统一发送入口（组包 + 互斥 + 写出），TRUE=整帧写出成功
	BOOL SendFrame(PC_PROTOCOL_COMM_FLAG flag, const unsigned char *payload, unsigned int payloadLen);
	// 新增：环形缓冲逐帧提取，返回帧总长（0 = 暂无完整帧）
	static int TryExtractFrame(std::vector<unsigned char> &rx, unsigned char *out, int outCap);
	// 页面门禁：curPage 或 MCU 回传页面命中其一即放行
	BOOL IsOnPage(unsigned char page);

public:
	// 刺激功能封装
	BOOL SetEleStimulationTestVVValue(int setVVValue);
	BOOL SetEleStimulationTestImAValue(int setImAValue);
	BOOL GetEleStimulationMesFeedbackElectrode1IV(float * fElectrode1FeedIV);
	BOOL GetEleStimulationEleImpedanceValue(float *fEleImpedanceValue);
	BOOL GetEleStimulationelElectrodeTempValue(float fEletrodeTemp[2]);
public:
	// 射频功能封装
	BOOL GetEleRFElectrode1Temp(float * fElectrode1Temp);   // 第一路温度
	BOOL GetEleRFElectrode2Temp(float* fElectrode2Temp);    // 第二路温度
	// 射频参数设置
	BOOL SetRFTestPageParameter(tRfTestParam setParam);     // 后台射频参数
	BOOL SetRFPageParameter(tCtRfParam setParam);           // 连续射频参数
	// 读取连续射频输出电压、电流、功率
	BOOL GetRFOutput(float* fVoltage, float* fCurrent, float* fPower);

	// 测量电压、电流和功率
	BOOL GetRFTestVWImAPW(float *fVmeV, float *fImemA, float *fPW);
	// 监测电压、电流和功率
	BOOL GetMonitorVVImAPW(float *fVmoV, float *fImomA, float *fPW);
	// 输入电压、输入电流、变压器温度、电感温度、散热器温度
	BOOL GetVinVIINATTTITH(float *fVinV, float *fIinA, float *fTT, float *fTI, float *fTH);
	BOOL GetCaliRFIVPowerDetectElectrodeTemp(CaliRFIVPowerDetectElectrodeTemp *dst);
	// 读取 NEM 值 = CQM
	bool GetCQM(unsigned short* CQMval);
	BOOL GetRFAccuacy(RFAccuacy *getBlaVal);
	BOOL bGetBlaState;
	BOOL setM2Version(t_sys_info sys_info);   // 用于写硬件版本
	BOOL shutPCProgram();

	// 刺激参数设置命令
	BOOL SetStimParameter(tStimParam setParam);

	BOOL SetPurfParameter(tPuRfParam setParam);   // 脉冲射频参数设置
	BOOL GetPurfTempEle1(float* temp);            // 读取脉冲射频温度
	BOOL GetPurfVoltage(float* volt);             // 输出电压
	BOOL GetPurfWidth(float* width);              // 脉宽

	BOOL GetInformation(CString* picture);        // 读取系统信息

	BOOL SetSEEGParams(tELModeStat param);
};
