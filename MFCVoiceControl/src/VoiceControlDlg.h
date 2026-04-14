#pragma once

#include "Resource.h"
#include "AudioCapture.h"
#include "SherpaASR.h"
#include "SherpaTTS.h"
#include "CommandParser.h"

class CVoiceControlDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CVoiceControlDlg)

public:
    CVoiceControlDlg(CWnd* pParent = nullptr);
    virtual ~CVoiceControlDlg();

    enum { IDD = IDD_VOICECONTROL_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();

    DECLARE_MESSAGE_MAP()

    // 按钮事件处理
    afx_msg void OnBnClickedStartListen();
    afx_msg void OnBnClickedStopListen();
    afx_msg void OnBnClickedConfirm();
    afx_msg void OnBnClickedReset();
    afx_msg void OnBnClickedApplyMode();

    // 自定义消息处理
    afx_msg LRESULT OnASRResult(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTTSDone(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnVoiceStatus(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnLogMessage(WPARAM wParam, LPARAM lParam);

    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
    // 初始化各模块
    bool InitializeASR();
    bool InitializeTTS();
    bool InitializeAudio();

    // 处理ASR识别结果
    void ProcessASRResult(const std::string& text);

    // 执行命令
    void ExecuteCommand(const CommandParser::Command& cmd);

    // 设置参数值到对应的输入框
    void SetParameterValue(const std::string& param, const std::string& value);

    // 更新状态显示
    void UpdateStatus(const std::string& status);

    // 添加日志
    void AddLog(const std::string& message);

    // 语音复述（在工作线程中执行TTS）
    void SpeakResponse(const std::string& text);

    // 获取模型目录路径
    std::string GetModelPath();

    // 控件变量
    CEdit       m_editASRResult;
    CEdit       m_editTemperature;
    CEdit       m_editHumidity;
    CEdit       m_editPressure;
    CComboBox   m_comboMode;
    CListBox    m_listLog;
    CStatic     m_staticStatus;
    CButton     m_btnStartListen;
    CButton     m_btnStopListen;
    CButton     m_btnConfirm;

    // 核心模块
    AudioCapture    m_audioCapture;
    SherpaASR       m_asrEngine;
    SherpaTTS       m_ttsEngine;
    CommandParser   m_cmdParser;

    // 状态
    std::atomic<bool>   m_bListening;
    std::thread         m_asrThread;
    std::thread         m_ttsThread;
    std::mutex          m_asrMutex;
    std::mutex          m_ttsMutex;

    // 参数名到控件ID的映射
    std::map<std::string, UINT> m_paramToEditID;
};
