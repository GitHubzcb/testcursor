#include "stdafx.h"
#include "VoiceControlDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CVoiceControlDlg, CDialogEx)

CVoiceControlDlg::CVoiceControlDlg(CWnd* pParent)
    : CDialogEx(IDD_VOICECONTROL_DIALOG, pParent)
    , m_bListening(false)
{
}

CVoiceControlDlg::~CVoiceControlDlg()
{
    m_bListening = false;
    m_audioCapture.Shutdown();
    m_asrEngine.Shutdown();
    m_ttsEngine.Shutdown();

    if (m_asrThread.joinable()) m_asrThread.join();
    if (m_ttsThread.joinable()) m_ttsThread.join();
}

void CVoiceControlDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_ASR_RESULT,   m_editASRResult);
    DDX_Control(pDX, IDC_EDIT_TEMPERATURE,   m_editTemperature);
    DDX_Control(pDX, IDC_EDIT_HUMIDITY,      m_editHumidity);
    DDX_Control(pDX, IDC_EDIT_PRESSURE,      m_editPressure);
    DDX_Control(pDX, IDC_COMBO_MODE,         m_comboMode);
    DDX_Control(pDX, IDC_LIST_LOG,           m_listLog);
    DDX_Control(pDX, IDC_STATIC_STATUS,      m_staticStatus);
    DDX_Control(pDX, IDC_BTN_START_LISTEN,   m_btnStartListen);
    DDX_Control(pDX, IDC_BTN_STOP_LISTEN,    m_btnStopListen);
    DDX_Control(pDX, IDC_BTN_CONFIRM,        m_btnConfirm);
}

BEGIN_MESSAGE_MAP(CVoiceControlDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START_LISTEN,  &CVoiceControlDlg::OnBnClickedStartListen)
    ON_BN_CLICKED(IDC_BTN_STOP_LISTEN,   &CVoiceControlDlg::OnBnClickedStopListen)
    ON_BN_CLICKED(IDC_BTN_CONFIRM,       &CVoiceControlDlg::OnBnClickedConfirm)
    ON_BN_CLICKED(IDC_BTN_RESET,         &CVoiceControlDlg::OnBnClickedReset)
    ON_BN_CLICKED(IDC_BTN_APPLY_MODE,    &CVoiceControlDlg::OnBnClickedApplyMode)
    ON_MESSAGE(WM_ASR_RESULT,            &CVoiceControlDlg::OnASRResult)
    ON_MESSAGE(WM_TTS_DONE,              &CVoiceControlDlg::OnTTSDone)
    ON_MESSAGE(WM_VOICE_STATUS,          &CVoiceControlDlg::OnVoiceStatus)
    ON_MESSAGE(WM_LOG_MESSAGE,           &CVoiceControlDlg::OnLogMessage)
    ON_WM_DESTROY()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// ================================================================
//  对话框初始化
// ================================================================

BOOL CVoiceControlDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowText(_T("MFC 语音控制系统"));

    // 参数名 -> 输入框控件ID 映射
    m_paramToEditID["温度"] = IDC_EDIT_TEMPERATURE;
    m_paramToEditID["湿度"] = IDC_EDIT_HUMIDITY;
    m_paramToEditID["压力"] = IDC_EDIT_PRESSURE;

    // 初始化模式下拉框
    m_comboMode.AddString(_T("自动"));
    m_comboMode.AddString(_T("手动"));
    m_comboMode.AddString(_T("节能"));
    m_comboMode.SetCurSel(0);

    // 初始化按钮状态
    m_btnStopListen.EnableWindow(FALSE);

    // 初始化各模块
    AddLog("系统启动中...");

    if (InitializeASR()) {
        AddLog("ASR引擎初始化成功 (SenseVoice)");
    } else {
        AddLog("ASR引擎初始化失败，请检查模型文件");
    }

    if (InitializeTTS()) {
        AddLog("TTS引擎初始化成功 (Piper/VITS)");
    } else {
        AddLog("TTS引擎初始化失败，请检查模型文件");
    }

    if (InitializeAudio()) {
        AddLog("音频设备初始化成功");
    } else {
        AddLog("音频设备初始化失败，请检查麦克风");
    }

    UpdateStatus("就绪 - 点击[开始监听]按钮启动语音控制");
    AddLog("系统就绪");

    return TRUE;
}

// ================================================================
//  模块初始化
// ================================================================

bool CVoiceControlDlg::InitializeASR()
{
    std::string modelPath = GetModelPath();

    SherpaASR::Config config;
    config.modelDir = modelPath + "\\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17";
    config.modelFile = "model.int8.onnx";
    config.tokensFile = "tokens.txt";
    config.language = "zh";
    config.numThreads = 2;
    config.provider = "cpu";

    return m_asrEngine.Initialize(config);
}

