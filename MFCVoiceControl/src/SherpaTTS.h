#pragma once

#include <string>
#include <vector>
#include <functional>

// TTS 合成完成回调 (PCM float 数据)
using TTSCompleteCallback = std::function<void(const std::vector<float>& audio, int sampleRate)>;

// TTS 引擎封装 - 基于 sherpa-onnx 的 VITS/Piper 模型
class SherpaTTS
{
public:
    SherpaTTS();
    ~SherpaTTS();

    struct Config {
        std::string modelFile;      // VITS ONNX 模型文件路径
        std::string lexiconFile;    // 词典文件路径
        std::string tokensFile;     // tokens 文件路径
        std::string dataDirOrDict;  // espeak-ng-data 目录或词典目录
        int         numThreads = 2;
        float       speed = 1.0f;   // 语速 (0.5-2.0)
        int         speakerId = 0;  // 说话人ID（多说话人模型）
        std::string provider = "cpu";
    };

    bool Initialize(const Config& config);
    void Shutdown();
    bool IsInitialized() const { return m_bInitialized; }

    // 合成语音: 返回 PCM float 数据
    struct AudioResult {
        std::vector<float> samples;
        int sampleRate;
    };

    AudioResult Synthesize(const std::string& text);

    // 合成并直接播放
    bool SynthesizeAndPlay(const std::string& text);

    // 设置合成完成回调
    void SetCompleteCallback(TTSCompleteCallback callback) { m_completeCallback = callback; }

    // 语速调节
    void SetSpeed(float speed) { m_fSpeed = speed; }
    float GetSpeed() const { return m_fSpeed; }

private:
    // 使用 Windows waveOut 播放 PCM 数据
    bool PlayAudio(const std::vector<float>& samples, int sampleRate);
    // 将 float 转换为 int16 PCM
    std::vector<int16_t> ConvertToInt16(const std::vector<float>& samples);

    bool                m_bInitialized;
    Config              m_config;
    float               m_fSpeed;
    TTSCompleteCallback m_completeCallback;

    void*               m_pTts;  // SherpaOnnxOfflineTts*
};
