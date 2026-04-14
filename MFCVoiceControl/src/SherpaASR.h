#pragma once

#include <string>
#include <vector>
#include <functional>

// sherpa-onnx C API 前向声明
// 实际使用时包含: #include "sherpa-onnx/c-api/c-api.h"
// 这里我们使用动态加载方式，避免编译时依赖

// ASR 识别结果回调
using ASRResultCallback = std::function<void(const std::string& text)>;

// ASR 引擎封装 - 基于 sherpa-onnx 的 SenseVoice 模型
class SherpaASR
{
public:
    SherpaASR();
    ~SherpaASR();

    struct Config {
        std::string modelDir;           // 模型目录路径
        std::string modelFile;          // 模型文件名 (如 "model.int8.onnx")
        std::string tokensFile;         // tokens文件名
        std::string language;           // 语言: "zh", "en", "ja", "ko", "yue"
        int         numThreads = 2;     // 推理线程数
        bool        useGPU = false;     // 是否使用GPU（需DirectML支持）
        std::string provider = "cpu";   // "cpu" 或 "directml"
    };

    bool Initialize(const Config& config);
    void Shutdown();
    bool IsInitialized() const { return m_bInitialized; }

    // 离线识别：传入完整语音段 (float, 16kHz, mono, [-1.0, 1.0])
    std::string Recognize(const std::vector<float>& audioData, int sampleRate = 16000);

    // 设置识别完成回调
    void SetResultCallback(ASRResultCallback callback) { m_resultCallback = callback; }

private:
    bool              m_bInitialized;
    Config            m_config;
    ASRResultCallback m_resultCallback;

    // sherpa-onnx 句柄（使用 void* 避免头文件依赖）
    void*             m_pRecognizer;
};

// ============================================================
// 备选方案：基于 whisper.cpp 的 ASR 引擎
// ============================================================
class WhisperASR
{
public:
    WhisperASR();
    ~WhisperASR();

    struct Config {
        std::string modelPath;          // ggml 模型文件路径
        std::string language;           // "zh", "en" 等
        int         numThreads = 4;
        bool        translate = false;  // 是否翻译为英文
    };

    bool Initialize(const Config& config);
    void Shutdown();
    bool IsInitialized() const { return m_bInitialized; }

    std::string Recognize(const std::vector<float>& audioData, int sampleRate = 16000);

private:
    bool    m_bInitialized;
    Config  m_config;
    void*   m_pContext;   // whisper_context*
};
