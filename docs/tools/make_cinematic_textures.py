# Plain Python 3 + Pillow (no engine): python docs/tools/make_cinematic_textures.py
# Writes the three small procedural textures of the "cinematic look" pass to SourceAssets/Cinematic; they are imported
# and used by docs/tools/make_cinematic_materials.py (editor Python). Everything is generated from fixed seeds, so the
# files are the same on every run.
#   T_HCJ_CloudNoise.png  1024 x 1024, tileable, four different cloud fields in R, G, B, A (sums of smooth noise
#                         octaves; B is "ridged": bright thin veins). The nebula material samples it a few times at
#                         different scales instead of computing noise per pixel.
#   T_HCJ_Runes.png       1024 x 256: sixteen rune-like glyphs (8 x 2 cells of 128 px), white strokes with a soft glow
#                         on black. Made of straight strokes on a 3 x 5 grid of points; they are no real script.
#   T_HCJ_Craters.png     512 x 512, tileable: a field of round craters as a tangent-space normal in R, G (0.5 = flat)
#                         and a cavity / rim brightness in B (0.5 = neutral, darker in the bowl, lighter on the rim).
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceAssets", "Cinematic"))


def noise_octave(rng, cells, size):
    """Smooth tileable noise: a cells x cells grid of random values, wrapped, enlarged with bicubic filtering."""
    small = Image.new("L", (cells, cells))
    small.putdata([rng.randrange(256) for _ in range(cells * cells)])
    pad = 2
    wrapped = Image.new("L", (cells + 2 * pad, cells + 2 * pad))
    for oy in range(-1, 2):
        for ox in range(-1, 2):
            wrapped.paste(small, (pad + ox * cells, pad + oy * cells))
    scale = size // cells
    big = wrapped.resize(((cells + 2 * pad) * scale, (cells + 2 * pad) * scale), Image.BICUBIC)
    return big.crop((pad * scale, pad * scale, pad * scale + size, pad * scale + size))


def fbm(seed, size, first_cells, octaves, gain=0.5, ridged=False):
    """Weighted mean of noise octaves (8 bit: Image.blend keeps the running mean)."""
    rng = random.Random(seed)
    total = None
    amplitude = 1.0
    norm = 0.0
    cells = first_cells
    for _ in range(octaves):
        if cells > size // 2:
            break
        layer = noise_octave(rng, cells, size)
        if ridged:
            # 1 - |2n - 1|: bright where the noise crosses its middle -> thin veins
            layer = layer.point(lambda v: int(255 - abs(2 * v - 255)))
        total = layer if total is None else Image.blend(total, layer, amplitude / (norm + amplitude))
        norm += amplitude
        amplitude *= gain
        cells *= 2
    return total


def stretch(img, low_cut=0.0, power=1.0):
    lo, hi = img.getextrema()
    span = max(hi - lo, 1)

    def f(v):
        x = min(1.0, max(0.0, (v - lo) / span))
        x = max(0.0, (x - low_cut) / (1.0 - low_cut))
        return int(255.0 * (x ** power) + 0.5)

    return img.point(f)


def make_clouds():
    size = 1024
    r = stretch(fbm(11, size, 4, 7, 0.52), 0.18, 1.5)
    g = stretch(fbm(23, size, 8, 6, 0.55), 0.12, 1.3)
    b = stretch(fbm(37, size, 4, 6, 0.5, ridged=True), 0.45, 2.2)
    a = stretch(fbm(51, size, 2, 4, 0.45), 0.0, 1.0)
    Image.merge("RGBA", (r, g, b, a)).save(os.path.join(OUT, "T_HCJ_CloudNoise.png"))


