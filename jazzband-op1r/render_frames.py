# Draws the screen frames the host test logged (Q10 line segments) onto a 320x160 OLED-like canvas, 3x scale.
import sys
from PIL import Image, ImageDraw
S = 3
frames, cur = [], None
for line in open(sys.argv[1]):
    if line.startswith('F '): cur = (line[2:].strip(), []); frames.append(cur); continue
    x0, y0, x1, y1, w, c = line.split(); cur[1].append((int(x0), int(y0), int(x1), int(y1), int(w), int(c, 16)))
pick = sys.argv[3].split(',') if len(sys.argv) > 3 else [f[0] for f in frames]
imgs = []
for name, lines in frames:
    if name not in pick: continue
    im = Image.new('RGB', (320 * S, 160 * S), (0, 0, 0)); d = ImageDraw.Draw(im)
    for x0, y0, x1, y1, w, c in lines:
        col = ((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255)
        d.line([(x0 * S / 1024, y0 * S / 1024), (x1 * S / 1024, y1 * S / 1024)], fill=col, width=max(1, round(w * S / 1024)))
    d.text((6, 4), name, fill=(80, 80, 80))
    imgs.append(im)
sheet = Image.new('RGB', (320 * S * 2 + 10, (160 * S + 10) * ((len(imgs) + 1) // 2)), (30, 30, 30))
for i, im in enumerate(imgs): sheet.paste(im, ((i % 2) * (320 * S + 10), (i // 2) * (160 * S + 10)))
sheet.save(sys.argv[2]); print(len(imgs), 'frames')
