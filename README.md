# Temperature Chart Generator

一个基于 C++ 的温度数据图表生成程序。读取 CSV 格式的温度数据，生成交互式 HTML 图表（基于 Plotly.js），支持 **2D 曲线图** 和 **3D 曲面图** 等多种可视化方式。

## 功能特性

- **2D 曲线图**：折线图、面积图、柱状图
- **3D 曲面图**：三维曲面、热力图、等高线图
- **综合仪表盘**：将曲线和曲面整合在同一页面
- **统计摘要**：自动计算平均值、最大值、最小值
- **交互式图表**：缩放、悬停查看数值、拖拽旋转3D视图
- **暗色主题**：精美的渐变暗色 UI 风格
- **纯 C++17**：无需安装 Python 或其他运行时依赖

## 项目结构

```
├── CMakeLists.txt            # 构建配置
├── include/
│   ├── temperature_data.h    # 温度数据结构定义
│   ├── csv_reader.h          # CSV 读取器头文件
│   └── chart_generator.h     # 图表生成器头文件
├── src/
│   ├── main.cpp              # 主程序入口
│   ├── csv_reader.cpp        # CSV 文件解析
│   └── chart_generator.cpp   # HTML/Plotly 图表生成
└── data/
    ├── temperature_timeseries.csv   # 示例时间序列数据
    └── temperature_surface.csv      # 示例曲面数据
```

## 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

> 需要支持 C++17 的编译器（GCC 8+, Clang 7+, MSVC 19.14+）

## 使用方法

### 生成 Demo 图表

```bash
./build/tempcharts demo
```

会在 `output/` 目录下生成 4 个 HTML 文件，用浏览器打开即可查看。

### 从 CSV 生成曲线图

```bash
./build/tempcharts line data/temperature_timeseries.csv my_chart.html
```

CSV 格式要求：

```csv
Time, Series1, Series2, ...
2024-01-01, 15.3, 20.1, ...
2024-02-01, 16.1, 21.5, ...
```

### 从 CSV 生成曲面图

```bash
./build/tempcharts surface data/temperature_surface.csv surface.html
```

CSV 格式要求：

```csv
Label, Y1, Y2, Y3, ...
X1, z11, z12, z13, ...
X2, z21, z22, z23, ...
```

## 示例数据说明

- **temperature_timeseries.csv**：中国主要城市（北京、上海、广州、哈尔滨）2024 年 1-12 月平均气温
- **temperature_surface.csv**：10 个中国城市在一天中 8 个时间点的温度分布

## 生成的图表类型

| 模式 | 图表 | 说明 |
|------|------|------|
| `line` | 折线图 | 多条曲线叠加，支持悬停查看 |
| `line` | 面积图 | 半透明填充面积 |
| `line` | 柱状图 | 分组柱形对比 |
| `surface` | 3D 曲面 | 可旋转/缩放的三维温度曲面 |
| `surface` | 热力图 | 二维热力色彩映射 |
| `surface` | 等高线 | 带标注的等高线图 |
| `demo` | 数学曲面 | sinc 函数生成的数学演示曲面 |

## 技术栈

- **C++17** — 核心逻辑、CSV 解析、文件生成
- **Plotly.js** — 前端图表渲染（通过 CDN 加载，无需本地安装）
- **CMake** — 跨平台构建系统
