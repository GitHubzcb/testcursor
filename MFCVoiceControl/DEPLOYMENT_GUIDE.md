# MFC 对话框语音识别 + 克隆复述 + 按钮操作 — 完整部署指南

## 目录

- [一、需求分析与模型选型](#一需求分析与模型选型)
- [二、推荐方案与架构设计](#二推荐方案与架构设计)
- [三、环境准备](#三环境准备)
- [四、第一步：编译 sherpa-onnx (C++ 库)](#四第一步编译-sherpa-onnx-c-库)
- [五、第二步：下载 ASR 和 TTS 模型](#五第二步下载-asr-和-tts-模型)
- [六、第三步：创建 MFC 对话框项目并集成](#六第三步创建-mfc-对话框项目并集成)
- [七、第四步：编译运行与测试](#七第四步编译运行与测试)
- [八、进阶：使用 Qwen3-ASR + CosyVoice2 (需GPU)](#八进阶使用-qwen3-asr--cosyvoice2-需gpu)
- [九、常见问题](#九常见问题)

---

## 一、需求分析与模型选型

### 1.1 您的场景

| 项目 | 描述 |
|------|------|
| **硬件** | Windows Pad (端侧设备) |
| **应用** | MFC 对话框程序 (x86 编译平台) |
| **功能** | 语音识别 → 命令解析 → 控件操作 → 语音复述 |
| **示例** | "设置温度48" → 输入框填入48 → 点击确定按钮 → 语音回复"已设置温度为48" |

### 1.2 关于 Qwen3-ASR 和 VoxCPM2

**Qwen3-ASR：**
- ✅ 精度极高（SOTA级别），支持52种语言/方言
- ✅ 0.6B版本有ONNX导出工具（[qwen3-asr-onnx](https://github.com/andrewleech/qwen3-asr-onnx)）
- ⚠️ 0.6B INT4 需 ~2GB显存，1.7B需 ~4GB显存
- ⚠️ 原生API是Python，MFC集成需要ONNX Runtime桥接
- ⚠️ 自回归模型，推理速度相对较慢

**VoxCPM2：**
- ✅ 语音克隆效果极佳，支持30种语言
- ❌ 2B参数，需 ~8GB显存 + CUDA 12+
- ❌ 纯Python实现，无法直接集成到C++ MFC
- ❌ 对端侧Pad资源要求过高

**结论：** 在端侧Windows Pad上，Qwen3-ASR 和 VoxCPM2 均不是最优选择。推荐使用更轻量的替代方案。

### 1.3 推荐的端侧模型

| 组件 | 模型 | 大小 | 特点 |
|------|------|------|------|
| **ASR** | SenseVoice-Small (via sherpa-onnx) | ~234M (~100MB INT8) | 速度是Whisper的15倍，中英日韩粤语支持 |
| **TTS** | VITS/Piper (via sherpa-onnx) | ~20-60MB | 纯CPU实时合成，免GPU |
| **框架** | sherpa-onnx | — | 统一的C++ API，ASR+TTS一站式 |

**为什么选 sherpa-onnx？**
1. **纯C++ API** — 可直接链接到MFC项目，无需Python
2. **ONNX Runtime** — 跨平台推理，支持x86/x64/ARM
3. **离线运行** — 无需网络连接
4. **模型丰富** — 预转换的ONNX模型直接可用
5. **生产就绪** — 多语言API (C++/C#/Java/Python)

---

## 二、推荐方案与架构设计

```
用户说话
    │
    ▼
┌──────────────────┐
│  Windows waveIn   │  ← 16kHz, 16bit, mono PCM 录音
│  音频采集模块     │
└────────┬─────────┘
         │ PCM 数据流
         ▼
┌──────────────────┐
│  简易 VAD 模块    │  ← 基于 RMS 能量的语音活动检测
│  静音切割         │     检测到完整语音段后触发 ASR
└────────┬─────────┘
         │ 完整语音段 (float[])
         ▼
┌──────────────────┐
│  sherpa-onnx ASR  │  ← SenseVoice-Small (ONNX)
│  语音识别引擎     │     ~100ms 识别延迟 (10s音频)
└────────┬─────────┘
         │ 文本: "设置温度四十八"
         ▼
┌──────────────────┐
│  命令解析器       │  ← 正则匹配 + 中文数字转换
│  CommandParser    │     "温度" → IDC_EDIT_TEMPERATURE
│                    │     "四十八" → "48"
└────────┬─────────┘
         │ Command{SET_VALUE, "温度", "48"}
         ▼
┌──────────────────────────────────────┐
│           MFC 对话框 UI              │
│                                      │
│  SetDlgItemText(IDC_EDIT_TEMP, "48") │  ← 设置输入框
│  SendMessage(IDC_BTN_CONFIRM, ...)   │  ← 模拟点击确定
│                                      │
└────────┬─────────────────────────────┘
         │ 回复文本: "已设置温度为48"
         ▼
┌──────────────────┐
│  sherpa-onnx TTS  │  ← VITS/Piper 中文模型
│  语音合成引擎     │     CPU 实时合成
└────────┬─────────┘
         │ PCM 音频数据
         ▼
┌──────────────────┐
│  Windows waveOut  │  ← 播放合成语音
│  音频播放模块     │
└──────────────────┘
```

---

## 三、环境准备

### 3.1 必需软件

| 软件 | 版本要求 | 用途 |
|------|----------|------|
| Visual Studio | 2019 或 2022 | MFC 开发 (需安装 "使用C++的桌面开发" + MFC组件) |
| CMake | 3.16+ | 编译 sherpa-onnx |
| Git | 最新版 | 克隆源码 |
| curl | Windows 10+ 内置 | 下载模型文件 |

### 3.2 安装 Visual Studio MFC 组件

1. 运行 Visual Studio Installer
2. 勾选 "使用C++的桌面开发" 工作负载
3. 在右侧"安装详细信息"中确保勾选:
   - MSVC v143 生成工具 (或 v142)
   - Windows 10/11 SDK
   - **用于最新 v143 生成工具的 C++ MFC (x86 和 x64)**
4. 完成安装

---

## 四、第一步：编译 sherpa-onnx (C++ 库)

### 4.1 方法一：下载预编译库（推荐快速开始）

访问 [sherpa-onnx Releases](https://github.com/k2-fsa/sherpa-onnx/releases)，
下载对应平台的预编译包：

- `sherpa-onnx-v{version}-win-x86.zip` (32位，MFC x86)
- `sherpa-onnx-v{version}-win-x64.zip` (64位)

解压后得到：
```
sherpa-onnx-v{version}-win-x86/
├── bin/          ← DLL文件 (运行时需要)
│   ├── sherpa-onnx-c-api.dll
│   ├── onnxruntime.dll
│   └── ...
├── include/      ← 头文件
│   └── sherpa-onnx/c-api/c-api.h
└── lib/          ← 导入库
    ├── sherpa-onnx-c-api.lib
    └── onnxruntime.lib
```

> **⚠️ 注意运行时匹配问题：**
> 预编译库默认使用 `/MT`（静态运行时），而 MFC 项目默认使用 `/MD`（动态运行时）。
> 如果链接时出现 `LNK2038: mismatch detected for 'RuntimeLibrary'` 错误，
> 需要从源码编译并修改运行时配置。

### 4.2 方法二：从源码编译（推荐生产环境）

打开 **"x86 Native Tools Command Prompt for VS 2022"**（从开始菜单搜索），
或运行：
```batch
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
```

然后执行：

```batch
REM 1. 克隆源码
git clone --depth 1 https://github.com/k2-fsa/sherpa-onnx.git
cd sherpa-onnx

REM 2. 创建编译目录
mkdir build-win32
cd build-win32

REM 3. CMake 配置（生成 x86 项目）
cmake -G "Visual Studio 17 2022" -A Win32 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=ON ^
    -DCMAKE_INSTALL_PREFIX=.\install ^
    -DSHERPA_ONNX_ENABLE_C_API=ON ^
    -DSHERPA_ONNX_ENABLE_BINARY=OFF ^
    ..

REM 4. 编译和安装
cmake --build . --config Release --target install
```

### 4.3 修改运行时为 /MD（匹配MFC）

如果遇到运行时不匹配错误，编辑 `sherpa-onnx/CMakeLists.txt`：

找到类似以下的代码段：
```cmake
if(MSVC)
  # 将 /MT 改为 /MD
  string(REPLACE "/MT" "/MD" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/MTd" "/MDd" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
```

或者在 MFC 项目中将运行时改为 `/MT`：
- 项目属性 → C/C++ → 代码生成 → 运行库 → 多线程 (/MT)

---

## 五、第二步：下载 ASR 和 TTS 模型

### 5.1 自动下载

运行 `scripts/download_models.bat`。

### 5.2 手动下载

#### ASR 模型：SenseVoice-Small

```
下载地址: https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
大小: ~200MB
```

解压到 `models/` 目录下。

#### TTS 模型：VITS 中文语音

```
下载地址: https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-zh-hf-fanchen-C.tar.bz2
大小: ~100MB
```

解压到 `models/` 目录下。

#### 更多可选中文TTS模型

| 模型 | 下载链接 | 特点 |
|------|----------|------|
| vits-zh-hf-fanchen-C | sherpa-onnx releases | 女声，自然度高 |
| vits-zh-hf-theresa | sherpa-onnx releases | 另一种女声 |
| vits-melo-tts-zh_en | sherpa-onnx releases | 中英混合 |

完整模型列表: https://k2-fsa.github.io/sherpa/onnx/tts/index.html

### 5.3 验证模型目录结构

```
models/
├── sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/
│   ├── model.onnx          (或 model.int8.onnx)
│   └── tokens.txt
└── vits-zh-hf-fanchen-C/
    ├── vits-zh-hf-fanchen-C.onnx
    ├── lexicon.txt
    └── tokens.txt
```

---

## 六、第三步：创建 MFC 对话框项目并集成

### 6.1 创建MFC项目

1. Visual Studio → 文件 → 新建 → 项目
2. 选择 **"MFC 应用"**
3. 项目名称: `MFCVoiceControl`
4. 应用程序类型: **基于对话框**
5. 项目设置:
   - 平台: **x86 (Win32)**
   - 字符集: **使用 Unicode 字符集**
   - MFC使用: **在共享DLL中使用MFC**

### 6.2 配置项目属性

右键项目 → 属性，进行以下配置：

#### C/C++ → 常规
```
附加包含目录: $(SolutionDir)sherpa-onnx-install\include
```

#### C/C++ → 预处理器
```
预处理器定义: 添加 USE_SHERPA_ONNX
```

#### 链接器 → 常规
```
附加库目录: $(SolutionDir)sherpa-onnx-install\lib
```

#### 链接器 → 输入
```
附加依赖项:
  sherpa-onnx-c-api.lib
  onnxruntime.lib
  winmm.lib
```

#### 生成事件 → 后期生成事件
```batch
xcopy /Y /D "$(SolutionDir)sherpa-onnx-install\bin\*.dll" "$(OutDir)"
xcopy /Y /D /E /I "$(SolutionDir)models" "$(OutDir)models"
```

### 6.3 添加源代码

将 `src/` 目录下的所有 `.h` 和 `.cpp` 文件添加到项目中。

**核心文件说明：**

| 文件 | 功能 |
|------|------|
| `AudioCapture.h/cpp` | Windows waveIn 录音 + 简易 VAD |
| `SherpaASR.h/cpp` | sherpa-onnx ASR 引擎封装 |
| `SherpaTTS.h/cpp` | sherpa-onnx TTS 引擎封装 |
| `CommandParser.h/cpp` | 语音命令解析（含中文数字转换） |
| `VoiceControlDlg.h/cpp` | MFC 对话框（UI + 业务逻辑） |
| `VoiceControlApp.h/cpp` | MFC 应用程序入口 |
| `Resource.h` | 控件 ID 定义 |

### 6.4 修改对话框资源

使用 Visual Studio 资源编辑器设计对话框，或参考 `res/VoiceControl.rc` 中的布局。

关键控件：
- **开始监听/停止监听** 按钮
- **ASR 识别结果** 显示框（只读）
- **温度/湿度/压力** 输入框
- **模式选择** 下拉框
- **确定/重置** 按钮
- **操作日志** 列表框

---

## 七、第四步：编译运行与测试

### 7.1 编译

1. 确保平台选择为 **x86 (Win32) Release**
2. 生成 → 生成解决方案 (F7)
3. 检查输出目录：
   ```
   Release/
   ├── MFCVoiceControl.exe
   ├── sherpa-onnx-c-api.dll
   ├── onnxruntime.dll
   └── models/
       ├── sherpa-onnx-sense-voice-.../
       └── vits-zh-hf-fanchen-C/
   ```

### 7.2 运行测试

1. 启动程序
2. 检查日志：确认 ASR/TTS 引擎初始化成功
3. 点击 **"开始监听"**
4. 对麦克风说：**"设置温度48"**
5. 预期结果：
   - 识别结果显示: "设置温度48"
   - 温度输入框自动填入: "48"
   - 日志显示: "执行: 设置温度 = 48"
   - 语音回复: "已设置温度为48"

### 7.3 支持的语音命令

| 语音指令示例 | 解析结果 | 操作 |
|-------------|----------|------|
| "设置温度48" | SET_VALUE, 温度, 48 | 温度框填入48 |
| "温度四十八" | SET_VALUE, 温度, 48 | 温度框填入48（中文数字） |
| "湿度60" | SET_VALUE, 湿度, 60 | 湿度框填入60 |
| "设置压力101.3" | SET_VALUE, 压力, 101.3 | 压力框填入101.3 |
| "确定" / "确认" | CONFIRM | 点击确定按钮 |
| "重置" / "清除" | RESET | 清空所有输入框 |
| "选择自动模式" | SELECT_MODE, 自动 | 切换下拉框到"自动" |
| "切换节能模式" | SELECT_MODE, 节能 | 切换下拉框到"节能" |

---

## 八、进阶：使用 Qwen3-ASR + CosyVoice2 (需GPU)

如果你的Pad设备有独立GPU（≥6GB VRAM），可以获得更高的识别精度和更自然的语音克隆效果。

### 8.1 架构：Python 后端 + MFC 前端

由于 Qwen3-ASR 和 CosyVoice2 均为 Python 生态模型，需要通过进程间通信(IPC)与MFC集成：

```
┌──────────────────┐          ┌──────────────────┐
│  MFC 对话框 (C++)│  Socket  │  Python 后端      │
│                  │ ←──────→ │                    │
│  录音 + UI操作   │  JSON    │  Qwen3-ASR (ONNX) │
│  命令执行        │          │  CosyVoice2 (TTS)  │
└──────────────────┘          └──────────────────┘
```

### 8.2 Python 后端服务

```python
#!/usr/bin/env python3
"""
voice_backend.py - Qwen3-ASR + CosyVoice2 后端服务
MFC 前端通过 TCP Socket 通信
"""

import socket
import json
import struct
import numpy as np
import soundfile as sf
import io
import threading

# ---- ASR: Qwen3-ASR via ONNX Runtime ----
# pip install onnxruntime-directml
# 参考: https://github.com/andrewleech/qwen3-asr-onnx

class Qwen3ASREngine:
    def __init__(self, model_dir="models/qwen3-asr-0.6b-onnx"):
        import onnxruntime as ort

        # 使用 DirectML 加速 (Windows GPU)
        providers = ['DmlExecutionProvider', 'CPUExecutionProvider']
        self.session = ort.InferenceSession(
            f"{model_dir}/model_int4.onnx",
            providers=providers
        )
        # 加载 tokenizer
        # ... (具体实现取决于 qwen3-asr-onnx 导出格式)

    def recognize(self, audio_data: np.ndarray, sample_rate: int = 16000) -> str:
        # 预处理音频 → 模型推理 → 解码文本
        # 具体实现参考 qwen3-asr-onnx 仓库
        pass


# ---- TTS: CosyVoice2 ----
# pip install cosyvoice
# 参考: https://github.com/FunAudioLLM/CosyVoice

class CosyVoiceTTSEngine:
    def __init__(self, model_dir="models/CosyVoice2-0.5B"):
        from cosyvoice import CosyVoice2

        self.model = CosyVoice2(model_dir)

    def synthesize(self, text: str, reference_wav: str = None) -> bytes:
        if reference_wav:
            # 语音克隆模式
            result = self.model.inference_zero_shot(
                text, "", reference_wav
            )
        else:
            # 普通合成模式
            result = self.model.inference_sft(
                text, "中文女"
            )

        # 收集所有生成的音频片段
        audio_data = []
        for chunk in result:
            audio_data.append(chunk['tts_speech'].numpy())

        audio = np.concatenate(audio_data, axis=-1)

        # 转换为 WAV 字节
        buf = io.BytesIO()
        sf.write(buf, audio.flatten(), 22050, format='WAV')
        return buf.getvalue()


class VoiceBackendServer:
    """TCP Socket 服务器，接收 MFC 端发送的音频数据，返回识别和合成结果"""

    def __init__(self, host='127.0.0.1', port=9527):
        self.host = host
        self.port = port
        self.asr = Qwen3ASREngine()
        self.tts = CosyVoiceTTSEngine()

    def start(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        sock.listen(5)
        print(f"语音后端服务启动: {self.host}:{self.port}")

        while True:
            client, addr = sock.accept()
            threading.Thread(target=self.handle_client,
                           args=(client,), daemon=True).start()

    def handle_client(self, client):
        try:
            while True:
                # 读取消息长度 (4字节)
                header = client.recv(4)
                if not header:
                    break
                msg_len = struct.unpack('!I', header)[0]

                # 读取消息内容
                data = b''
                while len(data) < msg_len:
                    chunk = client.recv(min(msg_len - len(data), 4096))
                    if not chunk:
                        break
                    data += chunk

                request = json.loads(data.decode('utf-8'))
                response = self.process_request(request)

                # 发送响应
                resp_data = json.dumps(response).encode('utf-8')
                client.sendall(struct.pack('!I', len(resp_data)))
                client.sendall(resp_data)

        except Exception as e:
            print(f"客户端处理错误: {e}")
        finally:
            client.close()

    def process_request(self, request):
        cmd = request.get('command')

        if cmd == 'asr':
            import base64
            audio_bytes = base64.b64decode(request['audio_data'])
            audio_array = np.frombuffer(audio_bytes, dtype=np.float32)
            text = self.asr.recognize(audio_array, request.get('sample_rate', 16000))
            return {'status': 'ok', 'text': text}

        elif cmd == 'tts':
            text = request['text']
            wav_data = self.tts.synthesize(text)
            import base64
            return {
                'status': 'ok',
                'audio_data': base64.b64encode(wav_data).decode('utf-8')
            }

        return {'status': 'error', 'message': 'unknown command'}


if __name__ == '__main__':
    server = VoiceBackendServer()
    server.start()
```

### 8.3 MFC 端 Socket 客户端代码

如果选择 Python 后端方案，在 MFC 中通过 TCP Socket 通信：

```cpp
// SocketClient.h - 与 Python 后端通信
#pragma once
#include <winsock2.h>
#include <string>
#include <vector>
#pragma comment(lib, "ws2_32.lib")

class SocketClient
{
public:
    SocketClient() : m_socket(INVALID_SOCKET) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~SocketClient() {
        Disconnect();
        WSACleanup();
    }

    bool Connect(const char* host = "127.0.0.1", int port = 9527) {
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket == INVALID_SOCKET) return false;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host);

        return connect(m_socket, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    void Disconnect() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    // 发送请求并接收响应 (JSON 字符串)
    std::string SendRequest(const std::string& jsonRequest) {
        // 发送长度头 + 数据
        uint32_t len = htonl((uint32_t)jsonRequest.size());
        send(m_socket, (char*)&len, 4, 0);
        send(m_socket, jsonRequest.c_str(), (int)jsonRequest.size(), 0);

        // 接收响应长度
        uint32_t respLen = 0;
        recv(m_socket, (char*)&respLen, 4, 0);
        respLen = ntohl(respLen);

        // 接收响应数据
        std::string response(respLen, '\0');
        int received = 0;
        while (received < (int)respLen) {
            int n = recv(m_socket, &response[received], respLen - received, 0);
            if (n <= 0) break;
            received += n;
        }

        return response;
    }

private:
    SOCKET m_socket;
};
```

### 8.4 Qwen3-ASR ONNX 导出步骤

```bash
# 安装导出工具
pip install qwen3-asr-onnx

# 导出 0.6B 模型（INT4 量化，适合端侧）
python -m qwen3_asr_onnx.export \
    --model Qwen/Qwen3-ASR-0.6B \
    --output ./models/qwen3-asr-0.6b-onnx \
    --quantize int4 \
    --fuse-rmsnorm

# 导出后目录:
# models/qwen3-asr-0.6b-onnx/
# ├── model_int4.onnx
# ├── config.json
# └── tokenizer files...
```

---

## 九、常见问题

### Q1: 链接错误 LNK2038: mismatch detected for 'RuntimeLibrary'

**原因：** sherpa-onnx 默认用 `/MT`，MFC 默认用 `/MD`。

**解决：** 二选一:
- 从源码编译 sherpa-onnx，将 `/MT` 改为 `/MD`
- 将 MFC 项目的运行库改为 `/MT`

### Q2: 识别结果为空或不准确

**排查步骤：**
1. 检查麦克风是否正常工作（Windows 设置 → 声音 → 输入）
2. 调低 VAD 静音阈值：`m_audioCapture.SetSilenceThreshold(300.0f)`
3. 确保说话时间 > 500ms
4. 检查模型文件路径是否正确

### Q3: TTS 没有声音

**排查步骤：**
1. 确认扬声器/耳机正常
2. 检查 TTS 模型文件是否完整
3. 检查日志是否有 TTS 初始化错误

### Q4: 中文数字识别不正确

SenseVoice 开启 ITN（逆文本归一化）后，通常会直接输出阿拉伯数字。如果输出中文数字，`CommandParser` 内置了中文数字转换功能，支持：
- 一~九, 十, 百
- 零~玖（大写）
- 两, 〇
- 小数点（"点"→"."）

### Q5: 在没有GPU的Pad上能否运行？

完全可以。推荐方案（sherpa-onnx + SenseVoice + Piper/VITS）完全支持 CPU 推理。
- ASR: SenseVoice-Small 在 CPU 上处理 10 秒音频约需 100ms
- TTS: VITS 在 CPU 上可实现实时合成

### Q6: 能否支持更多自定义命令？

可以。在 `CommandParser` 中添加：
```cpp
// 添加新参数
m_cmdParser.AddParameter("风速", {"风速", "风量", "fan"});

// 添加参数到控件映射
m_paramToEditID["风速"] = IDC_EDIT_FANSPEED;

// 添加新模式
m_cmdParser.AddMode("静音", {"静音", "安静", "silent"});
```

### Q7: 如何实现真正的语音克隆（用用户自己的声音复述）？

推荐方案中的 Piper/VITS 模型是预训练的标准语音，不支持零样本克隆。
如需语音克隆，有两个选择：

1. **CosyVoice2 (需GPU)：** 参考第八节，通过 Python 后端实现零样本克隆
2. **Piper 微调：** 录制用户 ~30分钟语音数据，微调 Piper 模型（参考 [Piper Training](https://github.com/rhasspy/piper/blob/master/TRAINING.md)）

---

## 总结

| 方案 | ASR 模型 | TTS 模型 | 硬件要求 | 集成方式 | 适用场景 |
|------|----------|----------|----------|----------|----------|
| **推荐** | SenseVoice-Small | VITS/Piper | CPU only | C++ 直接链接 | 端侧Pad，无GPU |
| **备选A** | whisper.cpp (base) | Piper TTS | CPU only | C++ 直接链接 | 极简部署 |
| **高级** | Qwen3-ASR-0.6B | CosyVoice2-0.5B | GPU ≥6GB | Python后端+Socket | 高精度+语音克隆 |
| ❌ 不推荐 | Qwen3-ASR-1.7B | VoxCPM2 | GPU ≥8GB | Python后端+Socket | 资源要求过高 |