bool CVoiceControlDlg::InitializeTTS()
{
    std::string modelPath = GetModelPath();

    SherpaTTS::Config config;
    config.modelFile = modelPath + "\\vits-zh-hf-fanchen-C\\vits-zh-hf-fanchen-C.onnx";
    config.lexiconFile = modelPath + "\\vits-zh-hf-fanchen-C\\lexicon.txt";
    config.tokensFile = modelPath + "\\vits-zh-hf-fanchen-C\\tokens.txt";
    config.numThreads = 2;
    config.speed = 1.0f;
    config.provider = "cpu";

    return m_ttsEngine.Initialize(config);
}

bool CVoiceControlDlg::InitializeAudio()
{
    if (!m_audioCapture.Initialize(16000, 16, 1)) {
        return false;
    }

    // VAD 参数调优
    m_audioCapture.SetSilenceThreshold(500.0f);
    m_audioCapture.SetSilenceDurationMs(800);
    m_audioCapture.SetMinSpeechDurationMs(500);

    // 设置语音段回调 - 当VAD检测到完整语音段后触发ASR
    m_audioCapture.SetSpeechSegmentCallback(
        [this](const std::vector<float>& samples) {
            if (!m_bListening) return;

            // 在工作线程中执行ASR识别
            std::thread([this, samples]() {
                std::lock_guard<std::mutex> lock(m_asrMutex);

                std::string result = m_asrEngine.Recognize(samples, 16000);

                if (!result.empty()) {
                    // 将结果复制到堆上，通过消息传递到UI线程
                    std::string* pResult = new std::string(result);
                    PostMessage(WM_ASR_RESULT,
                               reinterpret_cast<WPARAM>(pResult), 0);
                }
            }).detach();
        });

    return true;
}

std::string CVoiceControlDlg::GetModelPath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    std::string path(exePath);
    size_t lastSlash = path.find_last_of('\\');
    if (lastSlash != std::string::npos) {
        path = path.substr(0, lastSlash);
    }
    return path + "\\models";
}

// ================================================================
//  按钮事件处理
// ================================================================

void CVoiceControlDlg::OnBnClickedStartListen()
{
    if (m_bListening) return;

    if (!m_asrEngine.IsInitialized()) {
        AfxMessageBox(_T("ASR引擎未初始化，无法开始监听"));
        return;
    }

    if (m_audioCapture.StartCapture()) {
        m_bListening = true;
        m_btnStartListen.EnableWindow(FALSE);
        m_btnStopListen.EnableWindow(TRUE);
        UpdateStatus("正在监听... 请说话");
        AddLog("开始语音监听");
    } else {
        AfxMessageBox(_T("启动麦克风失败"));
    }
}

void CVoiceControlDlg::OnBnClickedStopListen()
{
    m_bListening = false;
    m_audioCapture.StopCapture();
    m_btnStartListen.EnableWindow(TRUE);
    m_btnStopListen.EnableWindow(FALSE);
    UpdateStatus("已停止监听");
    AddLog("停止语音监听");
}

void CVoiceControlDlg::OnBnClickedConfirm()
{
    CString strTemp, strHumid, strPress;
    m_editTemperature.GetWindowText(strTemp);
    m_editHumidity.GetWindowText(strHumid);
    m_editPressure.GetWindowText(strPress);

    CString msg;
    msg.Format(_T("参数已确认:\n温度: %s°C\n湿度: %s%%\n压力: %skPa"),
               strTemp.GetString(), strHumid.GetString(), strPress.GetString());
    AddLog(std::string(CT2A(msg)));

    // 实际应用中，这里发送参数到底层设备/控制器
    // SendCommandToDevice(strTemp, strHumid, strPress);
}

void CVoiceControlDlg::OnBnClickedReset()
{
    m_editTemperature.SetWindowText(_T(""));
    m_editHumidity.SetWindowText(_T(""));
    m_editPressure.SetWindowText(_T(""));
    m_comboMode.SetCurSel(0);
    AddLog("参数已重置");
}

void CVoiceControlDlg::OnBnClickedApplyMode()
{
    int sel = m_comboMode.GetCurSel();
    if (sel != CB_ERR) {
        CString modeName;
        m_comboMode.GetLBText(sel, modeName);
        AddLog("模式已切换为: " + std::string(CT2A(modeName)));
    }
}

// ================================================================
//  自定义消息处理
// ================================================================

LRESULT CVoiceControlDlg::OnASRResult(WPARAM wParam, LPARAM lParam)
{
    std::string* pResult = reinterpret_cast<std::string*>(wParam);
    if (!pResult) return 0;

    std::string text = *pResult;
    delete pResult;

    ProcessASRResult(text);
    return 0;
}

LRESULT CVoiceControlDlg::OnTTSDone(WPARAM wParam, LPARAM lParam)
{
    UpdateStatus("正在监听... 请说话");
    return 0;
}

LRESULT CVoiceControlDlg::OnVoiceStatus(WPARAM wParam, LPARAM lParam)
{
    std::string* pStatus = reinterpret_cast<std::string*>(wParam);
    if (pStatus) {
        UpdateStatus(*pStatus);
        delete pStatus;
    }
    return 0;
}

LRESULT CVoiceControlDlg::OnLogMessage(WPARAM wParam, LPARAM lParam)
{
    std::string* pMsg = reinterpret_cast<std::string*>(wParam);
    if (pMsg) {
        AddLog(*pMsg);
        delete pMsg;
    }
    return 0;
}

