"""Generate a realistic infrared test image mimicking the user's real image.
Color bar: right edge, narrow (15px), with temperature labels on the left of bar.
Main image: simulated body thermal pattern with complex gradients.
"""
import numpy as np
import cv2

H, W = 476, 636

# Color bar parameters (matching user's description)
BAR_LEFT = 605  # x=605 (a bit before edge to fit text to the left)
BAR_RIGHT = BAR_LEFT + 15
BAR_TOP = 20
BAR_BOTTOM = 460
MIN_TEMP = 30.0
MAX_TEMP = 38.0

image = np.zeros((H, W, 3), dtype=np.uint8)

# --- Main image: simulate complex thermal scene (body-like pattern) ---
# Background: cool (purple/blue)
bg_temp = np.ones((H, W)) * 0.1  # normalized 0-1
# Body silhouette: warm
cy, cx = H // 2, W // 3
yy, xx = np.mgrid[0:H, 0:W]
# Torso
body_mask = ((xx - cx) ** 2 / (80**2) + (yy - cy) ** 2 / (180**2)) < 1
bg_temp[body_mask] = 0.6 + np.random.rand(body_mask.sum()) * 0.3
# Head
head_mask = ((xx - cx) ** 2 + (yy - cy + 150) ** 2) < 40**2
bg_temp[head_mask] = 0.8 + np.random.rand(head_mask.sum()) * 0.2
# Some random hot spots
for _ in range(20):
    rx, ry = np.random.randint(50, W-100), np.random.randint(50, H-50)
    rr = np.random.randint(10, 40)
    spot = ((xx - rx)**2 + (yy - ry)**2) < rr**2
    bg_temp[spot] = np.clip(bg_temp[spot] + np.random.rand() * 0.3, 0, 1)

# Apply JET colormap to main image
bg_norm = (np.clip(bg_temp, 0, 1) * 255).astype(np.uint8)
main_colored = cv2.applyColorMap(bg_norm, cv2.COLORMAP_JET)
image[:, :BAR_LEFT - 30] = main_colored[:, :BAR_LEFT - 30]

# Fill the area between main image and bar with dark background
image[:, BAR_LEFT - 30:BAR_LEFT] = 20  # dark strip

# --- Color bar ---
for y in range(BAR_TOP, BAR_BOTTOM + 1):
    ratio = (y - BAR_TOP) / (BAR_BOTTOM - BAR_TOP)
    val = int(255 * (1.0 - ratio))  # top=bright, bottom=dark
    pix = cv2.applyColorMap(np.array([[val]], dtype=np.uint8), cv2.COLORMAP_JET)
    color = pix[0, 0]
    image[y, BAR_LEFT:BAR_RIGHT] = color

# Fill right of bar with dark
image[:, BAR_RIGHT:] = 20

# --- Temperature labels (to the LEFT of bar, like real IR cameras) ---
font = cv2.FONT_HERSHEY_SIMPLEX
font_scale = 0.4
thickness = 1
num_labels = 9  # 30.0, 31, 32, ..., 37, 38.0

for i in range(num_labels):
    temp = MAX_TEMP - i * (MAX_TEMP - MIN_TEMP) / (num_labels - 1)
    y_pos = BAR_TOP + int(i * (BAR_BOTTOM - BAR_TOP) / (num_labels - 1))
    if i == 0 or i == num_labels - 1:
        label = f"{temp:.1f}"
    else:
        label = f"{temp:.0f}"
    (tw, th), _ = cv2.getTextSize(label, font, font_scale, thickness)
    # Text to the RIGHT of the bar
    cv2.putText(image, label, (BAR_RIGHT + 3, y_pos + th // 2),
                font, font_scale, (255, 255, 255), thickness)

out_path = "tests/test_realistic.png"
cv2.imwrite(out_path, image)
print(f"Realistic test image saved: {out_path} ({W}x{H})")
print(f"Color bar: x=[{BAR_LEFT}, {BAR_RIGHT}], y=[{BAR_TOP}, {BAR_BOTTOM}], width={BAR_RIGHT-BAR_LEFT}")
print(f"Temperature: {MIN_TEMP} ~ {MAX_TEMP}")
