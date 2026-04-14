#pragma once

// 对话框资源 ID
#define IDD_VOICECONTROL_DIALOG     102
#define IDR_MAINFRAME               128

// 控件 ID - 语音控制区域
#define IDC_BTN_START_LISTEN        1001   // 开始语音监听
#define IDC_BTN_STOP_LISTEN         1002   // 停止语音监听
#define IDC_STATIC_STATUS           1003   // 状态显示
#define IDC_EDIT_ASR_RESULT         1004   // ASR识别结果显示
#define IDC_LIST_LOG                1005   // 操作日志列表

// 控件 ID - 参数设置区域（示例：温度控制）
#define IDC_STATIC_TEMP_LABEL       1010   // "温度" 标签
#define IDC_EDIT_TEMPERATURE        1011   // 温度输入框
#define IDC_STATIC_TEMP_UNIT        1012   // "°C" 单位标签
#define IDC_BTN_CONFIRM             1013   // 确定按钮
#define IDC_BTN_RESET               1014   // 重置按钮

// 控件 ID - 参数设置区域（示例：湿度控制）
#define IDC_STATIC_HUMID_LABEL      1020   // "湿度" 标签
#define IDC_EDIT_HUMIDITY           1021   // 湿度输入框
#define IDC_STATIC_HUMID_UNIT       1022   // "%" 单位标签

// 控件 ID - 参数设置区域（示例：压力控制）
#define IDC_STATIC_PRESS_LABEL      1030   // "压力" 标签
#define IDC_EDIT_PRESSURE           1031   // 压力输入框
#define IDC_STATIC_PRESS_UNIT       1032   // "kPa" 单位标签

// 控件 ID - 模式选择
#define IDC_COMBO_MODE              1040   // 模式选择下拉框
#define IDC_BTN_APPLY_MODE          1041   // 应用模式按钮

// 自定义消息
#define WM_ASR_RESULT               (WM_USER + 100)  // ASR识别完成
#define WM_TTS_DONE                 (WM_USER + 101)  // TTS播放完成
#define WM_VOICE_STATUS             (WM_USER + 102)  // 语音状态更新
#define WM_LOG_MESSAGE              (WM_USER + 103)  // 日志消息
