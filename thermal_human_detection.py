"""
远红外图像人体检测与 Mask 生成模块

支持两种检测模式：
  - 'yolo'  : 使用 YOLOv8 深度学习模型（精度高，需要 ultralytics）
  - 'hog'   : 使用 OpenCV HOG + 传统热区分割（无需额外模型权重）

输出：
  - 二值 mask 图像（uint8，255=人体区域，0=背景）
  - 每个人体区域的温度统计信息（需提供原始温度数据）
"""

from __future__ import annotations

import argparse
import json
import logging
import os
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import List, Optional, Tuple

import cv2
import numpy as np

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# 数据结构
# ---------------------------------------------------------------------------

@dataclass
class DetectionResult:
    """单张图像的检测结果"""
    image_path: str
    mask: np.ndarray                     # H×W uint8，255=人体
    boxes: List[Tuple[int, int, int, int]] = field(default_factory=list)  # (x1,y1,x2,y2)
    scores: List[float] = field(default_factory=list)
    person_count: int = 0


@dataclass
class TempStats:
    """单个人体区域的温度统计"""
    person_id: int
    pixel_count: int
    temp_min: float
    temp_max: float
    temp_mean: float
    temp_std: float
    temp_median: float


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def load_thermal_image(image_path: str) -> Tuple[np.ndarray, np.ndarray]:
    """
    加载远红外图像，同时返回：
      - 用于检测的 uint8 灰度图（0-255）
      - 原始数组（用于温度统计，可能是 uint16 或 float32）

    支持格式：
      - 8bit 灰度 PNG/BMP/JPEG（直接使用）
      - 16bit 单通道 PNG（线性映射到 8bit 用于显示，保留原始用于统计）
      - 标准彩色图（先转灰度）
    """
    path = Path(image_path)
    if not path.exists():
        raise FileNotFoundError(f"图像文件不存在: {image_path}")

    # 先尝试 16bit 读取
    raw = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if raw is None:
        raise ValueError(f"无法读取图像: {image_path}")

    if raw.ndim == 3:
        # 彩色图转灰度
        gray_raw = cv2.cvtColor(raw, cv2.COLOR_BGR2GRAY)
    else:
        gray_raw = raw

    # 归一化到 uint8 用于检测
    if gray_raw.dtype == np.uint16:
        gray8 = cv2.normalize(gray_raw, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    elif gray_raw.dtype == np.float32 or gray_raw.dtype == np.float64:
        gray8 = cv2.normalize(gray_raw, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    else:
        gray8 = gray_raw.astype(np.uint8)

    return gray8, gray_raw


def enhance_thermal(gray8: np.ndarray) -> np.ndarray:
    """对热成像灰度图做自适应直方图均衡，增强人体轮廓"""
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    return clahe.apply(gray8)


# ---------------------------------------------------------------------------
# 检测器
# ---------------------------------------------------------------------------

class YOLODetector:
    """基于 YOLOv8 的人体检测器"""

    def __init__(
        self,
        model_path: str = "yolov8n.pt",
        conf_threshold: float = 0.35,
        iou_threshold: float = 0.45,
        device: str = "cpu",
    ):
        try:
            from ultralytics import YOLO
        except ImportError:
            raise ImportError(
                "ultralytics 未安装，请运行: pip install ultralytics"
            )
        logger.info(f"加载 YOLO 模型: {model_path}")
        self.model = YOLO(model_path)
        self.conf = conf_threshold
        self.iou = iou_threshold
        self.device = device

    def detect(self, gray8: np.ndarray) -> Tuple[List, List]:
        """
        输入 uint8 灰度图，输出 (boxes, scores)
        boxes: [(x1,y1,x2,y2), ...]  像素坐标
        """
        # YOLO 期望 BGR 3 通道
        bgr = cv2.cvtColor(gray8, cv2.COLOR_GRAY2BGR)
        results = self.model(
            bgr,
            conf=self.conf,
            iou=self.iou,
            classes=[0],           # class 0 = person
            device=self.device,
            verbose=False,
        )
        boxes, scores = [], []
        for r in results:
            for box in r.boxes:
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy().astype(int)
                boxes.append((x1, y1, x2, y2))
                scores.append(float(box.conf[0]))
        return boxes, scores


class HOGDetector:
    """基于 HOG + OpenCV 的行人检测器（不依赖外部模型权重）"""

    def __init__(
        self,
        win_stride: Tuple[int, int] = (8, 8),
        scale: float = 1.05,
        hit_threshold: float = 0.0,
        padding: Tuple[int, int] = (8, 8),
    ):
        self.hog = cv2.HOGDescriptor()
        self.hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
        self.win_stride = win_stride
        self.scale = scale
        self.hit_threshold = hit_threshold
        self.padding = padding

    def detect(self, gray8: np.ndarray) -> Tuple[List, List]:
        rects, weights = self.hog.detectMultiScale(
            gray8,
            winStride=self.win_stride,
            padding=self.padding,
            scale=self.scale,
            hitThreshold=self.hit_threshold,
        )
        boxes, scores = [], []
        if len(rects):
            # NMS（使用 groupRectangles 变体）
            rects_merged, weights_merged = cv2.groupRectangles(
                [r.tolist() for r in rects] * 2,
                1,
                0.2,
            )
            for i, (x, y, w, h) in enumerate(rects_merged):
                boxes.append((x, y, x + w, y + h))
                scores.append(float(weights_merged[i]) if i < len(weights_merged) else 1.0)
        return boxes, scores


class ThermalThresholdDetector:
    """
    基于热区分割的人体检测器（备用方案）

    算法：
      1. CLAHE 增强
      2. Otsu/自适应阈值 → 取高亮热区
      3. 形态学操作填充空洞
      4. 连通域过滤（面积、宽高比）→ 候选人体框
    """

    def __init__(
        self,
        min_area: int = 800,
        max_area: int = 80000,
        min_aspect: float = 0.2,
        max_aspect: float = 1.5,
        percentile_threshold: float = 75.0,
    ):
        self.min_area = min_area
        self.max_area = max_area
        self.min_aspect = min_aspect
        self.max_aspect = max_aspect
        self.percentile_threshold = percentile_threshold

    def detect(self, gray8: np.ndarray) -> Tuple[List, List]:
        enhanced = enhance_thermal(gray8)

        # 取热区（假设人体比背景亮）
        thresh_val = np.percentile(enhanced, self.percentile_threshold)
        _, binary = cv2.threshold(
            enhanced, int(thresh_val), 255, cv2.THRESH_BINARY
        )

        # 形态学：先腐蚀去噪，再膨胀填充
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel, iterations=2)
        binary = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel, iterations=3)

        num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(
            binary, connectivity=8
        )

        boxes, scores = [], []
        for i in range(1, num_labels):  # 0 是背景
            area = stats[i, cv2.CC_STAT_AREA]
            x = stats[i, cv2.CC_STAT_LEFT]
            y = stats[i, cv2.CC_STAT_TOP]
            w = stats[i, cv2.CC_STAT_WIDTH]
            h = stats[i, cv2.CC_STAT_HEIGHT]

            if area < self.min_area or area > self.max_area:
                continue
            aspect = w / max(h, 1)
            if aspect < self.min_aspect or aspect > self.max_aspect:
                continue

            boxes.append((x, y, x + w, y + h))
            scores.append(float(area) / (gray8.shape[0] * gray8.shape[1]))

        return boxes, scores


