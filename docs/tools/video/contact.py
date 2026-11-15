"""Contact sheet of a frame dump, for checking a shot: python contact.py <run dir> <out.jpg> [first] [last] [count] [cols]"""
import os, sys
from PIL import Image, ImageDraw

run, out = sys.argv[1], sys.argv[2]
files = sorted(f for f in os.listdir(run) if f.startswith('Frame_'))
first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
last = int(sys.argv[4]) if len(sys.argv) > 4 else len(files) - 1
count = int(sys.argv[5]) if len(sys.argv) > 5 else 16
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 4
last = min(last, len(files) - 1)
picks = [first + round(i * (last - first) / max(1, count - 1)) for i in range(count)]
tw = 1600 // cols
th = tw * 9 // 16
rows = (count + cols - 1) // cols
sheet = Image.new('RGB', (cols * tw, rows * th))
d = ImageDraw.Draw(sheet)
for i, k in enumerate(picks):
    im = Image.open(os.path.join(run, files[k])).convert('RGB').resize((tw, th), Image.LANCZOS)
    x, y = (i % cols) * tw, (i // cols) * th
    sheet.paste(im, (x, y))
    d.rectangle((x, y, x + 46, y + 14), fill=(0, 0, 0))
    d.text((x + 3, y + 1), str(k), fill=(255, 255, 0))
sheet.save(out, quality=85)
