#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Generates the vector artwork for the games247 "Neon" theme (themes/neon-247).
# Usage: python3 make_svgs.py <output images directory>
import math
import random
import sys

out = sys.argv[1]
random.seed(247)


def write(name, body, w, h, extra=""):
    with open(f"{out}/{name}", "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
                f'viewBox="0 0 {w} {h}"{extra}>\n{body}</svg>\n')


# Sky: deep night purple fading to a hot pink glow at the horizon, with stars.
stars = []
for _ in range(170):
    x, y = random.uniform(0, 1920), random.uniform(0, 640)
    r = random.choice([1.2, 1.2, 1.6, 2.0, 2.6])
    o = round(random.uniform(0.35, 1.0) * (1 - y / 900), 2)
    stars.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{r}" fill="#ffffff" opacity="{o}"/>')
write("sky.svg", f'''<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#05010f"/>
<stop offset="0.35" stop-color="#14062e"/>
<stop offset="0.62" stop-color="#3a0b5c"/>
<stop offset="0.80" stop-color="#8a1a6e"/>
<stop offset="0.93" stop-color="#ff3d8b"/>
<stop offset="1" stop-color="#ff7a59"/>
</linearGradient></defs>
<rect width="1920" height="1080" fill="url(#g)"/>
{"".join(stars)}
''', 1920, 1080, ' preserveAspectRatio="none"')

# Sun: a yellow-to-magenta disc whose lower half is cut into thinning stripes.
R = 300
cx = cy = 300
parts = []


def slice_poly(y0, y1):
    # Polygon of the disc between heights y0 and y1 (y grows downwards).
    pts = []
    steps = 24
    for i in range(steps + 1):
        y = y0 + (y1 - y0) * i / steps
        dx = math.sqrt(max(R * R - (y - cy) ** 2, 0))
        pts.append((cx + dx, y))
    for i in range(steps, -1, -1):
        y = y0 + (y1 - y0) * i / steps
        dx = math.sqrt(max(R * R - (y - cy) ** 2, 0))
        pts.append((cx - dx, y))
    return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + "Z"


y = cy - R
parts.append(slice_poly(y, cy + 20))
y = cy + 20
gap, band = 6, 34
while y < cy + R:
    y += gap
    y1 = min(y + band, cy + R)
    if y1 > y:
        parts.append(slice_poly(y, y1))
    y = y1
    gap += 4
    band = max(band - 5, 8)
write("sun.svg", f'''<defs><linearGradient id="s" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#fff35c"/>
<stop offset="0.45" stop-color="#ffb02e"/>
<stop offset="0.75" stop-color="#ff3d8b"/>
<stop offset="1" stop-color="#c41ccf"/>
</linearGradient></defs>
<path d="{" ".join(parts)}" fill="url(#s)"/>
''', 600, 600)


# Mountains: two ridges with a neon rim, drawn along the horizon.
def ridge(seed, base, amp, n):
    random.seed(seed)
    pts = [(0, base)]
    for i in range(1, n):
        x = 1920 * i / n
        pts.append((x, base - random.uniform(0.25, 1.0) * amp))
    pts.append((1920, base))
    return pts


def path(pts, close=True):
    d = "M" + " L".join(f"{x:.0f},{y:.0f}" for x, y in pts)
    return d + (" L1920,300 L0,300 Z" if close else "")


back = ridge(3, 300, 210, 13)
front = ridge(9, 300, 120, 19)
write("mountains.svg", f'''<path d="{path(back)}" fill="#1c0638"/>
<path d="{path(back, False)}" fill="none" stroke="#ff3d8b" stroke-width="3" opacity="0.8"/>
<path d="{path(front)}" fill="#0d0221"/>
<path d="{path(front, False)}" fill="none" stroke="#2de2e6" stroke-width="2.5" opacity="0.9"/>
''', 1920, 300, ' preserveAspectRatio="none"')

# Panel: a dark glass box with a magenta-to-cyan neon border, stretched to fit.
write("panel.svg", '''<defs><linearGradient id="b" x1="0" y1="0" x2="1" y2="1">
<stop offset="0" stop-color="#ff3d8b"/><stop offset="1" stop-color="#2de2e6"/>
</linearGradient></defs>
<rect x="6" y="6" width="988" height="988" rx="22" fill="#0b0320" fill-opacity="0.78"
 stroke="url(#b)" stroke-width="5"/>
''', 1000, 1000, ' preserveAspectRatio="none"')

# Monitor: a retro TV bezel; the game picture is drawn behind its transparent screen.
write("monitor.svg", '''<defs><linearGradient id="m" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#2a1747"/><stop offset="1" stop-color="#120726"/>
</linearGradient>
<linearGradient id="e" x1="0" y1="0" x2="1" y2="0">
<stop offset="0" stop-color="#ff3d8b"/><stop offset="1" stop-color="#2de2e6"/>
</linearGradient></defs>
<path fill-rule="evenodd" fill="url(#m)" stroke="url(#e)" stroke-width="4"
 d="M30,10 H970 Q990,10 990,30 V720 Q990,740 970,740 H30 Q10,740 10,720 V30 Q10,10 30,10 Z
    M70,50 Q500,38 930,50 Q946,360 930,670 Q500,682 70,670 Q54,360 70,50 Z"/>
<circle cx="900" cy="708" r="9" fill="#2de2e6"/>
<rect x="60" y="702" width="120" height="10" rx="5" fill="#ff3d8b" opacity="0.8"/>
''', 1000, 750)

# Test card for the TV when a game has no screenshot: colour bars in the theme's neon colours.
bars = ["#f4ecff", "#fff35c", "#2de2e6", "#3cff8a", "#ff3d8b", "#ff4b3e", "#6a3dff"]
w = 800 / len(bars)
body = "".join(f'<rect x="{i * w:.2f}" y="0" width="{w + 0.5:.2f}" height="400" fill="{c}"/>'
               for i, c in enumerate(bars))
low = ["#6a3dff", "#0b0320", "#ff3d8b", "#0b0320", "#2de2e6", "#0b0320", "#f4ecff"]
body += "".join(f'<rect x="{i * w:.2f}" y="400" width="{w + 0.5:.2f}" height="50" fill="{c}"/>'
                for i, c in enumerate(low))
body += '<rect x="0" y="450" width="800" height="150" fill="#120726"/>'
body += "".join(f'<rect x="{x}" y="470" width="{wd}" height="110" fill="{c}"/>' for x, wd, c in
                [(40, 150, "#1c0a3d"), (190, 150, "#f4ecff"), (340, 150, "#2a0f52"),
                 (560, 60, "#05010f"), (620, 60, "#0b0320"), (680, 60, "#160a30")])
write("colorbars.svg", body, 800, 600)
