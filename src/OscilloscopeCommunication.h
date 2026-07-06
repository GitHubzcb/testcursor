#pragma once
#include "visatype.h"
#define MAX_REC_SIZE 200
#define TIME_STEP 50

//==============================================================================
// COscilloscopeCommunication 修改说明（对照原版）：
// 1. 原版每一次读/写都完整执行 viOpenDefaultRM→viOpen→IO→viClose×2，
//    且中间夹 4 次 Sleep(50)（单次写耗时 200ms+）；工作线程与 UI 线程
//    并发高频开关 VISA 会话，容易造成 NI-VISA 内部死锁。
//    → 改为持久会话 + 临界区互斥（与 CPowerStation 相同方案）：
//      ConnectInstr 成功后保持会话，InstrWrite/InstrRead 直接复用。
// 2. 修复 ConnectInstr 中 new unsigned long 的两处内存泄漏，
//    补上 viClose(defaultRM)。
// 3. 修复构造函数 strmodeValue[5] 漏赋值、strmodeValue[6] 被赋两次
//    （"RS232" 被 "IIC" 覆盖）的笔误。
// 4. 其余测量函数（setScale/getVMAX/...）全部经由 InstrWrite/InstrRead
//    访问仪器，无需改动，自动获得互斥与会话复用。
//    ★ 本头文件对应的 OscilloscopeCommunication.cpp 只需替换
//      构造函数/析构函数/ConnectInstr/InstrWrite/InstrRead 五个函数，
//      替换代码见 patches/OscilloscopeCommunication.cpp.patch.md。
//==============================================================================
typedef enum
{
	EDGE = 0,
	PULSe,
	SLOPe,
	VIDeo,
	PATTern,
	RS232,
	IIC,
	SPI,
	CAN,
	FLEXray,
	USB
}enumTrigMode;
typedef enum
{
	NORMal = 0,//普通
	AVERages,//平均
	PEAK,//峰值检测
	HRESolution//高分辨率
}enumACQTYPE;
typedef enum
{
	PGReater = 0,
	PLESs,
	NGReater,
	NLESs,
	PGLess,
	NGLess
}enumTrigModePULSeWhen;
class COscilloscopeCommunication
{
private:
	CString	m_strDSOInstrAddr;
	CString	m_strDSOResult;
	int m_curChannel;
	float curChannelScale;

	// 新增：持久 VISA 会话 + 互斥
	ViSession m_defaultRM;
	ViSession m_instr;
	bool m_bSessionOpen;
	CRITICAL_SECTION m_csVisa;
	// 会话未建立时按 strAddr 建立（调用方须已持有 m_csVisa）
	bool EnsureSession(const CString &strAddr);
public:
	COscilloscopeCommunication();
	~COscilloscopeCommunication();
	bool InstrRead(CString strAddr, CString *pstrResult);
	bool InstrWrite(CString strAddr, CString strContent);

	bool ConnectInstr();
	void DisconnectInstr();   // 新增：释放持久会话
	//设置触发方式 Auto Normal Single
	CString strmodeValue[11];
	bool setTrigMode(enumTrigMode modeValue);//设置触发类型
	bool setTrigModePULSeWhen(enumTrigModePULSeWhen whenVal);
	bool setTrigPULSeSOURce();//脉冲触发通道设置
	bool setTrigEDGeSOURce();//边沿触发通道设置
	bool setTrigSWEAuto();//優化触发方式
	bool setTrigSWENormal();
	bool setTrigSWESingle();
	bool setScale(float fscaleVal);
	bool getScale(float *fscaleVal);
	bool setProbe(float fPorbeValue);
	bool setTimebaseDelayOffset(float fDelayOffsetVal);//:TIMebase:DELay:OFFSet
	bool setTimebaseOffset(float fOffsetVal);
	bool getTimeBaseOffset(float *fTimeBaseOffSetVal);
	//设置触发电平
	bool setTrigEDGLEV(float level);
	bool setTimeBaseScale(float ftimeBaseScal);//秒
	bool getRTime(float *fRTimes);//上升沿时间
	bool getFTime(float *fFTimes);//下降沿时间
	bool getFREQuency(int *fFREQuencyHz);
	bool getPWIth(float *fPWidth);
	bool getVMAX(float *fvMax);
	bool getVMin(float *fvMin);
	void setCurrentChannel(int setValue);
	bool setCouplingAC();
	bool setCouplingDC();
	bool setACQuireTYPE(enumACQTYPE acqType);

	//0801
	bool SetupEdgeMeasurement(bool isRiseTime);
	bool GetEdgeTime(bool isRiseTime, float* result_us);

	//0904新增示波器双通道模式
	bool setMemoryDepth(const CString& depthValue);
	bool getMemoryDepth(CString* depthResult);
	bool setupDualChannels(int ch1, int ch2, float ch1_scale, float ch2_scale,
		const CString& memoryDepth = "AUTO");
	bool measureDualChannelTiming(int triggerChannel,
		float* ch1_period, float* ch2_period,
		float* ch1_riseTime = nullptr, float* ch2_riseTime = nullptr,
		float* ch1_fallTime = nullptr, float* ch2_fallTime = nullptr);

	bool getWaveformTimeInfo(int channel, float* startTime, float* endTime, float* timeIncrement, float* dura);

	// 新增声明：获取单个完整波形时长
	bool getSingleWaveformDuration(int channel, float* duration_ms);
	bool setupForSingleAcquisition(int triggerChannel);
	bool waitForTriggerComplete();


	bool setupForControlMeasurement(int channel);   //控制时基和触发模式
	bool measureControlPeriod(int channel, float* controlPeriod); //测量控制周期
	bool getOutputDuration(int channel, float* duration);//测量输出持续时间



};
