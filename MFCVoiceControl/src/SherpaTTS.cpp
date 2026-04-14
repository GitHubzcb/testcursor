#include "stdafx.h"
#include "SherpaTTS.h"

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// ================================================================
//  sherpa-onnx TTS C API
//  实际项目中请直接包含 "sherpa-onnx/c-api/c-api.h"
// ================================================================

#ifdef USE_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif

SherpaTTS::SherpaTTS()
    : m_bInitialized(false)
    , m_fSpeed(1.0f)
    , m_pTts(nullptr)
{
}

SherpaTTS::~SherpaTTS()
{
    Shutdown();
}

bool SherpaTTS::Initialize(const Config& config)
{
    if (m_bInitialized) return true;
    m_config = config;
    m_fSpeed = config.speed;

#ifdef USE_SHERPA_ONNX
    SherpaOnnxOfflineTtsConfig ttsConfig;
    memset(&ttsConfig, 0, sizeof(ttsConfig));

    // VITS 模型配置
    ttsConfig.model.vits.model = config.modelFile.c_str();
    ttsConfig.model.vits.lexicon = config.lexiconFile.c_str();
    ttsConfig.model.vits.tokens = config.tokensFile.c_str();
    ttsConfig.model.vits.data_dir = config.dataDirOrDict.c_str();
    ttsConfig.model.vits.length_scale = 1.0f / config.speed;

    ttsConfig.model.num_threads = config.numThreads;
    ttsConfig.model.provider = config.provider.c_str();

    m_pTts = SherpaOnnxCreateOfflineTts(&ttsConfig);
    if (!m_pTts) return false;

    m_bInitialized = true;
    return true;

#else
    m_bInitialized = true;
    return true;
#endif
}

void SherpaTTS::Shutdown()
{
#ifdef USE_SHERPA_ONNX
    if (m_pTts) {
        SherpaOnnxDestroyOfflineTts(
            reinterpret_cast<SherpaOnnxOfflineTts*>(m_pTts));
        m_pTts = nullptr;
    }
#endif
    m_bInitialized = false;
}

SherpaTTS::AudioResult SherpaTTS::Synthesize(const std::string& text)
{
    AudioResult result;
    result.sampleRate = 22050; // Piper/VITS 默认采样率

    if (!m_bInitialized || text.empty()) return result;

#ifdef USE_SHERPA_ONNX
    auto* tts = reinterpret_cast<SherpaOnnxOfflineTts*>(m_pTts);

    const SherpaOnnxGeneratedAudio* audio =
        SherpaOnnxOfflineTtsGenerate(tts, text.c_str(),
                                      m_config.speakerId,
                                      m_fSpeed);
    if (audio) {
        result.sampleRate = audio->sample_rate;
        result.samples.assign(audio->samples, audio->samples + audio->n);
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    }

    if (m_completeCallback && !result.samples.empty()) {
        m_completeCallback(result.samples, result.sampleRate);
    }
#endif

    return result;
}

bool SherpaTTS::SynthesizeAndPlay(const std::string& text)
{
    auto result = Synthesize(text);
    if (result.samples.empty()) return false;
    return PlayAudio(result.samples, result.sampleRate);
}

bool SherpaTTS::PlayAudio(const std::vector<float>& samples, int sampleRate)
{
    if (samples.empty()) return false;

    auto pcm16 = ConvertToInt16(samples);

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hWaveOut = nullptr;
    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx,
                                   0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) return false;

    WAVEHDR header = {};
    header.lpData = reinterpret_cast<LPSTR>(pcm16.data());
    header.dwBufferLength = static_cast<DWORD>(pcm16.size() * sizeof(int16_t));

    waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
    waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

    // 等待播放完成
    while (!(header.dwFlags & WHDR_DONE)) {
        Sleep(10);
    }

    waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
    waveOutClose(hWaveOut);

    return true;
}

std::vector<int16_t> SherpaTTS::ConvertToInt16(const std::vector<float>& samples)
{
    std::vector<int16_t> result(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float val = samples[i];
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        result[i] = static_cast<int16_t>(val * 32767.0f);
    }
    return result;
}