def make_runes():
    cell = 128
    cols, rows = 8, 2
    scale = 4  # drawn large, reduced for smooth edges
    img = Image.new("L", (cols * cell * scale, rows * cell * scale), 0)
    draw = ImageDraw.Draw(img)
    rng = random.Random(5757)
    # points of a 3 x 5 grid inside the cell
    xs = [0.28, 0.5, 0.72]
    ys = [0.16, 0.33, 0.5, 0.67, 0.84]
    for index in range(cols * rows):
        ox = (index % cols) * cell * scale
        oy = (index // cols) * cell * scale
        strokes = []
        # a stem most of the time, then two to four more strokes between grid points that are not too far apart
        if rng.random() < 0.75:
            x = rng.choice(xs)
            strokes.append(((x, ys[0]), (x, ys[4])))
        for _ in range(rng.randint(2, 4)):
            for _attempt in range(20):
                a = (rng.choice(xs), rng.choice(ys))
                b = (rng.choice(xs), rng.choice(ys))
                d = math.hypot(a[0] - b[0], a[1] - b[1])
                if 0.2 < d < 0.62 and (a, b) not in strokes and (b, a) not in strokes:
                    strokes.append((a, b))
                    break
        width = int(0.085 * cell * scale)
        for a, b in strokes:
            pa = (ox + a[0] * cell * scale, oy + a[1] * cell * scale)
            pb = (ox + b[0] * cell * scale, oy + b[1] * cell * scale)
            draw.line([pa, pb], fill=255, width=width)
            for p in (pa, pb):
                draw.ellipse([p[0] - width / 2, p[1] - width / 2, p[0] + width / 2, p[1] + width / 2], fill=255)
        if rng.random() < 0.4:
            # a dot, as some scripts have
            p = (ox + rng.choice([0.3, 0.7]) * cell * scale, oy + rng.choice([0.2, 0.8]) * cell * scale)
            draw.ellipse([p[0] - width * 0.7, p[1] - width * 0.7, p[0] + width * 0.7, p[1] + width * 0.7], fill=255)
    sharp = img.resize((cols * cell, rows * cell), Image.LANCZOS)
    glow = sharp.filter(ImageFilter.GaussianBlur(5)).point(lambda v: min(255, int(v * 1.1)))
    out = ImageChops.lighter(sharp, glow.point(lambda v: int(v * 0.55)))
    out.save(os.path.join(OUT, "T_HCJ_Runes.png"))


def make_craters():
    size = 512
    rng = random.Random(9090)
    height = [[0.0] * size for _ in range(size)]
    craters = []
    for _ in range(84):
        craters.append((rng.random() * size, rng.random() * size, rng.uniform(14.0, 52.0) if rng.random() < 0.35 else rng.uniform(6.0, 16.0)))
    for cx, cy, radius in craters:
        reach = int(radius * 1.45) + 1
        depth = 0.55 * radius
        for dy in range(-reach, reach + 1):
            for dx in range(-reach, reach + 1):
                d = math.hypot(dx, dy) / radius
                if d > 1.45:
                    continue
                if d < 1.0:
                    h = -depth * (1.0 - d * d) + 0.22 * depth * d ** 6   # a bowl with a raised lip
                else:
                    h = 0.22 * depth * (1.0 - (d - 1.0) / 0.45) ** 2       # the rim falling off outside
                x = int(cx + dx) % size
                y = int(cy + dy) % size
                # later craters cut into earlier ones
                height[y][x] = h if d < 1.0 else max(height[y][x], h) if height[y][x] >= 0.0 else height[y][x]
    lo = min(min(row) for row in height)
    hi = max(max(row) for row in height)
    r = Image.new("L", (size, size))
    g = Image.new("L", (size, size))
    b = Image.new("L", (size, size))
    rp, gp, bp = [], [], []
    strength = 0.16
    for y in range(size):
        for x in range(size):
            dx = (height[y][(x + 1) % size] - height[y][(x - 1) % size]) * strength
            dy = (height[(y + 1) % size][x] - height[(y - 1) % size][x]) * strength
            inv = 1.0 / math.sqrt(dx * dx + dy * dy + 1.0)
            rp.append(int(max(0.0, min(255.0, 127.5 - dx * inv * 127.5))))
            gp.append(int(max(0.0, min(255.0, 127.5 - dy * inv * 127.5))))
            h = height[y][x]
            cavity = 0.5 + (0.5 * h / hi if h > 0.0 else -0.42 * h / lo)
            bp.append(int(max(0.0, min(255.0, cavity * 255.0))))
    r.putdata(rp)
    g.putdata(gp)
    b.putdata(bp)
    Image.merge("RGB", (r, g, b)).filter(ImageFilter.GaussianBlur(0.7)).save(os.path.join(OUT, "T_HCJ_Craters.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_clouds()
    make_runes()
    make_craters()
    for name in sorted(os.listdir(OUT)):
        print(name, os.path.getsize(os.path.join(OUT, name)))
