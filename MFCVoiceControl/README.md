# MFC 对话框语音识别、克隆复述与按钮操作系统

## 项目概述

本项目实现了一个基于 MFC 对话框的语音控制系统，支持：

1. **语音识别 (ASR)**：本地部署端侧轻量级语音识别模型
2. **语音克隆复述 (TTS)**：将识别结果通过语音克隆模型复述出来
3. **命令解析与执行**：解析语音指令，自动设置对话框参数并执行按钮操作

**示例场景**：用户说"设置温度48"，系统识别语音内容，在输入框填入"48"，点击确定按钮，并通过语音复述"已设置温度为48"。

---

## 端侧模型选型分析

### 关于 Qwen3-ASR 和 VoxCPM2

| 模型 | 参数量 | 硬件需求 | MFC集成难度 | 是否推荐 |
|------|--------|----------|-------------|----------|
| Qwen3-ASR-0.6B | 600M | ~2GB VRAM (INT4) | 高（需Python/ONNX桥接） | ⚠️ 可选 |
| Qwen3-ASR-1.7B | 1.7B | ~4GB VRAM (INT4) | 高 | ❌ 不推荐端侧Pad |
| VoxCPM2 | 2B | ~8GB VRAM, CUDA 12+ | 极高（纯Python） | ❌ 不推荐端侧Pad |

**分析**：
- **Qwen3-ASR** 精度极高，支持52种语言，但模型较大，MFC（C++）集成需要通过ONNX或Python子进程桥接。在有GPU的Pad上可通过ONNX Runtime + DirectML加速。
- **VoxCPM2** 是2B参数的扩散自回归TTS模型，需要~8GB显存和CUDA 12+，对端侧Pad来说资源要求过高，不适合轻量级部署。

### 推荐方案（端侧Pad最佳实践）

| 组件 | 推荐模型 | 参数量 | 优势 |
|------|----------|--------|------|
| **ASR（语音识别）** | **sherpa-onnx + SenseVoice-Small** | ~234M | 纯C++ API，ONNX推理，15倍Whisper速度，中英日韩支持 |
| **TTS（语音合成）** | **sherpa-onnx + Piper/VITS** | ~20-60M | 纯C++ API，CPU实时合成，免GPU |
| **备选ASR** | **whisper.cpp (small/base)** | 74M/244M | 纯C/C++，无依赖，量化模型极小 |
| **备选TTS（需GPU）** | **CosyVoice2-0.5B** | 500M | 零样本语音克隆，150ms首包延迟 |

### 最终推荐架构

```
┌─────────────────────────────────────────────────┐
│              MFC 对话框应用程序 (x86)              │
├─────────────────────────────────────────────────┤
│                                                   │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ 录音模块  │→│ ASR模块   │→│ 命令解析模块   │  │
│  │ WinAPI   │  │sherpa-onnx│  │ 正则匹配      │  │
│  │ waveIn   │  │SenseVoice │  │ 中文数字转换  │  │
│  └──────────┘  └──────────┘  └───────┬───────┘  │
│                                       │          │
│  ┌──────────┐  ┌──────────┐  ┌───────▼───────┐  │
│  │ 音频播放  │←│ TTS模块   │←│ MFC控件操作   │  │
│  │ waveOut  │  │sherpa-onnx│  │ SetDlgItemText│  │
│  │ PlaySound│  │Piper/VITS │  │ SendMessage   │  │
│  └──────────┘  └──────────┘  └───────────────┘  │
│                                                   │
└─────────────────────────────────────────────────┘
```

---

## 一步一步部署指南

### 第1步：环境准备

#### 1.1 开发工具
- Visual Studio 2019/2022 (需安装MFC组件)
- CMake 3.16+
- Git

#### 1.2 下载 sherpa-onnx 预编译库

```bash
# 方法一：下载预编译的 Windows x86 库
# 访问 https://github.com/k2-fsa/sherpa-onnx/releases
# 下载 sherpa-onnx-v{version}-win-x86.zip (32位) 或 win-x64.zip (64位)

# 方法二：从源码编译（推荐，可控制运行时为MD模式以匹配MFC）
git clone https://github.com/k2-fsa/sherpa-onnx.git
cd sherpa-onnx
mkdir build-win32
cd build-win32

# 生成 x86 (Win32) 项目
cmake -G "Visual Studio 17 2022" -A Win32 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_SHARED_LIBS=ON ^
  -DCMAKE_INSTALL_PREFIX=./install ^
  ..

cmake --build . --config Release --target install
```

> **注意**：如果MFC项目使用 `/MD` 运行时（默认），需要确保 sherpa-onnx 也使用 `/MD` 编译。
> 修改 `sherpa-onnx/CMakeLists.txt` 中的 `/MT` 为 `/MD`。

### 第2步：下载ASR模型（SenseVoice-Small）