# ---------------------------------------------------------------------------
# Mask 生成
# ---------------------------------------------------------------------------

def boxes_to_mask(
    boxes: List[Tuple[int, int, int, int]],
    shape: Tuple[int, int],
    use_grabcut: bool = False,
    gray8: Optional[np.ndarray] = None,
) -> np.ndarray:
    """
    将检测框转换为二值 mask。

    参数：
        boxes      : [(x1,y1,x2,y2), ...]
        shape      : (H, W)
        use_grabcut: 是否用 GrabCut 精化轮廓（需要 gray8）
        gray8      : 原始灰度图（use_grabcut=True 时必须提供）

    返回：
        uint8 mask，人体区域=255，背景=0
    """
    mask = np.zeros(shape, dtype=np.uint8)
    for (x1, y1, x2, y2) in boxes:
        x1 = max(0, x1)
        y1 = max(0, y1)
        x2 = min(shape[1], x2)
        y2 = min(shape[0], y2)

        if use_grabcut and gray8 is not None and (x2 - x1) > 10 and (y2 - y1) > 10:
            region_mask = _grabcut_refine(gray8, (x1, y1, x2, y2), shape)
            mask = cv2.bitwise_or(mask, region_mask)
        else:
            mask[y1:y2, x1:x2] = 255

    return mask


