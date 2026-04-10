# 远红外图像人体检测与温度统计

基于远红外（热成像）图像，自动检测画面中的人体，将每个人体区域输出为二值 Mask，并结合原始温度数据对各人体区域进行精确温度统计。

---

## 功能概览

| 功能 | 说明 |
|------|------|
| 人体检测 | 支持 YOLO（深度学习）、HOG（传统）、热区阈值分割 三种模式 |
| Mask 生成 | 输出 uint8 PNG，人体区域=255，背景=0；可选 GrabCut 精化轮廓 |
| 温度统计 | 支持 16bit DN 原始值换算，输出 最小/最大/均值/标准差/中位数 温度 |
| 批量处理 | 一键处理整个目录，汇总输出 CSV + JSON + 可视化图像 |

---

## 目录结构

```
.
├── thermal_human_detection.py   # 核心模块（检测 + Mask + 温度统计）
├── thermal_batch.py             # 批量处理入口
├── test_thermal_detection.py    # 单元/集成测试
├── requirements.txt
└── README.md
```

---

## 安装

```bash
pip install -r requirements.txt
```

> 若仅使用 HOG / 热区阈值模式，可跳过 `ultralytics`：
> ```bash
> pip install "opencv-python>=4.8.0" "numpy>=1.24.0"
> ```

---

## 快速上手

### 单张图像处理

```bash
python thermal_human_detection.py path/to/thermal.png \
    --mode yolo \
    --output-dir output \
    --stats-json
```

输出文件：
- `output/thermal_mask.png`    — 人体 Mask（白色=人体，黑色=背景）
- `output/thermal_vis.png`     — 叠加可视化图像
- `output/thermal_stats.json`  — 各人体区域温度统计

### 批量处理目录

```bash
python thermal_batch.py /data/thermal_images \
    --mode yolo \
    --output-dir results
```

输出文件：
- `results/<stem>_mask.png`    — 每张图的 Mask
- `results/<stem>_vis.png`     — 每张图的可视化
- `results/<stem>_stats.json`  — 每张图的统计 JSON
- `results/summary.csv`        — 所有图像汇总 CSV

---

## 命令行参数说明

### `thermal_human_detection.py`

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `image` | — | 输入图像路径（必须） |
| `--mode` | `yolo` | 检测模式：`yolo` / `hog` / `threshold` |
| `--model` | `yolov8n.pt` | YOLO 模型路径（自动下载 nano 版本） |
| `--conf` | `0.35` | YOLO 置信度阈值 |
| `--device` | `cpu` | 推理设备（`cpu` / `cuda` / `0`） |
| `--grabcut` | False | 启用 GrabCut 精化 Mask 边缘 |
| `--output-dir` | `output` | 结果保存目录 |
| `--scale-factor` | `0.04` | DN → 开尔文换算系数（FLIR 常用值） |
| `--temp-offset` | `-273.15` | 开尔文 → 摄氏度偏移 |
| `--no-visualize` | False | 不输出可视化图像 |
| `--stats-json` | False | 保存温度统计 JSON |

---

## Python API 调用

```python
from thermal_human_detection import (
    detect_humans,
    load_thermal_image,
    compute_temperature_stats,
    save_mask,
)

# 1. 检测人体
result = detect_humans(
    image_path="thermal.png",
    mode="yolo",            # 或 "hog" / "threshold"
    conf_threshold=0.4,
    use_grabcut=False,      # True 可获得更精细的轮廓
)

print(f"检测到 {result.person_count} 人")
print(f"Mask shape: {result.mask.shape}")

# 2. 保存 Mask
save_mask(result.mask, "output/mask.png")

# 3. 温度统计（raw 为原始 16bit DN 或浮点温度数组）
_, raw = load_thermal_image("thermal.png")
stats = compute_temperature_stats(
    mask=result.mask,
    raw=raw,
    scale_factor=0.04,   # 根据相机参数调整
    offset=-273.15,
)

for s in stats:
    print(f"人体 #{s.person_id}: 均值={s.temp_mean:.2f}°C, 最高={s.temp_max:.2f}°C")
```

---

## 检测模式对比

| 模式 | 精度 | 速度 | 依赖 | 适用场景 |
|------|------|------|------|----------|
| `yolo` | 高 | 中（CPU）/ 快（GPU） | ultralytics | 标准热成像行人检测 |
| `hog` | 中 | 快 | 仅 OpenCV | 无 GPU、小尺寸图像 |
| `threshold` | 中低 | 极快 | 仅 OpenCV | 背景温度均匀、快速原型 |

> 优先推荐 `yolo` 模式；若 YOLO 无检测结果，系统自动退回 `threshold` 备用方案。

---

## 温度换算说明

热像仪原始数据通常为 16bit 无符号整数（DN 值），需按以下公式转换为摄氏度：

```
T(°C) = DN × scale_factor + offset
       = DN × 0.04 + (−273.15)    # FLIR 典型值
```

**不同型号相机请参阅产品手册获取正确参数**。若输入图像已是摄氏度浮点数，设置 `--scale-factor 1.0 --temp-offset 0.0`。

---

## 运行测试

```bash
pytest test_thermal_detection.py -v
```

---

## 常见问题

**Q: YOLO 模型在哪里下载？**  
首次运行时 `ultralytics` 会自动从官方源下载 `yolov8n.pt`（约 6 MB）。离线环境可手动下载并通过 `--model` 指定路径。

**Q: 如何使用在热成像数据集上微调的模型？**  
将微调后的权重路径传给 `--model`，检测的类别仍为 `person`（COCO class 0）。

**Q: GrabCut 精化 Mask 的效果如何？**  
GrabCut 能将 Mask 从矩形框细化到人体轮廓，但在热成像图像中人体与背景对比明显时效果有限，且会增加处理时间。实际使用中按需开启。
