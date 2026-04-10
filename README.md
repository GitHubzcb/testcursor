# 远红外图像人体检测（OpenCV + C++）

这是一个基于 OpenCV 的远红外（热成像）人体检测示例程序，采用传统视觉方法实现：

- 灰度化 + 高斯滤波
- OTSU 自适应阈值分割（支持热目标亮/暗极性）
- 形态学去噪（开闭运算）
- 轮廓筛选（面积、长宽比、实心度）输出人体框

> 适合做基础原型验证。若需要更强鲁棒性，可进一步引入背景建模、时序跟踪或深度学习检测器。

## 1. 依赖

- C++17 编译器（g++ / clang++ / MSVC）
- CMake 3.12+
- OpenCV 4.x（3.x 大多也可工作）

## 2. 构建

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

构建完成后可执行文件为：

- Linux/macOS: `build/ir_human_detector`
- Windows: `build/Debug/ir_human_detector.exe`（或对应配置目录）

## 3. 运行

### 3.1 使用默认摄像头

```bash
./build/ir_human_detector
```

### 3.2 使用远红外视频文件

```bash
./build/ir_human_detector --input /path/to/thermal_video.mp4
```

### 3.3 常用参数

```bash
./build/ir_human_detector \
  --input /path/to/thermal_video.mp4 \
  --min-area 700 \
  --max-area-ratio 0.30 \
  --polarity auto \
  --show-mask
```

参数说明：

- `--input <path>`：输入视频路径，不提供则使用摄像头
- `--camera <index>`：摄像头索引，默认 `0`
- `--min-area <pixels>`：候选人体最小面积（默认 `500`）
- `--max-area-ratio <0-1>`：候选人体最大面积占画面比例（默认 `0.35`）
- `--polarity <hot|cold|auto>`：
  - `hot`：人更亮（常见热像显示）
  - `cold`：人更暗（部分伪彩/反色流）
  - `auto`：自动选择更合理掩码（默认）
- `--show-mask`：显示二值掩码窗口，便于调参
- `--no-display`：不弹窗显示，仅执行处理流程

## 4. 退出

- 在显示窗口中按 `q` / `Q` / `ESC` 退出。

## 5. 常见问题

- 若 CMake 在编译器检测阶段报错 `cannot find -lstdc++`，说明当前环境缺少 C++ 标准库开发组件。  
  请安装完整 C++ 工具链后再构建（例如安装 `g++`/`libstdc++` 对应开发包）。
