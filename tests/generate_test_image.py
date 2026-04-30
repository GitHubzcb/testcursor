"""Generate a synthetic infrared pseudo-color test image with a known color bar."""
import numpy as np
import cv2
import os

H, W = 480, 640
BAR_WIDTH = 30
BAR_LEFT = W - 80
BAR_RIGHT = BAR_LEFT + BAR_WIDTH
BAR_TOP = 50
BAR_BOTTOM = H - 50

MIN_TEMP = 20.0
MAX_TEMP = 50.0

image = np.zeros((H, W, 3), dtype=np.uint8)

# Create a temperature field: a radial gradient centered at (W/3, H/2)
cx, cy = W // 3, H // 2
yy, xx = np.mgrid[0:H, 0:W]
dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
max_dist = np.sqrt(cx**2 + cy**2)
temp_field = MAX_TEMP - (dist / max_dist) * (MAX_TEMP - MIN_TEMP)
temp_field = np.clip(temp_field, MIN_TEMP, MAX_TEMP)

# Map temperature to [0, 255] and apply JET colormap
norm = ((temp_field - MIN_TEMP) / (MAX_TEMP - MIN_TEMP) * 255).astype(np.uint8)
colored = cv2.applyColorMap(norm, cv2.COLORMAP_JET)
image[:, :BAR_LEFT - 20] = colored[:, :BAR_LEFT - 20]

# Draw the color bar on the right side
for y in range(BAR_TOP, BAR_BOTTOM + 1):
    ratio = (y - BAR_TOP) / (BAR_BOTTOM - BAR_TOP)
    val = int(255 * (1.0 - ratio))
    pix = cv2.applyColorMap(np.array([[val]], dtype=np.uint8), cv2.COLORMAP_JET)
    color = pix[0, 0]
    image[y, BAR_LEFT:BAR_RIGHT] = color

# Add text for max/min temp with larger font and thicker stroke for clearer OCR
# Use FONT_HERSHEY_DUPLEX for better digit rendering
font = cv2.FONT_HERSHEY_DUPLEX
font_scale = 0.8
thickness = 2

max_text = f"{MAX_TEMP:.1f}"
min_text = f"{MIN_TEMP:.1f}"

# Position text to the right of bar, vertically centered with bar top/bottom
(tw, th), _ = cv2.getTextSize(max_text, font, font_scale, thickness)
cv2.putText(image, max_text, (BAR_RIGHT + 8, BAR_TOP + th // 2 + 2),
            font, font_scale, (255, 255, 255), thickness)

(tw, th), _ = cv2.getTextSize(min_text, font, font_scale, thickness)
cv2.putText(image, min_text, (BAR_RIGHT + 8, BAR_BOTTOM + th // 2 + 2),
            font, font_scale, (255, 255, 255), thickness)

out_dir = os.path.dirname(os.path.abspath(__file__))
out_path = os.path.join(out_dir, "test_infrared.png")
cv2.imwrite(out_path, image)
print(f"Test image saved to {out_path} ({W}x{H})")
print(f"Color bar region: x=[{BAR_LEFT}, {BAR_RIGHT}], y=[{BAR_TOP}, {BAR_BOTTOM}]")
print(f"Temperature: max={MAX_TEMP}, min={MIN_TEMP}")

# Ground truth temperatures
center_temp = temp_field[H // 2, W // 2]
print(f"Center pixel temperature (ground truth): {center_temp:.2f}")
print(f"Top-left quarter temperature (ground truth): {temp_field[H//4, W//4]:.2f}")
