"""Matches the frames of intro capture runs against the decoded reference thumbnails of the intro movie.

    python match_intro.py <ref dir> <out json> <run dir>...

The game plays the intro movie in wall-clock time while the frame dump runs at about a third of real time, so one
run holds only every second or third movie frame. Several runs are merged: every dumped frame is assigned the
movie frame it shows (smallest mean absolute difference of 160x90 thumbnails, the band of the title left out).
"""
import json, os, sys
from PIL import Image, ImageChops, ImageStat

W, H = 160, 90


def thumb(path):
    im = Image.open(path).convert('RGB')
    if im.size != (W, H):
        im = im.resize((W, H), Image.BILINEAR)
    # leave out the band where the title and the tagline are drawn
    top = im.crop((0, 0, W, 36)); bottom = im.crop((0, 54, W, H))
    out = Image.new('RGB', (W, 72)); out.paste(top, (0, 0)); out.paste(bottom, (0, 36))
    return out


def main():
    ref_dir, out_json, runs = sys.argv[1], sys.argv[2], sys.argv[3:]
    refs = [thumb(os.path.join(ref_dir, f)) for f in sorted(os.listdir(ref_dir)) if f.endswith('.png')]
    result = {}
    for run in runs:
        files = sorted(f for f in os.listdir(run) if f.startswith('Frame_'))
        seq = []
        for f in files:
            t = thumb(os.path.join(run, f))
            best, best_d = -1, 1e9
            for k, r in enumerate(refs):
                d = sum(ImageStat.Stat(ImageChops.difference(t, r)).mean)
                if d < best_d:
                    best, best_d = k, d
            seq.append((f, best, round(best_d, 1)))
            if best_d > 60 and len(seq) > 20 and seq[-1][2] > 60 and seq[-2][2] > 60 and seq[-3][2] > 60:
                break   # the menu has opened
        result[run] = seq
        print(os.path.basename(run), ' '.join('%d:%.0f' % (b, d) for _, b, d in seq))
    json.dump(result, open(out_json, 'w'))


if __name__ == '__main__':
    main()
