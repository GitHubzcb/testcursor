#include "stdafx.h"
#include "AudioCapture.h"

AudioCapture::AudioCapture()
    : m_hWaveIn(nullptr)
    , m_bCapturing(false)
    , m_bInitialized(false)
    , m_bInSpeech(false)
    , m_nSilenceFrames(0)
    , m_fSilenceThreshold(500.0f)
    , m_nSilenceDurationMs(800)
    , m_nMinSpeechDurationMs(300)
    , m_nSampleRate(16000)
{
    ZeroMemory(&m_wfx, sizeof(m_wfx));
    ZeroMemory(m_waveHeaders, sizeof(m_waveHeaders));
}

AudioCapture::~AudioCapture()
{
    Shutdown();
}

bool AudioCapture::Initialize(int sampleRate, int bitsPerSample, int channels)
{
    if (m_bInitialized) return true;

    m_nSampleRate = sampleRate;

    m_wfx.wFormatTag = WAVE_FORMAT_PCM;
    m_wfx.nChannels = channels;
    m_wfx.nSamplesPerSec = sampleRate;
    m_wfx.wBitsPerSample = bitsPerSample;
    m_wfx.nBlockAlign = (channels * bitsPerSample) / 8;
    m_wfx.nAvgBytesPerSec = sampleRate * m_wfx.nBlockAlign;
    m_wfx.cbSize = 0;

    MMRESULT result = waveInOpen(&m_hWaveIn, WAVE_MAPPER, &m_wfx,
                                  (DWORD_PTR)WaveInProc, (DWORD_PTR)this,
                                  CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        return false;
    }

    int bufferSize = (sampleRate * m_wfx.nBlockAlign * BUFFER_DURATION_MS) / 1000;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i].resize(bufferSize, 0);
        m_waveHeaders[i].lpData = reinterpret_cast<LPSTR>(m_buffers[i].data());
        m_waveHeaders[i].dwBufferLength = bufferSize;
        m_waveHeaders[i].dwFlags = 0;

        waveInPrepareHeader(m_hWaveIn, &m_waveHeaders[i], sizeof(WAVEHDR));
        waveInAddBuffer(m_hWaveIn, &m_waveHeaders[i], sizeof(WAVEHDR));
    }

    m_bInitialized = true;
    return true;
}

void AudioCapture::Shutdown()
{
    StopCapture();
    if (m_hWaveIn) {
        waveInReset(m_hWaveIn);
        for (int i = 0; i < NUM_BUFFERS; i++) {
            waveInUnprepareHeader(m_hWaveIn, &m_waveHeaders[i], sizeof(WAVEHDR));
        }
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
    }
    m_bInitialized = false;
}

bool AudioCapture::StartCapture()
{
    if (!m_bInitialized || m_bCapturing) return false;

    {
        std::lock_guard<std::mutex> lock(m_vadMutex);
        m_speechBuffer.clear();
        m_bInSpeech = false;
        m_nSilenceFrames = 0;
    }

    MMRESULT result = waveInStart(m_hWaveIn);
    if (result != MMSYSERR_NOERROR) return false;

    m_bCapturing = true;
    return true;
}

void AudioCapture::StopCapture()
{
    if (!m_bCapturing) return;
    m_bCapturing = false;
    if (m_hWaveIn) {
        waveInStop(m_hWaveIn);
        waveInReset(m_hWaveIn);
    }

    // 如果还有未完成的语音段，发送出去
    std::lock_guard<std::mutex> lock(m_vadMutex);
    if (m_bInSpeech && !m_speechBuffer.empty() && m_speechCallback) {
        auto floatData = ConvertToFloat(m_speechBuffer);
        m_speechCallback(floatData);
    }
    m_speechBuffer.clear();
    m_bInSpeech = false;
}

void AudioCapture::SetAudioDataCallback(AudioDataCallback callback)
{
    m_audioCallback = callback;
}

void AudioCapture::SetSpeechSegmentCallback(SpeechSegmentCallback callback)
{
    m_speechCallback = callback;
}

void CALLBACK AudioCapture::WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
                                        DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    if (uMsg != WIM_DATA) return;
    AudioCapture* pThis = reinterpret_cast<AudioCapture*>(dwInstance);
    if (!pThis || !pThis->m_bCapturing) return;

    WAVEHDR* pHeader = reinterpret_cast<WAVEHDR*>(dwParam1);
    pThis->ProcessBuffer(pHeader);

    if (pThis->m_bCapturing) {
        waveInAddBuffer(hwi, pHeader, sizeof(WAVEHDR));
    }
}

void AudioCapture::ProcessBuffer(WAVEHDR* pHeader)
{
    if (!pHeader || pHeader->dwBytesRecorded == 0) return;

    int numSamples = pHeader->dwBytesRecorded / sizeof(int16_t);
    std::vector<int16_t> samples(numSamples);
    memcpy(samples.data(), pHeader->lpData, pHeader->dwBytesRecorded);

    if (m_audioCallback) {
        m_audioCallback(samples);
    }

    VADProcess(samples);
}

void AudioCapture::VADProcess(const std::vector<int16_t>& samples)
{
    if (!m_speechCallback) return;

    std::lock_guard<std::mutex> lock(m_vadMutex);

    float rms = CalculateRMS(samples.data(), static_cast<int>(samples.size()));
    bool isSpeech = (rms > m_fSilenceThreshold);

    if (isSpeech) {
        if (!m_bInSpeech) {
            m_bInSpeech = true;
            m_nSilenceFrames = 0;
        }
        m_speechBuffer.insert(m_speechBuffer.end(), samples.begin(), samples.end());
        m_nSilenceFrames = 0;
    }
    else {
        if (m_bInSpeech) {
            m_speechBuffer.insert(m_speechBuffer.end(), samples.begin(), samples.end());
            m_nSilenceFrames++;

            int silenceFramesNeeded = m_nSilenceDurationMs / BUFFER_DURATION_MS;
            if (m_nSilenceFrames >= silenceFramesNeeded) {
                float durationMs = (float)m_speechBuffer.size() / m_nSampleRate * 1000.0f;
                if (durationMs >= m_nMinSpeechDurationMs) {
                    auto floatData = ConvertToFloat(m_speechBuffer);
                    m_speechCallback(floatData);
                }
                m_speechBuffer.clear();
                m_bInSpeech = false;
                m_nSilenceFrames = 0;
            }
        }
    }
}

float AudioCapture::CalculateRMS(const int16_t* data, int count)
{
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double val = static_cast<double>(data[i]);
        sum += val * val;
    }
    return static_cast<float>(std::sqrt(sum / count));
}

std::vector<float> AudioCapture::ConvertToFloat(const std::vector<int16_t>& samples)
{
    std::vector<float> result(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        result[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    return result;
}
