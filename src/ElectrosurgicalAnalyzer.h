#pragma once
#include "SerialPort.h"
#define  BUFFERSIZE 1024
#define TIMEOFWRITEREAD 100
// 等待电刀应答的最长时间(ms)：接收线程 100ms 轮询一次串口，
// 仪器自身响应普遍在 100~300ms，800ms 足够两个来回
#define ESU_RESPONSE_TIMEOUT_MS 800

//==============================================================================
// CElectrosurgicalAnalyzer 修改说明（对照原版）：
// 1. 原版发出 READ:ALL? 后仅 Sleep(100) 就解析 m_strContinueIPV，而接收
//    线程 300ms 才轮询一次串口，拿到的几乎总是"上一次"的应答（数据滞后
//    一拍）；bGetAll 标志跨线程无同步；应答分包时只取到前半。
//    → 改为"请求-应答序号匹配"：
//      a) 接收线程按行（\r 或 \n 结尾）组装完整应答，存入 m_strLastResponse
//         并递增应答序号 m_dwRespSeq（临界区保护）；
//      b) getAllValue() 发查询前记录当前序号；
//      c) getContIPV 等待序号变化（即"本次"应答到达）后才解析，超时则
//         输出置 0 并返回，绝不解析旧字符串。
// 2. 接收线程轮询周期从 300ms 缩短到 100ms，降低应答等待延迟。
// 3. interceptPULAllIP 增加除零保护（原版 fImA=0 时得到 inf）。
// 4. 对外接口保持不变；新增带返回值的 getContIPVEx 供需要判断
//    "本次读数是否有效"的调用方使用。
//==============================================================================

//新增电刀类型选择
enum AnalyzerModel {
	MODEL_ESU2400,
	MODEL_ESXTRA
};


class CElectrosurgicalAnalyzer:public CBaseThread//电刀分析仪
{
public:
	CElectrosurgicalAnalyzer();
	~CElectrosurgicalAnalyzer();
public:
	CSerialPort m_SerialPort;
	DWORD ThreadMethod();
	DWORD ThreadMethodBla();
	DWORD ThreadMethodFrock();
	void setElectrSurgAnalyzerCOM(const CString &strNewCom, DWORD dwNewBandRate);
	BOOL m_bConnectElectrSurgAnalyzerCOM;
	BOOL openElectrSurgAnalyzerCOM();
	BOOL closeElectrSurgAnalyzerCOM();
	bool bsetPulsedMode;//脉冲模式
	void setDisplayMeasureRFEnergy(int sreen);
	void setDisplayScreenNumber(int numberValue);
	void setPulsedMode();
	void setContinuousMode();
	void setImpedace(int impedanceVal);
	bool bGetAll;
	void getAllValue();
	void interceptContAllIP(CString strAll, float *fPWatt, float *fImA, float *VLoat);
	void interceptContAllPCf(CString strAll, float *fPWatt, float *fCF);
	void interceptPULAllIP(CString strAll, float *fPWatt, float *fImA, float *VLoat);
	bool bGetContIPV;
	bool bGetContPCf;
	bool bGetPULIPV;
	CString m_strContinueIPV;
	CString m_strContinePCF;
	CString m_strPULIPV;
	void getContIPV(float *fPWatt, float *fImA, float *VLoat);       //读取功率实际电压、电流负载电阻
	void getContPCf(float *fPWatt, float *fCF);
	void getPULIPV(float *fPWatt, float *fImA, float *VLoat);

	// 新增：带有效性返回值的读取接口，FALSE=本次查询超时/无应答
	BOOL getContIPVEx(float *fPWatt, float *fImA, float *VLoat, DWORD timeoutMs = ESU_RESPONSE_TIMEOUT_MS);

	//新：设置/读取电刀型号
	void SetAnalyzerModel(AnalyzerModel model) { m_Model = model;}
	AnalyzerModel GetAnalyzerModel() const { return m_Model; }

	//新：ESXTRA专用接口
	void SetESXTRAMode(int mode);
	void SetESXTRAFrequency(float freq);
	void CalibrateESXTRA();


private:
	AnalyzerModel m_Model;

	// 新增：应答同步（请求-应答序号匹配）
	CRITICAL_SECTION m_csResp;
	CString m_strLastResponse;   // 最近一条完整应答（已按型号解析为 P,I,V 格式）
	DWORD   m_dwRespSeq;         // 应答序号，接收线程每收到完整一行 +1
	CString m_strLineBuf;        // 行组装缓冲（仅接收线程访问）

	DWORD GetRespSeq();
	// 等待出现比 seqBefore 新的应答；成功返回 TRUE 并输出应答内容
	BOOL WaitResponse(DWORD seqBefore, DWORD timeoutMs, CString *strResp);
	// 接收线程收到完整一行后调用：按型号解析并存储
	void OnResponseLine(const CString &strLine);

	// 新增：内部辅助函数
	void SendESXTRACommand(const CString& command);
	void ParseESXTRAResponse(CString strResponse);
};