def _grabcut_refine(
    gray8: np.ndarray,
    box: Tuple[int, int, int, int],
    shape: Tuple[int, int],
    iter_count: int = 5,
) -> np.ndarray:
    """用 GrabCut 在 bbox 内精化人体轮廓"""
    bgr = cv2.cvtColor(gray8, cv2.COLOR_GRAY2BGR)
    x1, y1, x2, y2 = box
    rect = (x1, y1, x2 - x1, y2 - y1)

    gc_mask = np.zeros(shape, dtype=np.uint8)
    bgd_model = np.zeros((1, 65), np.float64)
    fgd_model = np.zeros((1, 65), np.float64)

    try:
        cv2.grabCut(bgr, gc_mask, rect, bgd_model, fgd_model, iter_count, cv2.GC_INIT_WITH_RECT)
        fg_mask = np.where(
            (gc_mask == cv2.GC_FGD) | (gc_mask == cv2.GC_PR_FGD), 255, 0
        ).astype(np.uint8)
    except cv2.error as e:
        logger.warning(f"GrabCut 失败，回退到矩形 mask: {e}")
        fg_mask = np.zeros(shape, dtype=np.uint8)
        fg_mask[y1:y2, x1:x2] = 255

    return fg_mask


# ---------------------------------------------------------------------------
# 主检测流水线
# ---------------------------------------------------------------------------

def detect_humans(
    image_path: str,
    mode: str = "yolo",
    model_path: str = "yolov8n.pt",
    conf_threshold: float = 0.35,
    use_grabcut: bool = False,
    fallback_to_threshold: bool = True,
    device: str = "cpu",
) -> DetectionResult:
    """
    端到端人体检测流水线。

    参数：
        image_path           : 远红外图像路径
        mode                 : 'yolo' | 'hog' | 'threshold'
        model_path           : YOLO 模型路径（仅 mode='yolo' 时使用）
        conf_threshold       : 置信度阈值
        use_grabcut          : 是否用 GrabCut 精化 mask
        fallback_to_threshold: YOLO/HOG 无检测结果时自动退回阈值法
        device               : 'cpu' | 'cuda' | '0'

    返回：
        DetectionResult
    """
    gray8, _ = load_thermal_image(image_path)
    h, w = gray8.shape[:2]
    enhanced = enhance_thermal(gray8)

    boxes, scores = [], []

    if mode == "yolo":
        try:
            detector = YOLODetector(model_path, conf_threshold, device=device)
            boxes, scores = detector.detect(enhanced)
        except ImportError as e:
            logger.warning(f"YOLO 不可用，切换到 HOG 模式: {e}")
            mode = "hog"

    if mode == "hog":
        detector = HOGDetector()
        boxes, scores = detector.detect(enhanced)

    if mode == "threshold":
        detector = ThermalThresholdDetector()
        boxes, scores = detector.detect(enhanced)

    # 回退策略
    if fallback_to_threshold and len(boxes) == 0 and mode != "threshold":
        logger.info("主检测器无结果，启用热区阈值分割备用方案")
        detector = ThermalThresholdDetector()
        boxes, scores = detector.detect(enhanced)

    mask = boxes_to_mask(boxes, (h, w), use_grabcut=use_grabcut, gray8=enhanced)

    result = DetectionResult(
        image_path=image_path,
        mask=mask,
        boxes=boxes,
        scores=scores,
        person_count=len(boxes),
    )
    logger.info(f"检测完成: 发现 {result.person_count} 个人体区域")
    return result


