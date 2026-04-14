#include "stdafx.h"
#include "SherpaASR.h"

// ================================================================
//  sherpa-onnx C API 类型和函数声明
//  实际项目中请直接包含:
//    #include "sherpa-onnx/c-api/c-api.h"
//  以下是关键类型的简化声明，用于展示集成方式
// ================================================================

#ifdef USE_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#else

// 简化的 API 声明（展示用途，实际请使用官方头文件）
extern "C" {

struct SherpaOnnxOfflineRecognizerConfig;
struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineStream;
struct SherpaOnnxOfflineRecognizerResult;

typedef SherpaOnnxOfflineRecognizer* (*PFN_CreateOfflineRecognizer)(
    const SherpaOnnxOfflineRecognizerConfig*);
typedef void (*PFN_DestroyOfflineRecognizer)(const SherpaOnnxOfflineRecognizer*);
typedef SherpaOnnxOfflineStream* (*PFN_CreateOfflineStream)(
    const SherpaOnnxOfflineRecognizer*);
typedef void (*PFN_DestroyOfflineStream)(const SherpaOnnxOfflineStream*);
typedef void (*PFN_AcceptWaveformOffline)(const SherpaOnnxOfflineStream*,
    int32_t, const float*, int32_t);
typedef void (*PFN_DecodeOfflineStream)(const SherpaOnnxOfflineRecognizer*,
    const SherpaOnnxOfflineStream*);
typedef const SherpaOnnxOfflineRecognizerResult* (*PFN_GetOfflineStreamResult)(
    const SherpaOnnxOfflineStream*);
typedef void (*PFN_DestroyOfflineRecognizerResult)(
    const SherpaOnnxOfflineRecognizerResult*);

} // extern "C"
#endif

// ================================================================
//  SherpaASR 实现 - 推荐方案
// ================================================================

SherpaASR::SherpaASR()
    : m_bInitialized(false)
    , m_pRecognizer(nullptr)
{
}

SherpaASR::~SherpaASR()
{
    Shutdown();
}

bool SherpaASR::Initialize(const Config& config)
{
    if (m_bInitialized) return true;
    m_config = config;

    // ============================================================
    //  以下为实际的 sherpa-onnx 初始化代码
    //  请确保已正确链接 sherpa-onnx-c-api.lib
    // ============================================================

#ifdef USE_SHERPA_ONNX
    // 构建模型文件路径
    std::string modelPath = config.modelDir + "/" + config.modelFile;
    std::string tokensPath = config.modelDir + "/" + config.tokensFile;

    // 创建 SenseVoice 识别器配置
    SherpaOnnxOfflineRecognizerConfig recognizerConfig;
    memset(&recognizerConfig, 0, sizeof(recognizerConfig));

    // SenseVoice 模型配置
    recognizerConfig.model_config.sense_voice.model = modelPath.c_str();
    recognizerConfig.model_config.sense_voice.language = config.language.c_str();
    recognizerConfig.model_config.sense_voice.use_itn = 1; // 启用逆文本归一化

    recognizerConfig.model_config.tokens = tokensPath.c_str();
    recognizerConfig.model_config.num_threads = config.numThreads;
    recognizerConfig.model_config.provider = config.provider.c_str();

    // 创建识别器
    m_pRecognizer = SherpaOnnxCreateOfflineRecognizer(&recognizerConfig);
    if (!m_pRecognizer) {
        return false;
    }

    m_bInitialized = true;
    return true;

#else
    // 演示模式：模拟初始化成功
    m_bInitialized = true;
    return true;
#endif
}

void SherpaASR::Shutdown()
{
#ifdef USE_SHERPA_ONNX
    if (m_pRecognizer) {
        SherpaOnnxDestroyOfflineRecognizer(
            reinterpret_cast<const SherpaOnnxOfflineRecognizer*>(m_pRecognizer));
        m_pRecognizer = nullptr;
    }
#endif
    m_bInitialized = false;
}

std::string SherpaASR::Recognize(const std::vector<float>& audioData, int sampleRate)
{
    if (!m_bInitialized || audioData.empty()) return "";

#ifdef USE_SHERPA_ONNX
    auto* recognizer = reinterpret_cast<const SherpaOnnxOfflineRecognizer*>(m_pRecognizer);

    // 创建流
    auto* stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) return "";

    // 输入音频
    SherpaOnnxAcceptWaveformOffline(stream, sampleRate,
                                     audioData.data(),
                                     static_cast<int32_t>(audioData.size()));

    // 解码
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    // 获取结果
    auto* result = SherpaOnnxGetOfflineStreamResult(stream);
    std::string text;
    if (result && result->text) {
        text = result->text;
    }

    // 清理
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);

    if (m_resultCallback && !text.empty()) {
        m_resultCallback(text);
    }

    return text;

#else
    // 演示模式：返回模拟结果
    return "";
#endif
}

// ================================================================
//  WhisperASR 实现 - 备选方案
// ================================================================

WhisperASR::WhisperASR()
    : m_bInitialized(false)
    , m_pContext(nullptr)
{
}

WhisperASR::~WhisperASR()
{
    Shutdown();
}

bool WhisperASR::Initialize(const Config& config)
{
    if (m_bInitialized) return true;
    m_config = config;

    // ============================================================
    //  以下为 whisper.cpp 的初始化代码
    //  请确保已编译 whisper.cpp 并链接 whisper.lib
    // ============================================================

#ifdef USE_WHISPER_CPP
    // #include "whisper.h"
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false; // x86 Pad 通常无GPU

    m_pContext = whisper_init_from_file_with_params(
        config.modelPath.c_str(), cparams);
    if (!m_pContext) return false;

    m_bInitialized = true;
    return true;
#else
    m_bInitialized = true;
    return true;
#endif
}

void WhisperASR::Shutdown()
{
#ifdef USE_WHISPER_CPP
    if (m_pContext) {
        whisper_free(reinterpret_cast<struct whisper_context*>(m_pContext));
        m_pContext = nullptr;
    }
#endif
    m_bInitialized = false;
}

std::string WhisperASR::Recognize(const std::vector<float>& audioData, int sampleRate)
{
    if (!m_bInitialized || audioData.empty()) return "";

#ifdef USE_WHISPER_CPP
    auto* ctx = reinterpret_cast<struct whisper_context*>(m_pContext);

    struct whisper_full_params params = whisper_full_default_params(
        WHISPER_SAMPLING_GREEDY);
    params.language = m_config.language.c_str();
    params.translate = m_config.translate;
    params.n_threads = m_config.numThreads;
    params.print_progress = false;
    params.print_timestamps = false;

    if (whisper_full(ctx, params, audioData.data(),
                     static_cast<int>(audioData.size())) != 0) {
        return "";
    }

    int numSegments = whisper_full_n_segments(ctx);
    std::string result;
    for (int i = 0; i < numSegments; i++) {
        result += whisper_full_get_segment_text(ctx, i);
    }

    return result;
#else
    return "";
#endif
}
