"""Copies the chosen frames of the capture runs to Media/page as the twelve store-page pictures and letters the cover.

    python make_stills.py

Every picture is one unaltered 1280x720 game frame of a fixed-step (30 game frames per second) capture run - the game
was never slowed down. STILLS says which run and frame; shots.py says how each run was staged.
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

CAP = r'F:\work\HCJ_push\capture'
OUT = r'F:\work\HypeCubeJam\Media\page'
FONTS = r'C:\Windows\Fonts'

STILLS = [
    # (file name, run, frame)
    ('01_hero_dogfight_brightest_nebula.png', 'hero0_nohud', 189),
    ('02_rune_portal_close.png', 'portal', 146),
    ('03_phaser_on_shield.png', 'phaser', 152),
    ('04_torpedo_hit_explosion_debris.png', 'phaser', 196),
    ('05_dense_asteroid_field_engine_jet.png', 'field_a', 160),
    ('06_warp_streaks.png', 'warp_b', 158),
    ('07_tractor_beam.png', 'tractor_a', 150),
    ('08_cloak_own_ship_ghosted.png', 'cloak_a', 330),
    ('09_hangar.png', 'menu', 300),
    ('10_station.png', 'station', 215),
    ('11_saturn.png', 'saturn', 170),
    ('12_title_menu_quick_play.png', 'menu', 100),
]
COVER = ('hero0_nohud', 189)     # run and frame the cover is cut from
COVER_CENTRE_X = 640            # centre of the 907 px wide crop


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, run, frame in STILLS:
        im = Image.open(os.path.join(CAP, run, 'Frame_%05d.png' % frame)).convert('RGB')
        assert im.size == (1280, 720), (name, im.size)
        im.save(os.path.join(OUT, name), optimize=True)
        print('%s <- %s frame %d' % (name, run, frame))

    run, frame = COVER
    src = Image.open(os.path.join(CAP, run, 'Frame_%05d.png' % frame)).convert('RGB')
    crop_w = round(720 * 630 / 500)
    x0 = max(0, min(1280 - crop_w, COVER_CENTRE_X - crop_w // 2))
    cover = src.crop((x0, 0, x0 + crop_w, 720)).resize((630, 500), Image.LANCZOS)
    # a soft dark band behind the lettering, then the title in the game's cyan with a glow
    shade = Image.new('L', (630, 500), 0)
    d = ImageDraw.Draw(shade)
    for y in range(500):
        k = max(0.0, 1.0 - y / 190.0)
        d.line((0, y, 630, y), fill=int(190 * k ** 1.5))
    cover = Image.composite(Image.new('RGB', (630, 500), (2, 8, 16)), cover, shade)
    title_font = ImageFont.truetype(os.path.join(FONTS, 'seguibl.ttf'), 70)
    sub_font = ImageFont.truetype(os.path.join(FONTS, 'seguisb.ttf'), 21)
    glow = Image.new('RGBA', (630, 500), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((315, 70), 'HYPECUBE JAM', font=title_font, fill=(60, 220, 255, 255), anchor='mm')
    glow = glow.filter(ImageFilter.GaussianBlur(9))
    cover = Image.alpha_composite(cover.convert('RGBA'), glow)
    d = ImageDraw.Draw(cover)
    d.text((317, 73), 'HYPECUBE JAM', font=title_font, fill=(0, 20, 30, 255), anchor='mm')
    d.text((315, 70), 'HYPECUBE JAM', font=title_font, fill=(140, 245, 255, 255), anchor='mm')
    d.text((315, 122), 'dogfights on the corners of a hypercube', font=sub_font, fill=(235, 245, 250, 255), anchor='mm')
    cover.convert('RGB').save(os.path.join(OUT, 'cover_630x500.png'), optimize=True)
    print('cover_630x500.png <- %s frame %d' % (run, frame))


if __name__ == '__main__':
    main()
