#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

// 音频录制回调：接收 PCM 16kHz 16bit mono 数据
using AudioDataCallback = std::function<void(const std::vector<int16_t>& samples)>;
// VAD 静音检测后的完整语音段回调
using SpeechSegmentCallback = std::function<void(const std::vector<float>& samples)>;

class AudioCapture
{
public:
    AudioCapture();
    ~AudioCapture();

    // 初始化音频设备（16kHz, 16bit, mono）
    bool Initialize(int sampleRate = 16000, int bitsPerSample = 16, int channels = 1);
    void Shutdown();

    // 开始/停止录音
    bool StartCapture();
    void StopCapture();
    bool IsCapturing() const { return m_bCapturing; }

    // 设置音频数据回调（原始PCM数据）
    void SetAudioDataCallback(AudioDataCallback callback);
    // 设置语音段回调（经过VAD的完整语音段，float格式 [-1.0, 1.0]）
    void SetSpeechSegmentCallback(SpeechSegmentCallback callback);

    // VAD 参数
    void SetSilenceThreshold(float threshold) { m_fSilenceThreshold = threshold; }
    void SetSilenceDurationMs(int ms) { m_nSilenceDurationMs = ms; }
    void SetMinSpeechDurationMs(int ms) { m_nMinSpeechDurationMs = ms; }

private:
    static void CALLBACK WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
                                     DWORD_PTR dwParam1, DWORD_PTR dwParam2);
    void ProcessBuffer(WAVEHDR* pHeader);
    void VADProcess(const std::vector<int16_t>& samples);
    float CalculateRMS(const int16_t* data, int count);
    std::vector<float> ConvertToFloat(const std::vector<int16_t>& samples);

    HWAVEIN              m_hWaveIn;
    WAVEFORMATEX         m_wfx;
    std::atomic<bool>    m_bCapturing;
    bool                 m_bInitialized;

    static const int     NUM_BUFFERS = 4;
    static const int     BUFFER_DURATION_MS = 100; // 每个缓冲区100ms
    WAVEHDR              m_waveHeaders[NUM_BUFFERS];
    std::vector<BYTE>    m_buffers[NUM_BUFFERS];

    AudioDataCallback    m_audioCallback;
    SpeechSegmentCallback m_speechCallback;

    // VAD 状态
    std::mutex           m_vadMutex;
    std::vector<int16_t> m_speechBuffer;     // 当前语音段缓存
    bool                 m_bInSpeech;         // 是否在语音段中
    int                  m_nSilenceFrames;    // 连续静音帧数
    float                m_fSilenceThreshold; // 静音阈值 (RMS)
    int                  m_nSilenceDurationMs;   // 语音结束的静音持续时间
    int                  m_nMinSpeechDurationMs;  // 最短语音段时长
    int                  m_nSampleRate;
};