# ---------------------------------------------------------------------------
# 温度统计（单独模块，可独立调用）
# ---------------------------------------------------------------------------

def compute_temperature_stats(
    mask: np.ndarray,
    raw: np.ndarray,
    scale_factor: float = 0.04,
    offset: float = -273.15,
    individual: bool = True,
) -> List[TempStats]:
    """
    基于 mask 对原始热图像数据进行温度统计。

    参数：
        mask         : uint8 二值 mask（255=人体）
        raw          : 原始温度数组（uint16 DN 值或已是开尔文/摄氏度浮点）
        scale_factor : DN → 开尔文换算系数（如 FLIR: 0.04 K/DN）
        offset       : 开尔文 → 摄氏度偏移（-273.15）
        individual   : True=对每个连通域分别统计；False=整体统计

    返回：
        List[TempStats]

    注意：
        对于不同型号的热像仪，scale_factor 和 offset 需要根据实际参数调整。
        若 raw 已是浮点摄氏度，设 scale_factor=1.0, offset=0.0。
    """
    # 温度换算
    if np.issubdtype(raw.dtype, np.integer):
        temp = raw.astype(np.float32) * scale_factor + offset
    else:
        temp = raw.astype(np.float32) * scale_factor + offset

    if not individual:
        pixels = temp[mask == 255]
        if len(pixels) == 0:
            return []
        return [TempStats(
            person_id=0,
            pixel_count=int(len(pixels)),
            temp_min=float(np.min(pixels)),
            temp_max=float(np.max(pixels)),
            temp_mean=float(np.mean(pixels)),
            temp_std=float(np.std(pixels)),
            temp_median=float(np.median(pixels)),
        )]

    # 对每个连通域分别统计
    num_labels, labels = cv2.connectedComponents(mask, connectivity=8)
    stats_list: List[TempStats] = []
    for i in range(1, num_labels):
        comp_mask = (labels == i)
        pixels = temp[comp_mask]
        if len(pixels) == 0:
            continue
        stats_list.append(TempStats(
            person_id=i,
            pixel_count=int(len(pixels)),
            temp_min=float(np.min(pixels)),
            temp_max=float(np.max(pixels)),
            temp_mean=float(np.mean(pixels)),
            temp_std=float(np.std(pixels)),
            temp_median=float(np.median(pixels)),
        ))

    return stats_list


# ---------------------------------------------------------------------------
# 可视化工具
# ---------------------------------------------------------------------------

def visualize_results(
    gray8: np.ndarray,
    result: DetectionResult,
    stats_list: Optional[List[TempStats]] = None,
    output_path: Optional[str] = None,
) -> np.ndarray:
    """
    在灰度图上叠加检测框、mask 和温度信息，生成可视化图像。

    返回 BGR uint8 图像。
    """
    vis = cv2.cvtColor(gray8, cv2.COLOR_GRAY2BGR)

    # 半透明 mask 叠加（绿色）
    overlay = vis.copy()
    overlay[result.mask == 255] = (0, 200, 0)
    cv2.addWeighted(overlay, 0.35, vis, 0.65, 0, vis)

    # 绘制检测框和标签
    for idx, ((x1, y1, x2, y2), score) in enumerate(
        zip(result.boxes, result.scores), start=1
    ):
        cv2.rectangle(vis, (x1, y1), (x2, y2), (0, 255, 255), 2)
        label = f"P{idx} {score:.2f}"
        if stats_list and idx <= len(stats_list):
            s = stats_list[idx - 1]
            label += f" {s.temp_mean:.1f}°C"
        cv2.putText(
            vis, label,
            (x1, max(y1 - 6, 12)),
            cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 1,
            cv2.LINE_AA,
        )

    # 右上角摘要
    summary = f"Persons: {result.person_count}"
    cv2.putText(vis, summary, (10, 24), cv2.FONT_HERSHEY_SIMPLEX,
                0.7, (255, 255, 0), 2, cv2.LINE_AA)

    if output_path:
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        cv2.imwrite(output_path, vis)
        logger.info(f"可视化图像已保存: {output_path}")

    return vis