```bash
# 下载 SenseVoice-Small ONNX 模型（INT8量化版，约100MB）
cd models

# 使用sherpa-onnx提供的预转换模型
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
tar xvf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2

# 模型目录结构：
# models/
# └── sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/
#     ├── model.onnx        (或 model.int8.onnx)
#     └── tokens.txt
```

### 第3步：下载TTS模型（Piper中文语音）

```bash
# 下载 Piper 中文 TTS 模型
cd models

# sherpa-onnx 提供的中文VITS模型
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-zh-hf-fanchen-C.tar.bz2
tar xvf vits-zh-hf-fanchen-C.tar.bz2

# 模型目录结构：
# models/
# └── vits-zh-hf-fanchen-C/
#     ├── vits-zh-hf-fanchen-C.onnx
#     ├── lexicon.txt
#     └── tokens.txt
```

### 第4步：配置MFC项目

#### 4.1 创建MFC对话框项目
1. Visual Studio → 新建项目 → MFC应用程序
2. 选择"基于对话框"
3. 项目名称：`MFCVoiceControl`

#### 4.2 配置头文件和库目录

在项目属性中设置：

```
C/C++ → 附加包含目录:
  $(SolutionDir)sherpa-onnx\install\include

链接器 → 附加库目录:
  $(SolutionDir)sherpa-onnx\install\lib

链接器 → 附加依赖项:
  sherpa-onnx-c-api.lib
  onnxruntime.lib

生成事件 → 后期生成事件:
  xcopy /Y "$(SolutionDir)sherpa-onnx\install\bin\*.dll" "$(OutDir)"
```

### 第5步：编译运行

1. 将 `src/` 下的源码文件添加到MFC项目中
2. 将模型文件放到可执行文件同级的 `models/` 目录
3. 编译运行即可

---

## 项目文件说明

```
MFCVoiceControl/
├── README.md                          # 本文件
├── src/
│   ├── VoiceControlDlg.h             # MFC对话框头文件
│   ├── VoiceControlDlg.cpp           # MFC对话框实现（主逻辑）
│   ├── AudioCapture.h                # Windows音频录制模块
│   ├── AudioCapture.cpp              # 音频录制实现
│   ├── SherpaASR.h                   # ASR引擎封装头文件
│   ├── SherpaASR.cpp                 # ASR引擎封装实现
│   ├── SherpaTTS.h                   # TTS引擎封装头文件
│   ├── SherpaTTS.cpp                 # TTS引擎封装实现
│   ├── CommandParser.h               # 命令解析器头文件
│   ├── CommandParser.cpp             # 命令解析器实现
│   ├── VoiceControlApp.h             # MFC应用程序类
│   ├── VoiceControlApp.cpp           # MFC应用程序入口
│   ├── Resource.h                    # 资源ID定义
│   └── stdafx.h                      # 预编译头文件
├── res/
│   └── VoiceControl.rc               # 对话框资源文件
├── scripts/
│   ├── download_models.bat           # Windows模型下载脚本
│   └── build_sherpa_onnx.bat         # sherpa-onnx编译脚本
└── models/                           # 模型存放目录（需下载）
    ├── .gitkeep
    └── README.md
```

---

## 备选方案：使用 whisper.cpp + Piper TTS

如果你更倾向于完全无依赖的纯 C/C++ 方案：

### ASR: whisper.cpp
```bash
git clone https://github.com/ggml-org/whisper.cpp.git
cd whisper.cpp
# 下载模型
bash models/download-ggml-model.sh base

# 编译 (Windows MSVC)
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A Win32 ..
cmake --build . --config Release
```

### TTS: Piper
```bash
# 下载预编译的 piper.exe
# https://github.com/rhasspy/piper/releases
# 下载中文语音模型
# https://huggingface.co/rhasspy/piper-voices/tree/main/zh_CN
```

此方案通过子进程调用 whisper 和 piper，适合快速原型验证。

---

## 高级方案：使用 Qwen3-ASR + CosyVoice2（需GPU）

如果Pad设备有独立GPU（6GB+ VRAM），可获得最佳识别精度和语音克隆效果：

```bash
# 创建Python环境
conda create -n voice_ctrl python=3.10
conda activate voice_ctrl

# 安装 Qwen3-ASR
pip install onnxruntime-directml
pip install qwen3-asr-onnx  # 社区ONNX导出工具

# 安装 CosyVoice2
pip install cosyvoice  # 或从GitHub安装
```

MFC程序通过 **命名管道** 或 **Socket** 与Python后端通信。
详见 `src/` 中的Python桥接代码注释。

---

## 许可证

本项目代码采用 MIT 许可证。
所使用的模型请遵循各自的开源协议：
- sherpa-onnx: Apache-2.0
- SenseVoice: 阿里巴巴模型协议
- Piper/VITS: MIT
- whisper.cpp: MIT
