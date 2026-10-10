"""contact.py FRAMEDIR T0 T1 STEP OUT.png - contact sheet of frames between T0..T1 ms (one per STEP ms), labelled relative to T0."""
import sys, os, glob
from PIL import Image, ImageDraw
d, t0, t1, step, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
frames = sorted((int(os.path.basename(f)[:8]), f) for f in glob.glob(os.path.join(d, '*.bmp')))
frames = [(t, f) for t, f in frames if t0 <= t <= t1]
pick, nxt = [], t0
for t, f in frames:
    if t >= nxt: pick.append((t, f)); nxt = t + step
ims = [(t, Image.open(f)) for t, f in pick]
w, h = ims[0][1].size; cols = 6; rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * (h + 14)), 'white')
dr = ImageDraw.Draw(sheet)
for i, (t, im) in enumerate(ims):
    x, y = (i % cols) * w, (i // cols) * (h + 14)
    sheet.paste(im, (x, y + 14)); dr.text((x + 3, y + 1), f'+{t - t0}ms', fill='black')
sheet.save(out); print(len(frames), 'frames in range,', len(ims), 'shown', sheet.size)
