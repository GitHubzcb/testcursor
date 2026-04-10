# 红外图像温度提取工具 (Infrared Thermal Image Temperature Extractor)

通过红外伪彩色图像及其色条信息，反推图像中各像素对应的温度值。

## 原理

红外热像仪输出的伪彩色图像通常包含：
1. **主图像区域** — 使用伪彩色色图（如 JET）对温度进行可视化
2. **色条（Color Bar）** — 位于图像右侧，展示从最高温度到最低温度的颜色渐变
3. **温度标注** — 色条顶部标注最高温度，底部标注最低温度

本工具的工作流程：
1. **自动检测色条区域**：扫描图像右侧，通过颜色梯度分析自动定位色条位置
2. **采样色条颜色**：沿色条从上到下采样每一行的平均颜色（在 CIE Lab 色彩空间中）
3. **建立颜色-温度映射**：色条顶部对应最高温度，底部对应最低温度，中间线性插值
4. **反推像素温度**：对图像中的每个像素，在 Lab 空间中找到色条中最接近的颜色（Delta-E 距离），从而得到对应温度
5. **LUT 加速**：使用 3D 查找表加速颜色匹配过程

## 依赖

- **OpenCV 4.x**（core, imgproc, imgcodecs, highgui）
- **CMake >= 3.10**
- **C++17 编译器**

### Ubuntu / Debian 安装依赖

```bash
sudo apt-get install -y libopencv-dev cmake g++
```

## 编译

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++
make -j$(nproc)
```

## 使用方法

```bash
./build/infrared_temp_extractor <图像路径> <最高温度> <最低温度> [选项]
```

### 必需参数

| 参数 | 说明 |
|------|------|
| `image_path` | 红外伪彩色图像路径 |
| `max_temp` | 色条顶部的最高温度 |
| `min_temp` | 色条底部的最低温度 |

### 可选参数

| 参数 | 说明 |
|------|------|
| `--bar-x1 N` | 手动指定色条左边界 x 坐标 |
| `--bar-x2 N` | 手动指定色条右边界 x 坐标 |
| `--bar-y1 N` | 手动指定色条上边界 y 坐标 |
| `--bar-y2 N` | 手动指定色条下边界 y 坐标 |
| `--output-csv FILE` | 将温度矩阵输出为 CSV 文件 |
| `--output-img FILE` | 将温度热图输出为图像文件 |
| `--show` | 在窗口中显示结果（需要 GUI 环境） |
| `--exclude-bar` | 输出时排除色条区域 |

### 示例

```bash
# 自动检测色条，输出 CSV 和热图
./build/infrared_temp_extractor thermal.jpg 45.2 22.1 \
    --output-csv temperature.csv --output-img result.png

# 手动指定色条区域
./build/infrared_temp_extractor thermal.jpg 45.2 22.1 \
    --bar-x1 590 --bar-x2 620 --bar-y1 40 --bar-y2 440 \
    --output-csv temperature.csv

# 排除色条区域
./build/infrared_temp_extractor thermal.jpg 45.2 22.1 \
    --exclude-bar --output-csv temperature.csv
```

## 输出说明

### CSV 输出
每行对应图像的一行像素，每列对应一个像素，值为该像素的温度（单位：与输入相同）。可用 Python/MATLAB 等工具进一步分析。

### 图像输出
输出一张带有颜色条和温度标注的 JET 伪彩色热图。

### 控制台输出
程序会输出：
- 图像尺寸
- 检测到的色条区域
- 计算进度
- 温度范围统计
- 9 个采样点的温度值

## 测试

```bash
# 生成测试图像
pip install opencv-python-headless
python3 tests/generate_test_image.py

# 运行提取
./build/infrared_temp_extractor tests/test_infrared.png 50.0 20.0 \
    --output-csv tests/output_temp.csv --output-img tests/output_heatmap.png

# 验证精度
python3 tests/verify_accuracy.py tests/output_temp.csv
```

## 精度

在合成测试图像上的验证结果：
- 平均绝对误差：0.123°C
- 最大绝对误差：0.728°C
- 100% 像素误差 < 1°C

## 注意事项

- 本工具假设色条位于图像右侧，且颜色从上（高温）到下（低温）渐变
- 自动检测适用于大多数标准红外图像格式；如果检测失败，请使用 `--bar-x1/x2/y1/y2` 手动指定
- 色条区域应只包含纯色渐变，不应包含文字或刻度线
- 建议使用无损格式（PNG）的红外图像以获得最佳精度；JPEG 压缩可能引入颜色误差
