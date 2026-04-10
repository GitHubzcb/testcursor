"""
批量处理远红外图像目录，对每张图像执行人体检测与温度统计。

用法示例：
    python thermal_batch.py /path/to/thermal/images --mode yolo --output-dir results
"""

from __future__ import annotations

import argparse
import csv
import json
import logging
import os
from dataclasses import asdict
from pathlib import Path
from typing import List

from thermal_human_detection import (
    detect_humans,
    load_thermal_image,
    compute_temperature_stats,
    visualize_results,
    save_mask,
    TempStats,
)

logger = logging.getLogger(__name__)

SUPPORTED_EXTS = {".png", ".bmp", ".jpg", ".jpeg", ".tiff", ".tif"}


def collect_images(directory: str) -> List[Path]:
    d = Path(directory)
    if not d.is_dir():
        raise NotADirectoryError(f"输入路径不是目录: {directory}")
    images = sorted(
        p for p in d.rglob("*") if p.suffix.lower() in SUPPORTED_EXTS
    )
    return images


def process_directory(
    input_dir: str,
    output_dir: str,
    mode: str = "yolo",
    model_path: str = "yolov8n.pt",
    conf: float = 0.35,
    device: str = "cpu",
    grabcut: bool = False,
    scale_factor: float = 0.04,
    temp_offset: float = -273.15,
    visualize: bool = True,
    stats_json: bool = True,
) -> None:
    images = collect_images(input_dir)
    if not images:
        logger.warning(f"目录中未找到支持的图像文件: {input_dir}")
        return

    logger.info(f"共找到 {len(images)} 张图像，开始批量处理…")
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    summary_rows = []

    for i, img_path in enumerate(images, start=1):
        logger.info(f"[{i}/{len(images)}] 处理: {img_path.name}")
        try:
            result = detect_humans(
                image_path=str(img_path),
                mode=mode,
                model_path=model_path,
                conf_threshold=conf,
                use_grabcut=grabcut,
                device=device,
            )

            stem = img_path.stem
            mask_path = out_dir / f"{stem}_mask.png"
            save_mask(result.mask, str(mask_path))

            _, raw = load_thermal_image(str(img_path))
            stats_list = compute_temperature_stats(
                result.mask, raw,
                scale_factor=scale_factor,
                offset=temp_offset,
            )

            if stats_json and stats_list:
                json_path = out_dir / f"{stem}_stats.json"
                with open(json_path, "w", encoding="utf-8") as f:
                    json.dump(
                        [asdict(s) for s in stats_list], f,
                        ensure_ascii=False, indent=2,
                    )

            if visualize:
                gray8, _ = load_thermal_image(str(img_path))
                vis_path = str(out_dir / f"{stem}_vis.png")
                visualize_results(gray8, result, stats_list, vis_path)

            for s in stats_list:
                summary_rows.append({
                    "image": img_path.name,
                    "person_id": s.person_id,
                    "pixel_count": s.pixel_count,
                    "temp_min": round(s.temp_min, 3),
                    "temp_max": round(s.temp_max, 3),
                    "temp_mean": round(s.temp_mean, 3),
                    "temp_std": round(s.temp_std, 3),
                    "temp_median": round(s.temp_median, 3),
                })

        except Exception as e:
            logger.error(f"处理 {img_path.name} 时出错: {e}", exc_info=True)

    # 汇总 CSV
    if summary_rows:
        csv_path = out_dir / "summary.csv"
        fieldnames = list(summary_rows[0].keys())
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(summary_rows)
        logger.info(f"汇总 CSV 已保存: {csv_path}")

    logger.info("批量处理完成。")


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    p = argparse.ArgumentParser(
        description="批量处理远红外图像目录",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("input_dir", help="远红外图像目录")
    p.add_argument("--mode", choices=["yolo", "hog", "threshold"], default="yolo")
    p.add_argument("--model", default="yolov8n.pt")
    p.add_argument("--conf", type=float, default=0.35)
    p.add_argument("--device", default="cpu")
    p.add_argument("--grabcut", action="store_true")
    p.add_argument("--output-dir", default="results")
    p.add_argument("--scale-factor", type=float, default=0.04)
    p.add_argument("--temp-offset", type=float, default=-273.15)
    p.add_argument("--no-visualize", action="store_true")
    p.add_argument("--no-json", action="store_true")
    args = p.parse_args()

    process_directory(
        input_dir=args.input_dir,
        output_dir=args.output_dir,
        mode=args.mode,
        model_path=args.model,
        conf=args.conf,
        device=args.device,
        grabcut=args.grabcut,
        scale_factor=args.scale_factor,
        temp_offset=args.temp_offset,
        visualize=not args.no_visualize,
        stats_json=not args.no_json,
    )


if __name__ == "__main__":
    main()
