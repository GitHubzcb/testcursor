"""
单元测试与集成测试，使用合成热成像图像验证核心功能。
运行：pytest test_thermal_detection.py -v
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import cv2
import numpy as np
import pytest

from thermal_human_detection import (
    ThermalThresholdDetector,
    HOGDetector,
    boxes_to_mask,
    compute_temperature_stats,
    detect_humans,
    load_thermal_image,
    save_mask,
    visualize_results,
    DetectionResult,
    TempStats,
)


# ---------------------------------------------------------------------------
# 测试夹具
# ---------------------------------------------------------------------------

def make_synthetic_thermal(
    h: int = 240,
    w: int = 320,
    num_persons: int = 2,
    use_uint16: bool = False,
) -> np.ndarray:
    """生成合成热成像图像：背景低亮度，人体区域高亮度"""
    img = np.full((h, w), 60, dtype=np.uint16 if use_uint16 else np.uint8)
    # 背景噪声
    noise = np.random.randint(0, 15, (h, w), dtype=np.uint16 if use_uint16 else np.uint8)
    img = (img.astype(np.int32) + noise).clip(0, 255 if not use_uint16 else 65535)
    img = img.astype(np.uint16 if use_uint16 else np.uint8)

    # 放置模拟人体热斑（竖向矩形，高亮）
    step = w // (num_persons + 1)
    for i in range(num_persons):
        cx = step * (i + 1)
        cy = h // 2
        x1, y1 = cx - 20, cy - 45
        x2, y2 = cx + 20, cy + 45
        x1, y1 = max(0, x1), max(0, y1)
        x2, y2 = min(w, x2), min(h, y2)
        val = 40000 if use_uint16 else 200
        img[y1:y2, x1:x2] = val

    return img.astype(np.uint16 if use_uint16 else np.uint8)


@pytest.fixture
def synthetic_image_path(tmp_path) -> str:
    """保存 8bit 合成热图并返回路径"""
    img = make_synthetic_thermal(h=240, w=320, num_persons=2)
    path = tmp_path / "synth_thermal.png"
    cv2.imwrite(str(path), img)
    return str(path)


@pytest.fixture
def synthetic_image_16bit_path(tmp_path) -> str:
    """保存 16bit 合成热图并返回路径"""
    img = make_synthetic_thermal(h=240, w=320, num_persons=1, use_uint16=True)
    path = tmp_path / "synth_thermal_16bit.png"
    cv2.imwrite(str(path), img)
    return str(path)


# ---------------------------------------------------------------------------
# load_thermal_image
# ---------------------------------------------------------------------------

class TestLoadThermalImage:
    def test_load_8bit(self, synthetic_image_path):
        gray8, raw = load_thermal_image(synthetic_image_path)
        assert gray8.dtype == np.uint8
        assert gray8.ndim == 2

    def test_load_16bit(self, synthetic_image_16bit_path):
        gray8, raw = load_thermal_image(synthetic_image_16bit_path)
        assert gray8.dtype == np.uint8
        assert raw.dtype == np.uint16
        # 归一化后应在 0-255 范围
        assert gray8.max() <= 255

    def test_file_not_found(self):
        with pytest.raises(FileNotFoundError):
            load_thermal_image("/nonexistent/path/image.png")


# ---------------------------------------------------------------------------
# ThermalThresholdDetector
# ---------------------------------------------------------------------------

class TestThermalThresholdDetector:
    def test_detects_hot_regions(self):
        img = make_synthetic_thermal(h=240, w=320, num_persons=2)
        detector = ThermalThresholdDetector(min_area=100)
        boxes, scores = detector.detect(img)
        # 应该能检测到至少一个区域
        assert len(boxes) >= 1

    def test_no_false_positives_on_uniform(self):
        img = np.full((240, 320), 128, dtype=np.uint8)
        detector = ThermalThresholdDetector(min_area=500)
        boxes, _ = detector.detect(img)
        # 均匀图像不应有太多候选区
        assert len(boxes) <= 3


# ---------------------------------------------------------------------------
# boxes_to_mask
# ---------------------------------------------------------------------------

class TestBoxesToMask:
    def test_mask_values(self):
        boxes = [(10, 10, 50, 80)]
        mask = boxes_to_mask(boxes, (120, 160))
        assert mask.dtype == np.uint8
        assert mask[30, 30] == 255
        assert mask[0, 0] == 0

    def test_empty_boxes(self):
        mask = boxes_to_mask([], (100, 100))
        assert mask.sum() == 0

    def test_multiple_boxes(self):
        boxes = [(0, 0, 30, 30), (70, 70, 100, 100)]
        mask = boxes_to_mask(boxes, (120, 120))
        assert mask[10, 10] == 255
        assert mask[80, 80] == 255
        assert mask[50, 50] == 0

    def test_out_of_bounds_clipped(self):
        boxes = [(-10, -10, 200, 200)]
        mask = boxes_to_mask(boxes, (100, 100))
        assert mask[0, 0] == 255
        assert mask[99, 99] == 255


# ---------------------------------------------------------------------------
# compute_temperature_stats
# ---------------------------------------------------------------------------

class TestComputeTemperatureStats:
    def test_basic_stats(self):
        raw = np.arange(100, dtype=np.uint16).reshape(10, 10)
        mask = np.zeros((10, 10), dtype=np.uint8)
        mask[2:8, 2:8] = 255

        stats = compute_temperature_stats(mask, raw, scale_factor=0.04, offset=-273.15)
        assert len(stats) >= 1
        s = stats[0]
        assert s.pixel_count > 0
        assert s.temp_min <= s.temp_mean <= s.temp_max
        assert s.temp_std >= 0

    def test_empty_mask_returns_empty(self):
        raw = np.zeros((50, 50), dtype=np.uint16)
        mask = np.zeros((50, 50), dtype=np.uint8)
        stats = compute_temperature_stats(mask, raw)
        assert stats == []

    def test_multiple_persons(self):
        raw = np.zeros((100, 200), dtype=np.uint16)
        mask = np.zeros((100, 200), dtype=np.uint8)
        mask[10:50, 10:60] = 255    # 人体 1
        mask[10:50, 140:190] = 255  # 人体 2

        raw[10:50, 10:60] = 1000
        raw[10:50, 140:190] = 2000

        stats = compute_temperature_stats(mask, raw, scale_factor=0.04, offset=-273.15, individual=True)
        assert len(stats) == 2
        # 第二个人体温度应高于第一个
        means = [s.temp_mean for s in stats]
        assert max(means) > min(means)

    def test_float_raw(self):
        raw = np.random.rand(50, 50).astype(np.float32) * 100
        mask = np.zeros((50, 50), dtype=np.uint8)
        mask[10:40, 10:40] = 255
        stats = compute_temperature_stats(mask, raw, scale_factor=1.0, offset=0.0)
        assert len(stats) >= 1


# ---------------------------------------------------------------------------
# detect_humans (threshold 模式，无需模型)
# ---------------------------------------------------------------------------

class TestDetectHumans:
    def test_threshold_mode(self, synthetic_image_path):
        result = detect_humans(synthetic_image_path, mode="threshold")
        assert isinstance(result, DetectionResult)
        assert result.mask.dtype == np.uint8
        assert result.mask.shape[0] > 0
        assert result.person_count >= 0

    def test_hog_mode_runs(self, synthetic_image_path):
        result = detect_humans(synthetic_image_path, mode="hog")
        assert isinstance(result, DetectionResult)

    def test_result_mask_shape_matches_image(self, synthetic_image_path):
        gray8, _ = load_thermal_image(synthetic_image_path)
        result = detect_humans(synthetic_image_path, mode="threshold")
        assert result.mask.shape == gray8.shape


# ---------------------------------------------------------------------------
# save_mask & visualize_results
# ---------------------------------------------------------------------------

class TestSaveMaskAndVisualize:
    def test_save_mask(self, tmp_path, synthetic_image_path):
        result = detect_humans(synthetic_image_path, mode="threshold")
        mask_path = str(tmp_path / "test_mask.png")
        save_mask(result.mask, mask_path)
        assert Path(mask_path).exists()
        loaded = cv2.imread(mask_path, cv2.IMREAD_GRAYSCALE)
        assert loaded is not None
        assert loaded.shape == result.mask.shape

    def test_visualize_no_crash(self, synthetic_image_path):
        gray8, _ = load_thermal_image(synthetic_image_path)
        result = detect_humans(synthetic_image_path, mode="threshold")
        vis = visualize_results(gray8, result)
        assert vis.shape[2] == 3   # BGR

    def test_visualize_saves_file(self, tmp_path, synthetic_image_path):
        gray8, _ = load_thermal_image(synthetic_image_path)
        result = detect_humans(synthetic_image_path, mode="threshold")
        out_path = str(tmp_path / "vis.png")
        visualize_results(gray8, result, output_path=out_path)
        assert Path(out_path).exists()
