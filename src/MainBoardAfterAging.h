#pragma once
#include "afxwin.h"
#include "setupapi.h"
#include "DEVGUID.H"
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib,"Setupapi.lib.")

#include "ElectrosurgicalAnalyzer.h"
#include "PowerStation.h"
#include "OscilloscopeCommunication.h"
#include "BLACommunicate.h"
#include "MyStatic.h"
#include "KingPoElecAnalyzer.h"
#include <memory>


//0724
#include <afxmt.h>
#include <map>
#define WM_UPDATE_TEST_DATA (WM_USER + 100)
#define WM_UPDATE_COMBO_BOX (WM_USER + 101)

//==============================================================================
// CMainBoardAfterAging 头文件修改说明（对照原版）：
// 1. 新增成员 m_nEleTypeSel：电刀型号下拉框选中项的缓存。
//    工作线程（RunDataCollection→GetShow7EleSurAnaBlaValue 等）原来直接调
//    m_eleType.GetCurSel()，底层是跨线程 SendMessage，与 UI 线程的 Sleep
//    形成双向等待（死机主因之一）。改为 UI 线程在 OnInitDialog /
//    OnCbnSelchangeComboEletype 里更新该缓存，工作线程只读缓存。
// 2. 其余声明保持不变；对应 .cpp 的修改见
//    patches/MainBoardAfterAging.cpp.修改指南.md。
//==============================================================================

//#include "CSpreadSheet.h"

#define OPERATEQUIPSTEPTIME 200
#define MAINBOARDTESTITEMSNUMS 85//82
// CMainBoardAfterAging 对话框
typedef enum
{
	nonTest = 1,
	downExe3,
	caliRFIV7,
	lowRFTest8,
	HighRFTest9,
	PowerDetectionFun11,
	ImpedanceAccuracy13,
	StimAccuracy14,
	ElectrodeTemp15,
	NEMCalACC
}curBlaTestFun;
typedef enum
{
	nonNem = 1,
	NEMCal,
	NEMAccCal,
	NEMFallOff,
	NEMShortCir
}curNEM;
typedef enum
{
	nonVR = 1,
	VR1,
	VR2,
	VR3,
	VR4,
	VR5,
	VR6
}curImpedanceAccuracy13;
typedef enum
{
	nonStimAccuracy = 1,
	//StimAcc10V1K1,
	StimDSORiseTime,
	StimDSOFallTime,
	StimAcc10V1K2,
	StimAcc5V500,
	StimAcc8mA500,
	StimAcc1V100,
	StimAcc2mA100
}curStimAccuracy14;
typedef struct
{
	CString strID;//1
	CString strProductCode;
	CString strBatchNumber;
	CString strHardwareVersion;
	CString str1VisualVariableQuality;//1
	CString str2ConnectState;//1
	CString str3DownloadExe[2];//2
	CString str4PowerCheck[2];//2
	CString str5Post[2];//2
	CString str6ShowTemp;//1
	CString str7rCalREVolandI[6];//去掉VPP   6
	CString str8LowRFAccuracy[8];//8
	CString str9HighRFAccuracy[8];//8
	CString str10FanTest;//1
	CString str11PowerMonitor[5];//5
	CString str12ImpedanceCal;//1
	CString str13ImpedanceAccuary[7];//7
	CString str14EleStimulationAccuary[19];//19
	CString str15ElectrodeTempMeasure[2];
	CString str16NEMCal;
	CString str17NEMAccuary[3];
	CString str18RelaySwitch;
	CString str19ComputerConnect;
	CString str20JumperRecoverandStartupCheck;
	CString str21SliconFixed;
	CString strKeyComPonents[6];
	CString strPasteFlag;
	CString strMainBoardAfterAgingTestFlag;
	CString strTesterName;
	CString strTestDate;
}mainBoardAferAgingWrite2Excel, *pMainBoardAferAgingWrite2Excel;
typedef struct
{
	BOOL b1VisualVariableQuality;
	BOOL b2ConnectState;
	BOOL b3DownloadExe[2];
	BOOL b4PowerCheck[2];
	BOOL b5Post[2];
	BOOL b6ShowTemp;
	BOOL b7rCalREVolandI[6];//去掉VPP
	BOOL b8LowRFAccuracy[8];
	BOOL b9HighRFAccuracy[8];
	BOOL b10FanTest;
	BOOL b11PowerMonitor[5];
	BOOL b12ImpedanceCal;
	BOOL b13ImpedanceAccuary[7];
	BOOL b14EleStimulationAccuary[19];
	BOOL b15ElectrodeTempMeasure[2];
	BOOL b16NEMCal;
	BOOL b17NEMAccuary[3];
	BOOL b18RelaySwitch;
	BOOL b19ComputerConnect;
	BOOL b20JumperRecoverandStartupCheck;
	BOOL b21SliconFixed;
	BOOL bPasteFlag;
	BOOL bMainBoardAfterAgingTestFlag;
	//BOOL bTesterName;
	//BOOL bTestDate;
}bMainBoardAferAgingQulity;
typedef struct
{
	BOOL b3DownloadExeCurFlg;
	BOOL b4PowerCheckCurFlg;
	BOOL b5rCalREVolandICurFlg;
	BOOL b6LowHighRFAccuracyCurFlg;
	//BOOL b7PowerMonitorCurFlg;
	BOOL b9ImpedanceAccuaryCurFlg[6];
	BOOL b10EleStimulationAccuaryCurFlg[9];//0代表电刺激 1：0v1k 2:up Step 3:down Step 4:10v1k 5:5v500 6:8ma500 7:1v100 8:2ma100
	BOOL b12NEMCAL;
	BOOL b13NEMACC;
	BOOL b14EleRelayTest;
}bMainBoardAferAgingCurTestingFlag;
typedef struct
{
	float fPowerStationA;
	float fEleSurAnaPW;
	float fEleSurAnaImA;
	float fEleSurAnaVV;
	float fBlaVmoV;
	float fBlaImomA;
	float fBlaPoW;
	float fBlaVmeV;
	float fBlaImemA;
	float fBlaPmeW;
}GetPRAccuracy;
typedef struct
{
	float fDSOvMax;
	float fDSOvMin;
	float fBlaElectrode1FeedIV;
}GetStimAcc;

