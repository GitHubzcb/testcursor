# 红外热成像人体区域分析 — 升级说明

## 本次改动摘要

### 问题背景

原版代码只向 LLM 输入每个区域的**均值温度**，无左右区分，信息过少，导致 AI 诊断报告质量低。

### 升级内容

#### 1. `BodyRegion` 枚举扩展（`ThermalAnalysis.h`）

大腿、膝盖、小腿、脚、胳膊、手从单个区域拆分为 `_L` / `_R` 左右两个独立 ID：

```
REGION_THIGH_L / REGION_THIGH_R
REGION_KNEE_L  / REGION_KNEE_R
REGION_LEG_L   / REGION_LEG_R
REGION_FOOT_L  / REGION_FOOT_R
REGION_ARM_L   / REGION_ARM_R
REGION_HAND_L  / REGION_HAND_R
```

头、颈、胸、腹、腰保持轴对称，不分侧。

#### 2. `RegionTempStat` 结构体升级

新增字段：
- `stdDev`：区域温度标准差
- `_sumTemp` / `_sumSq`：在线累积（内部使用）
- `Accumulate(t, x, y)`：单像素累积接口
- `Finalize()`：统计完成后计算均值和标准差

#### 3. `BuildBodyRegionMask` 重构

- 计算**动态人体中线 x**：上端取双肩中点 x，下端取双踝/双膝中点 x，线性插值，适应倾斜站姿
- 每行每列像素按与中线 x 的位置关系归入左/右区域
- 加入**膝盖过渡窗口**（膝关节上下各 12% 腿长），减少分割锯齿
- 手臂和手部按骨骼中点 x 判断左右

#### 4. `CalcRegionTempFromMatrix` 升级

使用 `Accumulate` + `Finalize` 模式，一次遍历即可完成均值与标准差计算。

#### 5. `BuildLLMPrompt` 新函数

输入 `std::vector<std::map<int, RegionTempStat>>`，输出完整的 LLM 提示词：

```
大腿：
  左：35.4℃（最高 36.8℃  最低 34.2℃  标准差 0.60℃）
  右：36.2℃（最高 37.1℃  最低 35.0℃  标准差 0.45℃）
  差值：0.8℃  ⚠ 左右温差偏大，建议重点关注
```

异常告警阈值（内置）：
| 指标 | 阈值 | 提示 |
|------|------|------|
| 左右均值差 | ≥ 0.8℃ | 温度不对称，可能炎症/血循异常 |
| 区域均值 | > 37℃ | 局部高温 |
| 区域均值 | < 28℃ | 局部低温 |
| 标准差 | ≥ 1.5℃ | 区域内温度分布不均匀 |

#### 6. `OnBnClickedButtonAitrain` 调用改动

见 `OnBnClickedButtonAitrain_patch.cpp` 中的补丁说明：

- `allImages` 类型由 `std::vector<std::map<CStringW, double>>` 改为 `std::vector<std::map<int, RegionTempStat>>`
- 提示词构建替换为 `BuildLLMPrompt(allImages)`

## 文件清单

| 文件 | 说明 |
|------|------|
| `ThermalAnalysis.h` | 枚举、结构体、内联函数声明 |
| `ThermalAnalysis.cpp` | `BuildBodyRegionMask`、`CalcRegionTempFromMatrix`、`BuildLLMPrompt` 实现 |
| `OnBnClickedButtonAitrain_patch.cpp` | 原对话框函数的修改补丁说明 |