def save_mask(mask: np.ndarray, output_path: str) -> None:
    """保存二值 mask 图像"""
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    cv2.imwrite(output_path, mask)
    logger.info(f"Mask 已保存: {output_path}")


# ---------------------------------------------------------------------------
# CLI 入口
# ---------------------------------------------------------------------------

def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="远红外图像人体检测与温度统计工具",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("image", help="输入远红外图像路径（支持 PNG/BMP/JPEG，8bit 或 16bit）")
    p.add_argument(
        "--mode",
        choices=["yolo", "hog", "threshold"],
        default="yolo",
        help="检测模式",
    )
    p.add_argument("--model", default="yolov8n.pt", help="YOLO 模型路径")
    p.add_argument("--conf", type=float, default=0.35, help="置信度阈值（仅 YOLO）")
    p.add_argument("--device", default="cpu", help="推理设备（cpu/cuda/0）")
    p.add_argument("--grabcut", action="store_true", help="使用 GrabCut 精化 mask 轮廓")
    p.add_argument(
        "--output-dir", default="output", help="结果输出目录"
    )
    p.add_argument(
        "--scale-factor",
        type=float,
        default=0.04,
        help="温度换算系数（DN → 开尔文）",
    )
    p.add_argument(
        "--temp-offset",
        type=float,
        default=-273.15,
        help="温度偏移（开尔文 → 摄氏度）",
    )
    p.add_argument(
        "--no-visualize", action="store_true", help="不生成可视化图像"
    )
    p.add_argument(
        "--stats-json", action="store_true", help="将温度统计结果输出为 JSON 文件"
    )
    return p


def run_pipeline(args: argparse.Namespace) -> None:
    stem = Path(args.image).stem
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # 检测
    result = detect_humans(
        image_path=args.image,
        mode=args.mode,
        model_path=args.model,
        conf_threshold=args.conf,
        use_grabcut=args.grabcut,
        device=args.device,
    )

    # 保存 mask
    mask_path = out_dir / f"{stem}_mask.png"
    save_mask(result.mask, str(mask_path))

    # 温度统计
    _, raw = load_thermal_image(args.image)
    stats_list = compute_temperature_stats(
        result.mask, raw,
        scale_factor=args.scale_factor,
        offset=args.temp_offset,
    )

    # 打印统计
    if stats_list:
        print("\n========== 温度统计 ==========")
        for s in stats_list:
            print(
                f"  人体 #{s.person_id:2d} | 像素数: {s.pixel_count:6d} | "
                f"最低: {s.temp_min:6.2f}°C | 最高: {s.temp_max:6.2f}°C | "
                f"均值: {s.temp_mean:6.2f}°C | 标准差: {s.temp_std:5.2f} | "
                f"中位数: {s.temp_median:6.2f}°C"
            )
        print("================================\n")
    else:
        print("未检测到人体，或 mask 为空。")

    # 可选：保存 JSON
    if args.stats_json:
        json_path = out_dir / f"{stem}_stats.json"
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump([asdict(s) for s in stats_list], f, ensure_ascii=False, indent=2)
        logger.info(f"统计结果已保存: {json_path}")

    # 可视化
    if not args.no_visualize:
        gray8, _ = load_thermal_image(args.image)
        vis_path = str(out_dir / f"{stem}_vis.png")
        visualize_results(gray8, result, stats_list, vis_path)


def main() -> None:
    parser = build_argparser()
    args = parser.parse_args()
    run_pipeline(args)


if __name__ == "__main__":
    main()