// ================================================================
//  核心逻辑：处理ASR结果 → 解析命令 → 执行操作 → 语音复述
// ================================================================

void CVoiceControlDlg::ProcessASRResult(const std::string& text)
{
    // 1. 显示识别结果
    CString csText(text.c_str());
    m_editASRResult.SetWindowText(csText);
    AddLog("识别结果: " + text);

    // 2. 解析命令
    CommandParser::Command cmd = m_cmdParser.Parse(text);

    if (cmd.isValid) {
        // 3. 执行命令
        ExecuteCommand(cmd);

        // 4. 语音复述（异步）
        if (!cmd.responseText.empty()) {
            SpeakResponse(cmd.responseText);
        }
    } else {
        AddLog("未识别到有效命令: " + text);
        SpeakResponse("抱歉，未能识别您的指令");
    }
}

void CVoiceControlDlg::ExecuteCommand(const CommandParser::Command& cmd)
{
    switch (cmd.type) {
    case CommandParser::Command::CMD_SET_VALUE:
        // 设置参数值到输入框
        SetParameterValue(cmd.parameter, cmd.value);
        AddLog("执行: 设置" + cmd.parameter + " = " + cmd.value);
        break;

    case CommandParser::Command::CMD_CONFIRM:
        // 模拟点击确定按钮
        OnBnClickedConfirm();
        AddLog("执行: 确认操作");
        break;

    case CommandParser::Command::CMD_RESET:
        OnBnClickedReset();
        AddLog("执行: 重置操作");
        break;

    case CommandParser::Command::CMD_SELECT_MODE: {
        // 在下拉框中查找并选择对应模式
        CString targetMode(cmd.mode.c_str());
        int count = m_comboMode.GetCount();
        for (int i = 0; i < count; i++) {
            CString itemText;
            m_comboMode.GetLBText(i, itemText);
            if (itemText == targetMode) {
                m_comboMode.SetCurSel(i);
                OnBnClickedApplyMode();
                break;
            }
        }
        AddLog("执行: 选择模式 = " + cmd.mode);
        break;
    }

    case CommandParser::Command::CMD_START_LISTEN:
        OnBnClickedStartListen();
        break;

    case CommandParser::Command::CMD_STOP_LISTEN:
        OnBnClickedStopListen();
        break;

    default:
        break;
    }
}

void CVoiceControlDlg::SetParameterValue(const std::string& param,
                                          const std::string& value)
{
    auto it = m_paramToEditID.find(param);
    if (it == m_paramToEditID.end()) {
        AddLog("未找到参数控件: " + param);
        return;
    }

    UINT editID = it->second;
    CWnd* pEdit = GetDlgItem(editID);
    if (pEdit) {
        CString csValue(value.c_str());
        pEdit->SetWindowText(csValue);

        // 高亮显示（闪烁效果）
        pEdit->SetFocus();
        AddLog("已设置 " + param + " = " + value);
    }
}

// ================================================================
//  TTS 语音复述
// ================================================================

void CVoiceControlDlg::SpeakResponse(const std::string& text)
{
    if (!m_ttsEngine.IsInitialized()) return;

    UpdateStatus("语音回复: " + text);

    // 在工作线程中执行TTS合成和播放
    std::thread([this, text]() {
        std::lock_guard<std::mutex> lock(m_ttsMutex);
        m_ttsEngine.SynthesizeAndPlay(text);
        PostMessage(WM_TTS_DONE, 0, 0);
    }).detach();
}

// ================================================================
//  辅助方法
// ================================================================

void CVoiceControlDlg::UpdateStatus(const std::string& status)
{
    if (!::IsWindow(m_staticStatus.GetSafeHwnd())) return;
    CString csStatus(status.c_str());
    m_staticStatus.SetWindowText(csStatus);
}

void CVoiceControlDlg::AddLog(const std::string& message)
{
    if (!::IsWindow(m_listLog.GetSafeHwnd())) return;

    // 获取当前时间
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeStr[32];
    sprintf_s(timeStr, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    std::string logEntry = std::string(timeStr) + message;
    CString csLog(logEntry.c_str());

    m_listLog.AddString(csLog);
    // 滚动到最新条目
    m_listLog.SetTopIndex(m_listLog.GetCount() - 1);
}

void CVoiceControlDlg::OnDestroy()
{
    m_bListening = false;
    m_audioCapture.Shutdown();
    m_asrEngine.Shutdown();
    m_ttsEngine.Shutdown();
    CDialogEx::OnDestroy();
}

void CVoiceControlDlg::OnOK()
{
    // 屏蔽 Enter 键关闭对话框
}

void CVoiceControlDlg::OnCancel()
{
    if (AfxMessageBox(_T("确定退出语音控制系统？"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
        CDialogEx::OnCancel();
    }
}

void CVoiceControlDlg::OnTimer(UINT_PTR nIDEvent)
{
    CDialogEx::OnTimer(nIDEvent);
}
