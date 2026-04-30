# 红外图像温度提取工具 (Infrared Thermal Image Temperature Extractor)

通过红外伪彩色图像及其色条信息，全自动反推图像中各像素对应的温度值。

**无需安装任何额外 OCR 软件** — 程序内置了基于连通域分析和结构特征的数字识别算法，仅依赖 OpenCV 即可自动识别色条上的温度标注。

## 原理

红外热像仪输出的伪彩色图像通常包含：
1. **主图像区域** — 使用伪彩色色图（如 JET）对温度进行可视化
2. **色条（Color Bar）** — 位于图像右侧，展示从最高温度到最低温度的颜色渐变
3. **温度标注** — 色条顶部标注最高温度，底部标注最低温度

本工具的工作流程：
1. **自动检测色条区域**：扫描图像右侧，通过颜色梯度分析自动定位色条位置
2. **自动识别温度标注**：内置数字识别器自动读取色条附近的数字（最大/最小温度）
3. **采样色条颜色**：沿色条从上到下采样每一行的平均颜色（在 CIE Lab 色彩空间中）
4. **建立颜色-温度映射**：色条顶部对应最高温度，底部对应最低温度，中间线性插值
5. **反推像素温度**：对图像中的每个像素，在 Lab 空间中找到色条中最接近的颜色（Delta-E 距离），得到对应温度
6. **LUT 加速**：使用 3D 查找表加速颜色匹配过程

## 两种数字识别方案

### 方案一：内置数字识别器（默认，无需安装额外软件）

- **仅依赖 OpenCV**，无需安装 Tesseract 或其他 OCR 软件
- 使用连通域分析（Connected Component Analysis）分割字符
- 通过结构特征（孔洞数、区域像素分布、宽高比等）分类 0-9 和小数点
- 多种二值化策略（Otsu、自适应阈值、固定阈值）提高鲁棒性
- 适用于大多数标准红外图像

### 方案二：Tesseract OCR（可选编译）

- 编译时添加 `-DUSE_TESSERACT=ON` 启用
- 需要安装 `libtesseract-dev` 和 `tesseract-ocr-eng`
- 对复杂字体和小文字可能有更好的识别率
- 运行时通过 `--use-tesseract` 参数选择使用

## 依赖

### 最小依赖（默认，无需额外 OCR 软件）

- **OpenCV 4.x**（core, imgproc, imgcodecs, highgui）
- **CMake >= 3.10**
- **C++17 编译器**

```bash
# Ubuntu / Debian
sudo apt-get install -y libopencv-dev cmake g++
```

### 可选依赖（启用 Tesseract）

```bash
# 仅当需要 Tesseract 模式时安装
sudo apt-get install -y libtesseract-dev tesseract-ocr tesseract-ocr-eng libleptonica-dev
```

## 编译

### 默认编译（仅需 OpenCV，无需额外 OCR 软件）

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 带 Tesseract 支持编译（可选）

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_TESSERACT=ON
make -j$(nproc)
```

## 使用方法

```bash
./build/infrared_temp_extractor <图像路径> [最高温度 最低温度] [选项]
```

### 全自动模式（推荐）

程序自动检测色条区域，自动识别温度范围，无需任何额外输入：

```bash
./build/infrared_temp_extractor thermal.jpg --output-csv temperature.csv
```

### 手动指定温度模式

如果自动识别不准确，可以手动提供温度范围：

```bash
./build/infrared_temp_extractor thermal.jpg 45.2 22.1 --output-csv temperature.csv
```

### 参数说明

| 参数 | 说明 |
|------|------|
| `image_path` | 红外伪彩色图像路径（必需） |
| `max_temp` | 色条顶部的最高温度（可选，省略则自动识别） |
| `min_temp` | 色条底部的最低温度（可选，省略则自动识别） |

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
| `--use-tesseract` | 使用 Tesseract OCR（需编译时启用） |
| `--no-ocr` | 禁用自动识别（此时必须手动提供温度） |

### 示例

```bash
# 全自动模式（推荐）
./build/infrared_temp_extractor thermal.jpg --output-csv temperature.csv --output-img result.png

# 手动指定温度
./build/infrared_temp_extractor thermal.jpg 45.2 22.1 --output-csv temperature.csv

# 手动指定色条区域 + 自动识别温度
./build/infrared_temp_extractor thermal.jpg \
    --bar-x1 590 --bar-x2 620 --bar-y1 40 --bar-y2 440 \
    --output-csv temperature.csv

# 排除色条区域
./build/infrared_temp_extractor thermal.jpg --exclude-bar --output-csv temperature.csv
```

## 输出说明

### CSV 输出
每行对应图像的一行像素，每列对应一个像素，值为该像素的温度。可用 Python/MATLAB 等工具进一步分析。

### 图像输出
输出一张带有颜色条和温度标注的 JET 伪彩色热图。

### 控制台输出
程序会输出：
- 图像尺寸
- 检测到的色条区域
- 自动识别到的温度范围
- 计算进度
- 温度范围统计
- 9 个采样点的温度值

## 测试

```bash
# 生成测试图像
pip install opencv-python-headless numpy
python3 tests/generate_test_image.py

# 全自动模式运行
./build/infrared_temp_extractor tests/test_infrared.png \
    --output-csv tests/output_temp.csv --output-img tests/output_heatmap.png

# 验证精度（使用手动温度模式作为基准）
./build/infrared_temp_extractor tests/test_infrared.png 50.0 20.0 \
    --output-csv tests/output_temp_manual.csv
python3 tests/verify_accuracy.py tests/output_temp_manual.csv
```

## 内置数字识别器技术细节

内置识别器的工作流程：

1. **图像预处理**：灰度化、放大（小文字自动放大 2-4 倍）
2. **多种二值化**：Otsu 阈值、自适应阈值、固定阈值，正反相共 6 种方案
3. **连通域分析**：`cv::connectedComponentsWithStats` 提取字符边界框
4. **字符分类**：基于以下特征判断 0-9：
   - 内部孔洞数（`cv::findContours` 的层级关系）
   - 宽高比
   - 像素密度
   - 上/中/下区域像素分布
   - 孔洞位置（顶部/底部）
   - 水平/垂直投影
5. **小数点检测**：通过尺寸和位置关系识别

识别规则概览：
| 特征 | 数字 |
|------|------|
| 2 个孔洞 | 8 |
| 1 个孔洞 + 居中 | 0 |
| 1 个孔洞 + 顶部 | 9 |
| 1 个孔洞 + 底部 | 6 |
| 1 个孔洞 + 特殊形态 | 4 |
| 无孔洞 + 极窄 | 1 |
| 无孔洞 + 底部行最强 | 2 |
| 无孔洞 + 右侧强左侧弱 | 3 |
| 无孔洞 + 左上强右下强 | 5 |
| 无孔洞 + 顶部行最强 | 7 |

## 注意事项

- 本工具假设色条位于图像右侧，且颜色从上（高温）到下（低温）渐变
- 温度标注文字应位于色条顶部和底部附近
- 自动检测适用于大多数标准红外图像格式
- 如果自动识别失败，使用 `--bar-x1/x2/y1/y2` 手动指定色条区域
- 如果温度识别不准确，直接通过命令行参数提供最大/最小温度
- 建议使用无损格式（PNG）的红外图像以获得最佳精度
