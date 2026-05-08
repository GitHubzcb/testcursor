import numpy as np
import cv2

# Create a realistic bent needle image
width, height = 1024, 600
img = np.ones((height, width, 3), dtype=np.uint8) * 210

# Light wood-like background with horizontal grain
for y in range(height):
    base = 200 + int(10 * np.sin(y * 0.03))
    variation = np.random.randint(-3, 4, width)
    row = np.clip(base + variation, 0, 255).astype(np.uint8)
    img[y, :, 0] = row
    img[y, :, 1] = np.clip(row + 5, 0, 255)
    img[y, :, 2] = np.clip(row + 10, 0, 255)

# Needle parameters:
# Straight part: from right side (x=900) to bend point (x=500), at y=350
# Curved part: from x=500 to x=150, curving upward

needle_pts = []

# Straight section
for x in range(900, 500, -1):
    needle_pts.append((x, 350))

# Curved section: smooth upward bend
num_curve = 350
for i in range(num_curve):
    x = 500 - i
    t = i / float(num_curve)
    y = 350 - int(70 * (t ** 1.3))
    needle_pts.append((x, y))

# Draw needle - very dark, thin
for i in range(len(needle_pts) - 1):
    t = i / float(len(needle_pts))
    thickness = max(2, int(5 * (1 - t * 0.5)))
    cv2.line(img, needle_pts[i], needle_pts[i+1], (10, 10, 10), thickness, cv2.LINE_AA)

# Add slight reflection on needle (lighter line on top edge)
for i in range(0, len(needle_pts) - 1, 2):
    pt = (needle_pts[i][0], needle_pts[i][1] - 1)
    cv2.circle(img, pt, 0, (80, 80, 80), 1)

cv2.imwrite('images/needle.jpg', img)
print(f"Test image: {width}x{height}")
print(f"Straight: x=[900,500], y=350")
print(f"Bend start: x=500, y=350")
print(f"Tip: x={500 - num_curve}, y={350 - int(70)}")
