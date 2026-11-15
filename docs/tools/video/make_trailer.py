"""Cuts the trailer from the captured frame dumps (see shots.py / capture_run.py) and muxes it with avi_mux.py.

    python make_trailer.py [--no-main] [--list]

  Reads cutlist.py (the edit decision list), draws the captions and title cards with Pillow, builds the sound
  (music bed from the Lyria WAV exports + the game's own effects recorded with the frames) and writes
    Media/HypeCubeJam_trailer.avi             1280x720, 30 fps, MJPEG q90, PCM 48 kHz stereo (music + game effects)
    Media/HypeCubeJam_trailer_music_only.avi  the same pictures with the music bed alone
    Media/HypeCubeJam_trailer_preview.avi     640x360, lower quality
  Every source frame is one game frame at a fixed 30 fps step, used 1:1 - nothing is retimed. The exception is the
  intro movie, which the game plays in wall-clock time: its frames are picked from several capture runs by the movie
  frame they show (match_intro.py).
"""
import array, io, json, math, os, sys, wave
from PIL import Image, ImageDraw, ImageFont

import avi_mux
import cutlist

CAP = r'F:\work\HCJ_push\capture'
MEDIA = r'F:\work\HypeCubeJam\Media'
FONTS = r'C:\Windows\Fonts'
W, H, FPS, RATE = 1280, 720, 30, 48000
SPF = RATE // FPS


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


# ---------------------------------------------------------------- pictures

_caption_cache = {}


def caption_image(text):
    """An RGBA strip: dark backing, a cyan bar on the left, the text (FEATURE in semibold, the rest regular)."""
    if text in _caption_cache:
        return _caption_cache[text]
    head, _, tail = text.partition('|')
    f1, f2 = font('seguisb.ttf', 21), font('segoeui.ttf', 19)
    probe = ImageDraw.Draw(Image.new('RGB', (8, 8)))
    w1 = probe.textlength(head.strip(), font=f1)
    w2 = probe.textlength(tail.strip(), font=f2) if tail else 0
    gap = 12 if tail else 0
    width = int(18 + w1 + gap + w2 + 16)
    im = Image.new('RGBA', (width, 38), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, width - 1, 37), fill=(6, 14, 24, 205))
    d.rectangle((0, 0, 3, 37), fill=(120, 240, 255, 255))
    d.text((16, 19), head.strip(), font=f1, fill=(255, 255, 255, 255), anchor='lm')
    if tail:
        d.text((16 + w1 + gap, 20), tail.strip(), font=f2, fill=(170, 215, 230, 255), anchor='lm')
    _caption_cache[text] = im
    return im


def draw_caption(im, text, pos, alpha):
    if not text or alpha <= 0:
        return im
    cap = caption_image(text)
    if alpha < 1:
        cap = cap.copy()
        cap.putalpha(cap.getchannel('A').point(lambda v: int(v * alpha)))
    y = {'low': 656, 'hud': 414, 'top': 96}[pos or 'low']   # 'hud': clear of the HUD's sector line and radar
    im.paste(cap, (24, y), cap)
    return im


