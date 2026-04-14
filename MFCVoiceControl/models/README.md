# 模型文件目录

此目录用于存放语音识别(ASR)和语音合成(TTS)模型文件。

## 模型下载

运行 `scripts/download_models.bat` 自动下载，或手动下载以下文件：

### ASR 模型 (SenseVoice-Small)

- 下载地址: https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
- 解压后目录: `sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/`

### TTS 模型 (VITS 中文)

- 下载地址: https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-zh-hf-fanchen-C.tar.bz2
- 解压后目录: `vits-zh-hf-fanchen-C/`

## 目录结构

```
models/
├── sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/
│   ├── model.onnx (或 model.int8.onnx)
│   └── tokens.txt
└── vits-zh-hf-fanchen-C/
    ├── vits-zh-hf-fanchen-C.onnx
    ├── lexicon.txt
    └── tokens.txt
```
