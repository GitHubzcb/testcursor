"""Verify the accuracy of the temperature extraction by comparing CSV output to ground truth."""
import numpy as np
import csv
import sys

H, W = 480, 640
MIN_TEMP = 20.0
MAX_TEMP = 50.0

cx, cy = W // 3, H // 2
yy, xx = np.mgrid[0:H, 0:W]
dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
max_dist = np.sqrt(cx**2 + cy**2)
truth = MAX_TEMP - (dist / max_dist) * (MAX_TEMP - MIN_TEMP)
truth = np.clip(truth, MIN_TEMP, MAX_TEMP)

csv_path = sys.argv[1] if len(sys.argv) > 1 else "tests/output_temp.csv"
with open(csv_path) as f:
    reader = csv.reader(f)
    extracted = np.array([[float(v) for v in row] for row in reader])

# Compare only the main image area (exclude color bar region ~x=560+)
roi_w = min(560, extracted.shape[1])
truth_roi = truth[:, :roi_w]
extracted_roi = extracted[:, :roi_w]

diff = np.abs(truth_roi - extracted_roi)
print(f"Shape: truth={truth_roi.shape}, extracted={extracted_roi.shape}")
print(f"Mean Absolute Error: {diff.mean():.3f} deg")
print(f"Max Absolute Error:  {diff.max():.3f} deg")
print(f"Median Abs Error:    {np.median(diff):.3f} deg")
print(f"Errors < 1 deg:      {(diff < 1.0).mean() * 100:.1f}%")
print(f"Errors < 2 deg:      {(diff < 2.0).mean() * 100:.1f}%")

if diff.mean() < 2.0:
    print("\nVERIFICATION PASSED: Average error is within acceptable range.")
    sys.exit(0)
else:
    print("\nVERIFICATION FAILED: Average error is too large.")
    sys.exit(1)