class CMainBoardAfterAging : public CDialogEx
{
	DECLARE_DYNAMIC(CMainBoardAfterAging)

public:
	CMainBoardAfterAging(CWnd* pParent = NULL);   // 标准构造函数
	virtual ~CMainBoardAfterAging();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG_MAINBOADAFERAGING };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

	//0723 BLA线程
	CWinThread* m_pBlaComThread;
	HANDLE m_hBlaComEvent;  
	bool m_bBlaComThreadRunning;

	// 线程参数结构
	struct BlaComThreadParam {
		CMainBoardAfterAging* pThis;
		CString comPort;
		int baudRate;
		bool bOpen; // true=打开, false=关闭
	};
	// 线程函数
	static UINT BlaComThreadProc(LPVOID pParam);
	#define WM_BLACOM_RESULT (WM_USER + 200)
	afx_msg LRESULT OnBlaComResult(WPARAM wParam, LPARAM lParam);
	////0723OVER

	//0724
	virtual void OnDestroy();
	//线程控制函数
	void StartDataCollection();
	void StopDataCollection();
	// 线程函数
	static UINT DataCollectionThread(LPVOID pParam);
	UINT RunDataCollection();

	// 数据处理函数
	void ProcessTestData();
	void ProcessComboBoxUpdate();

	// UI更新函数
	void UpdateTestUI();
	void UpdateComboBoxUI();

	// 消息处理函数
	afx_msg LRESULT OnUpdateTestData(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUpdateComboBox(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	// 线程相关变量
	CWinThread* m_pDataThread;
	HANDLE m_hThreadEvent;
	bool m_bThreadRunning;
	CCriticalSection m_csDataLock;

	// 新增成员变量
	int DevCntLast;
	int DevCnt;

	// 新增：电刀型号下拉框选中项缓存。
	// 工作线程严禁调用 m_eleType.GetCurSel()（跨线程 SendMessage 会与
	// UI 线程的 Sleep 互相等待造成死锁），只允许读该缓存；
	// UI 线程在 OnInitDialog / OnCbnSelchangeComboEletype 中维护它。
	int m_nEleTypeSel;


	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	virtual BOOL OnInitDialog();
	CComboBox m_combo_serial_EleSurAna;
	afx_msg void OnBnClickedButtonOpencloseelesurgana();
	BOOL GetUsbSn(TCHAR *pszComName);
	CElectrosurgicalAnalyzer m_eleSurgAnalyer;
	CPowerStation m_powerStation;
	COscilloscopeCommunication m_DSO;
	CString EleSurAnaComName;
	void  upDateComCombox(CComboBox comComboxName, CString oldComNumber);
	CString blaComName;
	afx_msg void OnBnClickedButton6();
	CString m_str7EleSurAnaPRW;
	CString m_str7EleSurAnaIRmA;
	CString m_str7EleSurAnaVPPV;//不需要
	CString m_str7EleSurAnaVV;
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedButtonPowerstationopen();
	//未使用
	afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnCbnSelchangeComboPowerstationchannels();
	CComboBox m_powerStationChannels;
	afx_msg void OnBnClickedButton17();
	afx_msg void OnBnClickedRadio1bvisualtest1();
	afx_msg void OnBnClickedRadio2connectstate1();
	bMainBoardAferAgingCurTestingFlag m_curTestingFlg;
	void initCurTestFlag();
	BOOL checkCurTestState(int curPro);//1:电刺激测试
	float f3PowerStationI;
	BOOL GetandShow3Value();
	afx_msg void OnBnClickedButton3startstop();
	afx_msg void OnBnClickedRadio3programupgrade1();
	afx_msg void OnBnClickedRadio4led1();
	afx_msg void OnBnClickedRadio4dl1p1();
	afx_msg void OnBnClickedButton4startstop();
	//afx_msg void OnBnClickedButton5start();
	afx_msg void OnBnClickedRadio5post1();
	afx_msg void OnBnClickedRadioLedtone1();
	afx_msg void OnBnClickedRadio6tempdisplay1();
	afx_msg void OnBnClickedButton7setelesuranalyzer100();
	//BOOL GetBlaValue7();
	BOOL GetShow7EleSurAnaBlaValue();
	void SaveCompleteJSON(const nlohmann::json& data);
	CaliRFIVPowerDetectElectrodeTemp m_calRFPowerDectEletropTemp;

	afx_msg void OnBnClickedButton7getelesuranalyzer();
	afx_msg void OnBnClickedButton8setelesuranalyzer100();
	void GetBlaValue8();
	GetPRAccuracy m_GetLowPRAccuracyValue;
	BOOL GetShow8PowerStationEleSurAnaBlaValue();
	afx_msg void OnBnClickedButton8getpowerstaelesurana();
	int bSelectedHistoryFile;//0不建历史文件 1默认从行首开始查 2创建新历史文件
	afx_msg void OnBnClickedButton9setelesuranalyzer1000();
	void GetBlaValue9();
	GetPRAccuracy m_GetHighPRAccuracyValue;
	BOOL GetShow9PowerStationEleSurAnaBlaValue();

	afx_msg void OnBnClickedButton9getpowerstaelesurana();
	afx_msg void OnBnClickedRadio17();//10风扇控制
	void GetBlaValue11();
	afx_msg void OnBnClickedRadio12impedancecal1();
	void JudgeCollationData13(int flag, CString strSrc, float minValue, float maxValue);
	void GetBlaValue13();
	afx_msg void OnBnClickedRadio13difimpedancesound1();
	afx_msg void OnBnClickedButton140v1k();
	//BOOL GetShow14DOSBla10V1K1();
	float fRTimes;
	BOOL GetShow14DOSRiseTime();
	float fFTimes;
	BOOL GetShow14DOSFallTime();
	afx_msg void OnBnClickedButton18();//14 10V@1K
	void GetBlaValue14();
	
	BOOL GetShow14DOSBla10V1K2();
	bool stimParaSet(float scaleVal);//刺激标定需要的参数设置
	afx_msg void OnBnClickedButton1410v1kgetpvnv();
	afx_msg void OnBnClickedButton14setelesurana500();
	BOOL GetShow14DOSBla5V500();
	afx_msg void OnBnClickedButton145v500();
	BOOL GetShow14DOSBla8ma500();
	afx_msg void OnBnClickedButton148ma500();
	afx_msg void OnBnClickedButton14setelesurana100();//NEM准确性短接
	BOOL GetShow14DOSBla1v100();
	afx_msg void OnBnClickedButton141v100();
	BOOL GetShow14DOSBla2ma100();
	afx_msg void OnBnClickedButton142ma100();
	void GetBlaValue15();
	afx_msg void OnBnClickedRadio16nemcal1();
	afx_msg void OnBnClickedRadio17nemcal1();
	afx_msg void OnBnClickedRadio17nemfalloff1();
	afx_msg void OnBnClickedRadio17nemshortcircuit1();
	afx_msg void OnBnClickedRadio18relayswitch1();
	afx_msg void OnBnClickedRadio19computerconnect1();
	afx_msg void OnBnClickedRadio20jumperrecoverystartup1();
	afx_msg void OnBnClickedRadio21siliconefixedpotenttimeter1();
	afx_msg void OnBnClickedRadio22pastermark1();

	CString SaveFilename;
	CString OpenFilename;
	CString strData;
	CString m_strDate;
	void GetSystemTime();
	BOOL CeateRecordExcelTable(int endFlag);
	BOOL judgeMainBoardBoforeAgingBQualified();
	afx_msg void OnBnClickedButtonGetqulitywexcel();
	mainBoardAferAgingWrite2Excel m_strMainBoardAfterAgingWrite2Excel;
	bMainBoardAferAgingQulity m_bMainBoardAferAgingQulityFlag;
	CString m_str3PowerStationIA;
	CString m_str7BlaPMWatt;
	CString m_str7BlaIMmA;
	CString m_str7BlaVMV;
	CString m_str8PowerStationIOA;
	CString m_str8EleSurAnaPRWatt;
	CString m_str8EleSurAnaIRmA;
	CString m_str8EleSurAnaVRV;
	CString m_str8BlaVmeV;
	CString m_str8BlaImemA;
	CString m_str8BlaVmoV;
	CString m_str8BlaImomA;
	CString m_str9PowerStationIOA;
	CString m_str9EleSurAnaPRW;
	CString m_str9EleSurAnaIRmA;
	CString m_str9EleSurAnaVRV;
	CString m_str9BlaVme;
	CString m_str9BlaIme;
	CString m_str9BlaVmo;
	CString m_str9BlaImo;
	CString m_str11BlaVinV;
	CString m_str11BlaIINA;
	CString m_str11BlaTTC;
	CString m_str11BlaTIC;
	CString m_str11BlaTHC;
	CString m_str13BlaVR1;
	CString m_str13BlaVR2;
	CString m_str13BlaVR3;
	CString m_str13BlaVR4;
	CString m_str13BlaVR5;
	CString m_str13BlaVR6;
	afx_msg void OnBnClickedRadio14doswaveatzero1();
	afx_msg void OnBnClickedRadio14dossquarewave1();
	CString m_str14DocRiseAlongEdge;
	CString m_str14DocDropEdge;
	CString m_str14Doc101KPositiveVV;
	CString m_str14Doc5500PositiveVV;
	int StimTestFlag;
	afx_msg void OnBnClickedButton14setelesuranalyzer1000();
	CString m_str14Doc8mA500PositiveVV;
	CString m_str14Doc1V100PositiveVV;
	CString m_str14Doc2mA100PositiveVV;
	CString m_str14Doc10V1KNegativeV;
	CString m_str14Doc5V500NegativeV;
	CString m_str14Doc8mA500NegativeV;
	CString m_str14Doc1V100NegativeV;
	CString m_str14Doc2mA100NegativeV;
	CString m_str14Bla10V1KCurrenyFeedBackV;
	CString m_str14Bla5V500CurrenyFeedBackV;
	CString m_str14Bla8mA500CurrenyFeedBackV;
	CString m_str14Bla1V100CurrenyFeedBackV;
	CString m_str14Bla2mA100CurrenyFeedBackV;
	BOOL GetShow13Impedance2000();
	afx_msg void OnBnClickedButton13setelesuranalyzer2000();
	BOOL GetShow13Impedance1000();
	afx_msg void OnBnClickedButton13setelesuranalyzer1000();
	float fEleImpedanceValue;
	BOOL GetShow13Impedance50();
	afx_msg void OnBnClickedButton13setelesuranalyzer50();
	BOOL GetShow13Impedance100();
	afx_msg void OnBnClickedButton13setelesuranalyzer100();
	BOOL GetShow13Impedance200();
	afx_msg void OnBnClickedButton13setelesuranalyzer200();
	BOOL GetShow13Impedance500();
	afx_msg void OnBnClickedButton13setelesuranalyzer500();
	CString m_str15Electrode1;
	CString m_str15Electrode2;
	BOOL bMainBoardAfterAgingTestFlag;//未使用
	void GetAllBlaValue();
	afx_msg void OnBnClickedButtonWrite2excel();
	CString m_str1to21TestResult;
	void initTestProject();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CString m_strLastID;
	afx_msg void OnBnClickedButton7savebla();

	afx_msg void OnBnClickedButton8savebla();//NEM准确性
	
	afx_msg void OnBnClickedButton9savebla();//改为射频准确性测试
	int RFTestFlag;

	BOOL GetShow11BlaValue();
	afx_msg void OnBnClickedButton11getblavalue();
	//
	//int ImpedanceAccuracyFlag;
	afx_msg void OnBnClickedButton13saveblavalue();//改为做阻抗标定
	curNEM m_curNEM;
	unsigned short CQMval;
	bool GetShowNEMCal();
	bool GetShowNEMAccCal();
	bool GetShowNEMFallOff();
	bool GetShowNEMShorCir();
	int m_curNEMFlag;
	afx_msg void OnBnClickedButton1410v1ksaveblavalue();//NEM标定
	afx_msg void OnBnClickedButton145v500saveblavalue();
	afx_msg void OnBnClickedButton148ma500saveblavalue();//NEM准确性标定
	afx_msg void OnBnClickedButton141v100saveblavalue();
	afx_msg void OnBnClickedButton142ma100blasavevalue();//NEM准确性脱落
	float fEletrodeTemp[2];
	BOOL GetShow15BlaElectrodeTemp();
	afx_msg void OnBnClickedButton15saveblavalue();
	CComboBox m_ComboboxBlaPortsName;
	afx_msg void OnBnClickedButtonBlacomlink();
	CBLACommunicate m_blaCommunication;
	eletrodeMode m_setEletrodeMode;
	//afx_msg void OnTimer(UINT_PTR nIDEvent);
	curBlaTestFun m_curBlatestFun;
	curImpedanceAccuracy13 m_curImpedanceAcc13Test;
	curStimAccuracy14 m_curStimAcc14Test;
	CMyStatic m_3PowerStationIAUI;
	CMyStatic m_7EleSurAnaPRWUI;
	CMyStatic m_UI7EleSurAnaIRmA;
	CMyStatic m_UI7EleSurAnaVPPV;
	CMyStatic m_UI7EleSurAnaVV;
	CMyStatic m_UI7BlaPMWatt;
	CMyStatic m_UI7BlaIMmA;
	CMyStatic m_UI7BlaVMV;
	CMyStatic m_UI8PowerStationIOA;
	CMyStatic m_UI8EleSurAnaPRWatt;
	CMyStatic m_UI8EleSurAnaIRmA;
	CMyStatic m_UI8EleSurAnaVRV;
	CMyStatic m_UI8BlaVmeV;
	CMyStatic m_UI8BlaImemA;
	CMyStatic m_UI8BlaVmoV;
	CMyStatic m_UI8BlaImomA;
	CMyStatic m_UI9PowerStationIOA;
	CMyStatic m_UI9EleSurAnaPRW;
	CMyStatic m_UI9EleSurAnaIRmA;
	CMyStatic m_UI9EleSurAnaVRV;
	CMyStatic m_UI9BlaVme;
	CMyStatic m_UI9BlaIme;
	CMyStatic m_UI9BlaVmo;
	CMyStatic m_UI9BlaImo;
	CMyStatic m_UI11BlaVinV;
	CMyStatic m_UI11BlaIINA;
	CMyStatic m_UI11BlaTTC;
	CMyStatic m_UI11BlaTIC;
	CMyStatic m_UI11BlaTHC;
	CMyStatic m_UI13BlaVR1;
	CMyStatic m_UI13BlaVR2;
	CMyStatic m_UI13BlaVR3;
	CMyStatic m_UI13BlaVR4;
	CMyStatic m_UI13BlaVR5;
	CMyStatic m_UI13BlaVR6;
	CMyStatic m_UI15Electrode1;
	CMyStatic m_UI15Electrode2;
	CMyStatic m_UI14DocRiseAlongEdge;
	CMyStatic m_UI14DocDropEdge;
	CMyStatic m_UI14Doc101KPositiveVV;
	CMyStatic m_UI14Doc5500PositiveVV;
	CMyStatic m_UI14Doc8mA500PositiveVV;
	CMyStatic m_UI14Doc1V100PositiveVV;
	CMyStatic m_UI14Doc2mA100PositiveVV;
	CMyStatic m_UI14Doc10V1KNegativeV;
	CMyStatic m_UI14Doc5V500NegativeV;
	CMyStatic m_UI14Doc8mA500NegativeV;
	CMyStatic m_UI14Doc1V100NegativeV;
	CMyStatic m_UI14Doc2mA100NegativeV;
	CMyStatic m_UIstr14Bla10V1KCurrenyFeedBackV;
	CMyStatic m_UI14Bla5V500CurrenyFeedBackV;
	CMyStatic m_UI14Bla8mA500CurrenyFeedBackV;
	CMyStatic m_UI14Bla1V100CurrenyFeedBackV;
	CMyStatic m_UI14Bla2mA100CurrenyFeedBackV;

	void CloseCheck();
	void CloseDlgCheck();
	CMyStatic m_BFlAmainBoardbeforeAgingRet;
	CMyStatic m_writeExeclState;
	CMyStatic m_NEMCalUI;
	CMyStatic m_NEMACCShortCirUI;
	CMyStatic m_NEMACCFallOffUI;
	CMyStatic m_NEMACCCalUI;
	CComboBox m_QCPersonMBAfter;
	CMyStatic m_noteJiDianQiQieHuan;
	void initParameter();
	float f7SurAnaPW;
	float f7SurAnaImA;
	float f7SurAnaVV;
	GetStimAcc m_getStimAcc;
	afx_msg void OnBnClickedBtnRfLowTestSwitch();
	afx_msg void OnBnClickedBtnRfHighTestSwitch();

	//0723
	void AddIniValueIfExists(LPCTSTR section, LPCTSTR key, int nIDCombo, LPCTSTR iniPath);
	void GetImpedanceAccuracy13();
	void GetNEMCalACC();
	void GetStimAccuracy14();
	void UpdateDownExe3UI();
	void UpdateCaliRFIV7UI();
	void UpdatelowRFTest8UI();
	void UpdateHighRFTest9UI();
	void UpdateImpedanceAccuracy13UI();
	void UpdateNEMCalACCUI();
	void UpdateStimAccuracy14UI();
	void UpdateElectrodeTemp15UI();

	//0728
	void UpdateBlaParam5();
	afx_msg void OnBnClickedButtonParam5();

	//0731
	void UpdateRiseTimeUI();
	void UpdateFallTimeUI();
	void UpdateBla10V1K2UI();
	void UpdateBla5V500UI();
	void UpdateBla8ma500UI();
	void UpdateBla1v100UI();
	void UpdateBla2ma100UI();
	afx_msg void OnBnClickedButtonTest();

	//0801
	void UpdateNEMCalUI();
	void UpdateNEMAccCalUI();
	void UpdateNEMFallOffUI();
	void UpdateNEMShorCirUI();

	//NEM标定判别用
	CString strCAMCal;
	CString strCAMCal1;
	CString strCAMCal2;
	CString strCAMCal3;	

	afx_msg void OnCbnSelchangeDsoChannel();
	CComboBox m_DSOChannels;
	afx_msg void OnBnClickedButton8ma();
	afx_msg void OnBnClickedButton500again();
	afx_msg void OnBnClickedButton2ma();
	afx_msg void OnBnClickedButtonUpdate();
	CEdit m_updateVoltage;
	CEdit m_updateCurrent;
	CComboBox m_eleType;
	afx_msg void OnCbnSelchangeComboEletype();
	std::unique_ptr<KingPoElecAnalyzer> m_kingpoAnalyzer;
};