def card_image(lines):
    """A title card: [(text, font file, size, colour, y)] centred on black with a faint gradient."""
    im = Image.new('RGB', (W, H), (2, 5, 10))
    d = ImageDraw.Draw(im)
    for y in range(H):
        k = 1.0 - abs(y - H / 2) / (H / 2)
        d.line((0, y, W, y), fill=(int(2 + 6 * k), int(5 + 16 * k), int(10 + 26 * k)))
    for text, face, size, colour, y in lines:
        d.text((W // 2, y), text, font=font(face, size), fill=colour, anchor='mm')
    return im


def intro_files(first, last):
    """One game frame per output frame for movie frames first..last (24 per second) of the intro."""
    match = json.load(open(os.path.join(CAP, 'intro_match.json')))
    by_index = {}
    for run, seq in match.items():
        for name, index, diff in seq:
            if diff < 70:
                by_index.setdefault(index, []).append((diff, os.path.join(run if os.path.isabs(run) else os.path.join(CAP, run), name)))
    for index in by_index:
        by_index[index].sort()
    have = sorted(by_index)
    out = []
    n = int(round((last - first) / 24.0 * FPS))
    for j in range(n):
        want = first + j * 24.0 / FPS
        best = min(have, key=lambda k: (abs(k - want), k))
        out.append(by_index[best][0][1])
    return out


def build_timeline():
    """-> [frame], frame = [(kind, payload, caption, pos, caption alpha, weight)] (one or two layers), and the audio clips."""
    timeline = []
    audio_clips = []       # (wav path, first source sample, output frame, frames, fade-in frames, fade-out frames)
    marks = []
    cuts = cutlist.CUTS

    def cap_alpha(index, i, n):
        # a caption fades in and out with its cut, but stays up across cuts that carry the same caption
        cap = cuts[index].get('cap')
        first = index == 0 or cuts[index - 1].get('cap') != cap
        last = index == len(cuts) - 1 or cuts[index + 1].get('cap') != cap
        return min(1.0, (i + 1) / 8 if first else 1.0, (n - i) / 8 if last else 1.0)

    for index, cut in enumerate(cuts):
        kind = cut['kind']
        xf = cut.get('xf', 0)
        frames = []
        if kind == 'card':
            image = ('card', cut['lines'])
            n = cut['frames']
            fi, fo = cut.get('fade_in', 0), cut.get('fade_out', 0)
            for i in range(n):
                dim = min(1.0, (i + 1) / fi if fi else 1.0, (n - i) / fo if fo else 1.0)
                frames.append([('card', (cut['lines'], dim), None, None, 0.0, 1.0)])
        elif kind == 'intro':
            files = intro_files(cut['first'], cut['last'])
            for i, path in enumerate(files):
                frames.append([('png', path, cut.get('cap'), cut.get('pos'), cap_alpha(index, i, len(files)), 1.0)])
        else:
            src = os.path.join(CAP, cut['src'])
            a, b = cut['a'], cut['b']
            for i, f in enumerate(range(a, b + 1)):
                n = b - a + 1
                frames.append([('png', os.path.join(src, 'Frame_%05d.png' % f), cut.get('cap'), cut.get('pos'), cap_alpha(index, i, n), 1.0)])
            wav = os.path.join(src, 'Audio.wav')
            if os.path.exists(wav) and not cut.get('mute'):
                audio_clips.append([wav, a * SPF, len(timeline) - xf, b - a + 1, max(xf, 1), 1])
        if xf and timeline:
            # cross-fade: the first xf frames of this cut are blended over the last xf frames of the timeline
            for i in range(xf):
                k = (i + 1) / (xf + 1)
                old = timeline[len(timeline) - xf + i]
                old[0] = old[0][:5] + (1.0 - k,)
                old.append(frames[i][0][:5] + (k,))
            frames = frames[xf:]
        marks.append((len(timeline) - (xf if timeline else 0), cut))
        timeline.extend(frames)
    return timeline, audio_clips, marks


_card_cache = {}


def render(frame):
    out = None
    for kind, payload, cap, pos, cap_alpha, weight in frame:
        if kind == 'card':
            lines, dim = payload
            key = json.dumps(lines)
            if key not in _card_cache:
                _card_cache[key] = card_image(lines)
            im = _card_cache[key]
            if dim < 1:
                im = Image.blend(Image.new('RGB', (W, H), (0, 0, 0)), im, dim)
        else:
            im = Image.open(payload).convert('RGB')
            if im.size != (W, H):
                im = im.resize((W, H), Image.LANCZOS)
            im = draw_caption(im, cap, pos, cap_alpha)
        out = im if out is None else Image.blend(out, im, weight)
    return out


# ---------------------------------------------------------------- sound

def read_wav(path, first=0, count=None):
    """-> (rate, [left floats], [right floats]) of 16-bit PCM, sample frames first .. first+count."""
    with wave.open(path, 'rb') as w:
        ch, width, rate, total = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        assert width == 2, path
        first = min(first, total)
        w.setpos(first)
        raw = w.readframes(total - first if count is None else min(count, total - first))
    s = array.array('h')
    s.frombytes(raw)
    if ch == 1:
        return rate, list(s), list(s)
    return rate, list(s[0::ch]), list(s[1::ch])


def resample(x, src_rate, dst_rate, n_out):
    """Linear interpolation; good enough for a 44.1 -> 48 kHz music bed."""
    if src_rate == dst_rate:
        return (x + [0] * n_out)[:n_out]
    step = src_rate / dst_rate
    last = len(x) - 2
    out = [0.0] * n_out
    for i in range(n_out):
        p = i * step
        k = int(p)
        if k > last:
            break
        f = p - k
        out[i] = x[k] * (1.0 - f) + x[k + 1] * f
    return out


def stats(left, right):
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right), 1e-9)
    rms = math.sqrt((sum(v * v for v in left) + sum(v * v for v in right)) / (2 * len(left)))
    db = lambda v: 20 * math.log10(max(v, 1e-9) / 32768.0)
    return 'peak %.1f dBFS, RMS %.1f dBFS' % (db(peak), db(rms))


