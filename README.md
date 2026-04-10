# Thermal Human Detector — 远红外图像人体检测与温度统计

基于 OpenCV 的 C++ 库，用于从远红外（热成像）图像中检测人体区域、生成二值 mask，并结合温度矩阵进行区域温度统计。

## 功能特点

- **人体热区检测**：通过自适应阈值分割 + 形态学处理 + 轮廓分析，从远红外灰度图中提取人体区域
- **温度阈值分割**：当提供温度矩阵时，可直接按人体温度范围（默认 30°C ~ 42°C）精确分割
- **二值 Mask 输出**：生成与原图等大的 mask（人体区域=255，背景=0），可直接用于取温区数据对应的区域
- **温度统计**：对每个人体区域计算最大/最小/平均温度及最高温度位置
- **可视化调试**：伪彩色叠加 mask + 包围框 + 温度标注

## 目录结构

```
.
├── CMakeLists.txt                  # CMake 构建文件
├── include/
│   └── thermal_human_detector.h    # 头文件（API 声明）
├── src/
│   └── thermal_human_detector.cpp  # 实现文件
├── example/
│   └── main.cpp                    # 使用示例
└── README.md
```

## 依赖

- **CMake** >= 3.14
- **OpenCV** >= 4.0（需要 core、imgproc、highgui、imgcodecs 模块）
- C++17 编译器

## 编译

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 使用

### 命令行示例

```bash
# 仅使用远红外灰度图（自适应阈值分割）
./thermal_detector_example thermal.png

# 同时提供温度矩阵（按温度范围精确分割）
./thermal_detector_example thermal.png temperature.yml
```

输出文件：
- `human_mask.png` — 人体区域二值 mask
- `detection_result.png` — 可视化检测结果

### 在代码中调用

```cpp
#include "thermal_human_detector.h"

// 方式 1：仅使用远红外灰度图
cv::Mat thermal_img = cv::imread("thermal.png", cv::IMREAD_ANYDEPTH | cv::IMREAD_GRAYSCALE);
thermal::DetectorParams params;
thermal::DetectionResult result = thermal::detect_human_thermal(thermal_img, params);

// result.full_mask 即为人体区域 mask（CV_8UC1，人体=255）
// 用 mask 提取温度数据：
cv::Mat temp_data;  // 你的温度矩阵
cv::Mat human_temp;
temp_data.copyTo(human_temp, result.full_mask);  // 只保留人体区域的温度数据

// 方式 2：结合温度矩阵进行精确检测
cv::Mat temp_matrix;  // CV_32FC1，每个像素对应真实温度 (°C)
thermal::DetectionResult result2 = thermal::detect_human_thermal(thermal_img, temp_matrix, params);

// 遍历每个人体区域的温度统计
for (const auto& region : result2.regions) {
    std::cout << "最高温度: " << region.max_temp << "°C" << std::endl;
    std::cout << "平均温度: " << region.avg_temp << "°C" << std::endl;
}
```

### 温度矩阵格式

温度矩阵通过 OpenCV FileStorage（`.yml`）格式加载，键名为 `temperature`：

```cpp
// 保存温度矩阵
cv::FileStorage fs("temperature.yml", cv::FileStorage::WRITE);
fs << "temperature" << temp_matrix;  // temp_matrix: CV_32FC1
fs.release();
```

## API 参考

### `DetectorParams` — 检测参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `temp_threshold_low` | 30.0 | 人体温度下限 (°C) |
| `temp_threshold_high` | 42.0 | 人体温度上限 (°C) |
| `morph_kernel_size` | 5 | 形态学运算核大小 |
| `min_area_ratio` | 0.002 | 最小区域占图像面积比 |
| `max_area_ratio` | 0.8 | 最大区域占图像面积比 |
| `aspect_ratio_min` | 0.2 | 最小宽高比 |
| `aspect_ratio_max` | 3.0 | 最大宽高比 |
| `use_adaptive_thresh` | true | 是否使用自适应阈值 |
| `adaptive_block_size` | 35 | 自适应阈值块大小 |
| `adaptive_C` | -8.0 | 自适应阈值常数 |

### 核心函数

- `detect_human_thermal(thermal_image, params)` — 仅灰度图检测
- `detect_human_thermal(thermal_image, temp_matrix, params)` — 结合温度矩阵检测
- `compute_temperature_stats(temp_matrix, mask, ...)` — 按 mask 统计温度
- `visualize_detection(thermal_image, result)` — 可视化结果

## 算法说明

1. **预处理**：将输入图像归一化至 8-bit 灰度，高斯模糊降噪
2. **分割**：
   - 有温度矩阵时：按温度范围阈值分割，可叠加自适应阈值
   - 无温度矩阵时：自适应高斯阈值分割（适用于人体在背景中的热对比度）
3. **形态学精炼**：闭运算填充空洞 → 开运算去除噪点 → 膨胀/腐蚀平滑边界
4. **轮廓过滤**：按面积比和宽高比筛选符合人体形态的区域
5. **统计输出**：对每个人体区域生成局部 mask 并计算温度统计量
