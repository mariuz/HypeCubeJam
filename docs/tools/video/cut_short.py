"""Cuts a shorter film out of an AVI written by avi_mux.py (MJPEG frames are independent, so cuts need no re-encoding).

    python cut_short.py <in.avi> <out.avi> <start-end> [<start-end> ...]     times in seconds

Each kept range gets a 40 ms audio fade at both ends (no clicks at the cuts); the last 1.5 s fade out.
"""
import struct, sys, array
from avi_mux import AviWriter, HEADER_ROOM

FPS, RATE, CH = 30, 48000, 2


def chunks(path):
    with open(path, 'rb') as f:
        f.seek(HEADER_ROOM)
        assert f.read(4) == b'LIST'
        size = struct.unpack('<I', f.read(4))[0]
        assert f.read(4) == b'movi'
        end = HEADER_ROOM + 8 + size
        while f.tell() < end:
            cc = f.read(4)
            n = struct.unpack('<I', f.read(4))[0]
            data = f.read(n)
            if n & 1:
                f.read(1)
            yield cc, data


def fade(pcm, gain_at):
    a = array.array('h', pcm)
    frames = len(a) // CH
    for i in range(frames):
        g = gain_at(i / frames)
        if g < 1.0:
            for c in range(CH):
                a[i * CH + c] = int(a[i * CH + c] * g)
    return a.tobytes()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    ranges = [tuple(round(float(x) * FPS) for x in r.split('-')) for r in sys.argv[3:]]
    keep = {}
    for a, b in ranges:
        for i in range(a, b):
            keep[i] = (i == a, i == b - 1)
    total = len(keep)
    tail = int(1.5 * FPS)
    out = 0
    with AviWriter(dst, 1280, 720, FPS, RATE, CH) as avi:
        idx = -1
        jpeg = None
        for cc, data in chunks(src):
            if cc == b'00dc':
                idx += 1
                jpeg = data
            elif cc == b'01wb' and idx in keep:
                first, last = keep[idx]
                pcm = data
                if first:
                    pcm = fade(pcm, lambda t: t)
                if last:
                    pcm = fade(pcm, lambda t: 1.0 - t)
                left = total - out
                if left <= tail:
                    g0, g1 = left / tail, (left - 1) / tail
                    pcm = fade(pcm, lambda t: g0 + (g1 - g0) * t)
                avi.add_frame(jpeg, pcm)
                out += 1
    print('frames', out, 'seconds', out / FPS)


main()
