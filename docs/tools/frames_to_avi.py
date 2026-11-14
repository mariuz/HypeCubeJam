"""Stitches a folder of PNG frames (and optionally a WAV file) into a Motion-JPEG .avi using only Pillow.

    python frames_to_avi.py <frames dir> <out.avi> [fps=30] [jpeg quality=88] [max frames]
                            [--audio file.wav] [--width W] [--every N] [--audio-rate HZ] [--mono]

  --audio       16-bit PCM WAV muxed in as an uncompressed audio stream, cut / padded to the length of the video
                (more than two channels: the first two are kept)
  --width W     frames are scaled to W pixels wide (height follows)
  --every N     keeps every Nth frame (the frame rate of the file becomes fps / N)
  --audio-rate  halves the sample rate until it is at most HZ (48000 -> 24000 -> 12000)
  --mono        mixes the audio down to one channel

Written because the capture machine had no ffmpeg; the output plays in VLC / Windows Media Player and is
accepted by the usual upload sites.
"""
import array, io, os, struct, sys, wave
from PIL import Image


def chunk(fourcc, data):
    return fourcc + struct.pack('<I', len(data)) + data + (b'\x00' if len(data) & 1 else b'')


def lst(kind, data):
    return b'LIST' + struct.pack('<I', len(data) + 4) + kind + data


def read_audio(path, max_rate, mono):
    """Returns (rate, channels, array('h') of interleaved samples)."""
    with wave.open(path, 'rb') as w:
        channels, width, rate, count = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(count)
    if width != 2:
        raise SystemExit('audio must be 16-bit PCM, got %d bytes per sample' % width)
    samples = array.array('h')
    samples.frombytes(raw[:len(raw) // 2 * 2])
    if channels > 2:
        kept = array.array('h', bytes(4 * (len(samples) // channels)))
        kept[0::2] = samples[0::channels]
        kept[1::2] = samples[1::channels]
        samples, channels = kept, 2
    if mono and channels == 2:
        samples = array.array('h', [(a + b) // 2 for a, b in zip(samples[0::2], samples[1::2])])
        channels = 1
    while max_rate and rate > max_rate:
        # crude decimation by two: average neighbouring frames
        if channels == 1:
            samples = array.array('h', [(a + b) // 2 for a, b in zip(samples[0::2], samples[1::2])])
        else:
            left = [(a + b) // 2 for a, b in zip(samples[0::4], samples[2::4])]
            right = [(a + b) // 2 for a, b in zip(samples[1::4], samples[3::4])]
            samples = array.array('h', bytes(4 * len(left)))
            samples[0::2] = array.array('h', left)
            samples[1::2] = array.array('h', right)
        rate //= 2
    return rate, channels, samples


def main():
    args = sys.argv[1:]
    opts = {}
    pos = []
    i = 0
    while i < len(args):
        if args[i] == '--mono':
            opts['mono'] = True
        elif args[i].startswith('--'):
            opts[args[i][2:]] = args[i + 1]
            i += 1
        else:
            pos.append(args[i])
        i += 1
    src, out = pos[0], pos[1]
    fps = int(pos[2]) if len(pos) > 2 else 30
    quality = int(pos[3]) if len(pos) > 3 else 88
    files = sorted(f for f in os.listdir(src) if f.lower().endswith('.png'))
    if len(pos) > 4:
        files = files[:int(pos[4])]
    duration = len(files) / fps
    every = int(opts.get('every', 1))
    files = files[::every]
    out_fps = fps / every
    target_width = int(opts.get('width', 0))

    frames = []
    width = height = 0
    for name in files:
        im = Image.open(os.path.join(src, name)).convert('RGB')
        if target_width and im.size[0] != target_width:
            im = im.resize((target_width, round(im.size[1] * target_width / im.size[0] / 2) * 2), Image.LANCZOS)
        if not frames:
            width, height = im.size
        elif im.size != (width, height):
            im = im.resize((width, height))
        buf = io.BytesIO()
        im.save(buf, 'JPEG', quality=quality)
        frames.append(buf.getvalue())
    n = len(frames)
    biggest = max(len(f) for f in frames)

    audio = None
    if 'audio' in opts:
        rate, channels, samples = read_audio(opts['audio'], int(opts.get('audio-rate', 0)), opts.get('mono', False))
        wanted = int(round(duration * rate)) * channels
        if len(samples) < wanted:
            samples.extend([0] * (wanted - len(samples)))
        audio = (rate, channels, samples[:wanted].tobytes())

    movi = []
    index = []
    offset = 4

    def add(fourcc, data, flags):
        nonlocal offset
        c = chunk(fourcc, data)
        index.append(fourcc + struct.pack('<III', flags, offset, len(data)))
        offset += len(c)
        movi.append(c)

    block = 0
    audio_bytes = 0
    if audio:
        block = audio[1] * 2
        audio_bytes = len(audio[2])
    for k, f in enumerate(frames):
        add(b'00dc', f, 0x10)
        if audio:
            # the slice of sound that belongs to this frame, on whole sample frames
            a0 = (audio_bytes // block) * k // n * block
            a1 = (audio_bytes // block) * (k + 1) // n * block
            if a1 > a0:
                add(b'01wb', audio[2][a0:a1], 0x10)

    scale, vrate = 1000, int(round(out_fps * 1000))
    streams = 2 if audio else 1
    byte_rate = int(biggest * out_fps) + (audio[0] * block if audio else 0)
    avih = struct.pack('<IIIIIIIIII4I', int(round(1000000 / out_fps)), byte_rate, 0, 0x110 if audio else 0x10, n, 0, streams, biggest, width, height, 0, 0, 0, 0)
    strh = b'vids' + b'MJPG' + struct.pack('<IHHIIIIIIIIhhhh', 0, 0, 0, 0, scale, vrate, 0, n, biggest, 0xFFFFFFFF, 0, 0, 0, width, height)
    strf = struct.pack('<IiiHH4sIiiII', 40, width, height, 1, 24, b'MJPG', width * height * 3, 0, 0, 0, 0)
    hdrl = chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', strh) + chunk(b'strf', strf))
    if audio:
        rate, channels, _ = audio
        astrh = b'auds' + struct.pack('<IIHHIIIIIIIIhhhh', 0, 0, 0, 0, 0, block, rate * block, 0, audio_bytes // block, rate * block // 2, 0xFFFFFFFF, block, 0, 0, 0, 0)
        astrf = struct.pack('<HHIIHH', 1, channels, rate, rate * block, block, 16)
        hdrl += lst(b'strl', chunk(b'strh', astrh) + chunk(b'strf', astrf))
    body = b'AVI ' + lst(b'hdrl', hdrl) + lst(b'movi', b''.join(movi)) + chunk(b'idx1', b''.join(index))
    with open(out, 'wb') as fh:
        fh.write(b'RIFF' + struct.pack('<I', len(body)) + body)
    print('%d frames, %dx%d, %.2f fps, %.1f s, %s, %.1f MB -> %s' % (
        n, width, height, out_fps, duration,
        ('audio %d Hz x%d' % (audio[0], audio[1])) if audio else 'no audio', (len(body) + 8) / 1e6, out))


if __name__ == '__main__':
    main()
