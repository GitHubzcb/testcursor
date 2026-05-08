# 针体弯曲检测 (Needle Bend Detection)

基于 OpenCV 的 C++ 高精度针体弯曲检测程序。可检测弯曲的针（如注射针、缝合针等）的弯曲角度、弯曲长度和弯曲位置。

## 功能

- **弯曲角度检测**: 计算针体直线段与弯曲末端切线方向的夹角（度）
- **弯曲长度检测**: 计算弯曲段的弧长（像素）
- **弯曲位置检测**: 高精度确定弯曲起始点的坐标位置
- **可视化输出**: 在弯曲区域画直线标注，输出带有结果标注的图像

## 算法原理

1. **图像预处理**: 灰度化 → 高斯模糊去噪 → Otsu自适应阈值二值化 → 形态学操作
2. **中心线提取**: 基于亚像素精度的灰度加权中心线提取（非传统骨架化）
3. **弯曲点检测**: 融合两种方法高精度定位：
   - 累积偏差法：从直线段开始逐步检测偏离参考直线的阈值突变
   - 分段线性拟合法：穷举分割点寻找最优两段拟合
   - 二阶导数精化：在候选区间内用距离二阶导数精确定位
4. **角度/长度计算**: 使用最小二乘直线拟合计算方向，弧长积分计算弯曲长度

## 依赖

- OpenCV 4.x
- CMake 3.10+
- C++17 编译器 (GCC 9+ / Clang 10+)

## 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

如果系统使用 clang 默认编译器且缺少 C++ 标准库，可指定 GCC：
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
```

## 使用

```bash
./build/needle_bend_detect <image_path>
```

示例：
```bash
./build/needle_bend_detect images/needle.jpg
```

## 输出

程序会在终端打印检测结果，并生成两个图像文件：

- `output_result.jpg` - 带结果标注的原始图像
- `output_debug.jpg` - 二值化结果上的标注（用于调试）

### 结果说明

| 指标 | 说明 |
|------|------|
| Bend Angle | 弯曲角度（直线段方向与针尖处切线方向的夹角） |
| Bend Arc Length | 弯曲段弧长（像素） |
| Straight Length | 直线段长度（像素） |
| Bend Position | 弯曲起始点坐标 |
| Tip Position | 针尖坐标 |
| Tail Position | 针尾坐标 |

## 注意事项

- 输入图像中针体应为深色（暗色），背景应为浅色
- 针的直线段（后端）应位于图像右侧
- 对于大于2000像素的图像会自动缩放处理
- 结果坐标已转换为原始图像尺寸