def build_audio(n_frames, audio_clips):
    n = n_frames * SPF
    m = cutlist.MUSIC
    music_l, music_r = [0.0] * n, [0.0] * n
    for track in m['tracks']:
        # {'file', 'from' (s in the trailer), 'to', 'offset' (s into the track), 'fade_in', 'fade_out', 'gain'}
        with wave.open(track['file'], 'rb') as w:
            rate = w.getframerate()
        a, b = int(track['from'] * RATE), min(n, int(track['to'] * RATE))
        need = int((b - a) * rate / RATE) + 4
        _, l, r = read_wav(track['file'], int(track.get('offset', 0) * rate), need)
        l, r = resample(l, rate, RATE, b - a), resample(r, rate, RATE, b - a)
        fi, fo, g = int(track.get('fade_in', 0) * RATE), int(track.get('fade_out', 0) * RATE), track.get('gain', 1.0)
        for i in range(b - a):
            k = g * min(1.0, (i + 1) / fi if fi else 1.0, (b - a - i) / fo if fo else 1.0)
            music_l[a + i] += l[i] * k
            music_r[a + i] += r[i] * k
    print('music bed: ' + stats(music_l, music_r))
    peak = max(max(abs(v) for v in music_l), max(abs(v) for v in music_r))
    scale = 32768.0 * 10 ** (m['bed_peak_db'] / 20) / peak
    music_l = [v * scale for v in music_l]
    music_r = [v * scale for v in music_r]

    sfx_l, sfx_r = [0.0] * n, [0.0] * n
    for wav, first, out_frame, frames, fade_in, fade_out in audio_clips:
        rate, l, r = read_wav(wav, first, frames * SPF)
        assert rate == RATE, wav
        o = out_frame * SPF
        fi, fo = max(fade_in * SPF, 720), max(fade_out * SPF, 720)
        cnt = len(l)
        for i in range(cnt):
            if 0 <= o + i < n:
                k = min(1.0, i / fi, (cnt - i) / fo)
                sfx_l[o + i] += l[i] * k
                sfx_r[o + i] += r[i] * k
    print('game effects (as recorded): ' + stats(sfx_l, sfx_r))
    g = 10 ** (m['sfx_db'] / 20)
    mix_l = [a + b * g for a, b in zip(music_l, sfx_l)]
    mix_r = [a + b * g for a, b in zip(music_r, sfx_r)]

    def finish(l, r, label):
        peak = max(max(abs(v) for v in l), max(abs(v) for v in r))
        scale = 32768.0 * 10 ** (m['final_peak_db'] / 20) / peak
        out = array.array('h', bytes(4 * len(l)))
        out[0::2] = array.array('h', [int(max(-32768, min(32767, round(v * scale)))) for v in l])
        out[1::2] = array.array('h', [int(max(-32768, min(32767, round(v * scale)))) for v in r])
        print('%s: %s (gain %.2f)' % (label, stats(list(out[0::2]), list(out[1::2])), scale))
        return out.tobytes()

    return finish(mix_l, mix_r, 'final mix (music + effects)'), finish(music_l, music_r, 'final mix (music only)')


# ---------------------------------------------------------------- main

def main():
    timeline, audio_clips, marks = build_timeline()
    n = len(timeline)
    print('%d frames, %.2f s' % (n, n / FPS))
    for start, cut in marks:
        label = cut.get('cap') or cut.get('src') or cut['kind']
        print('  %6.2f s  frame %5d  %-8s %s' % (start / FPS, start, cut.get('src', cut['kind']), label))
    if '--list' in sys.argv:
        return
    missing = [layer[1] for frame in timeline for layer in frame if layer[0] == 'png' and not os.path.exists(layer[1])]
    if missing:
        raise SystemExit('missing frames, e.g. ' + missing[0])
    mix, music_only = build_audio(n, audio_clips)
    with wave.open(os.path.join(CAP, 'trailer_mix.wav'), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(mix)
    main_on = '--no-main' not in sys.argv
    outs = []
    if main_on:
        outs.append((avi_mux.AviWriter(os.path.join(MEDIA, 'HypeCubeJam_trailer.avi'), W, H, FPS, RATE, 2), mix, 'main'))
        outs.append((avi_mux.AviWriter(os.path.join(MEDIA, 'HypeCubeJam_trailer_music_only.avi'), W, H, FPS, RATE, 2), music_only, 'main'))
    outs.append((avi_mux.AviWriter(os.path.join(MEDIA, 'HypeCubeJam_trailer_preview.avi'), W // 2, H // 2, FPS, RATE, 2), mix, 'small'))
    block = SPF * 4
    for i, frame in enumerate(timeline):
        im = render(frame)
        encoded = {}
        if main_on:
            buf = io.BytesIO(); im.save(buf, 'JPEG', quality=90, subsampling=1); encoded['main'] = buf.getvalue()
        buf = io.BytesIO(); im.resize((W // 2, H // 2), Image.LANCZOS).save(buf, 'JPEG', quality=62, subsampling=2); encoded['small'] = buf.getvalue()
        for writer, pcm, which in outs:
            writer.add_frame(encoded[which], pcm[i * block:(i + 1) * block])
        if i % 300 == 0:
            print('  frame %d of %d' % (i, n), flush=True)
    for writer, _, _ in outs:
        writer.close()
        print('%s: %.1f MB' % (writer.path, os.path.getsize(writer.path) / 1e6))


if __name__ == '__main__':
    main()
