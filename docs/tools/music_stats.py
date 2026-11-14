"""Measures music WAVs (16-bit PCM, as written by docs/tools/import_lyria.py to Saved/LyriaWav) without listening:
length, peak, RMS, and the RMS of the first and last seconds in 0.25 s steps (is there a fade or silence at the ends,
do the two ends match for a loop).

    python music_stats.py <file.wav> [<file.wav> ...]
"""
import array, math, sys, wave


def db(value):
    return 20 * math.log10(max(value, 1.0) / 32768.0)


def rms(samples):
    return math.sqrt(sum(x * x for x in samples) / max(len(samples), 1))


for path in sys.argv[1:]:
    with wave.open(path, 'rb') as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        print('%s: %d-bit, not measured' % (path, width * 8))
        continue
    s = array.array('h')
    s.frombytes(raw[:len(raw) // 2 * 2])
    peak = max(max(s), -min(s)) if s else 0
    total = rms(s)
    step = rate * ch // 4
    head = [db(rms(s[i:i + step])) for i in range(0, min(step * 12, len(s)), step)]
    tail_start = max(len(s) - step * 12, 0)
    tail = [db(rms(s[i:i + step])) for i in range(tail_start, len(s), step)]
    # first / last sample above -60 dBFS (33 of 32768)
    first = next((i for i, x in enumerate(s) if abs(x) > 33), len(s)) / (rate * ch)
    last = next((i for i in range(len(s) - 1, -1, -1) if abs(s[i]) > 33), 0) / (rate * ch)
    middle = db(rms(s[len(s) // 4: 3 * len(s) // 4]))
    name = path.replace('\\', '/').split('/')[-1]
    print('%s: %d Hz, %d ch, %.2f s, peak %.1f dBFS, rms %.1f dBFS (middle half %.1f), sound from %.2f s to %.2f s' % (
        name, rate, ch, n / rate, db(peak), db(total), middle, first, last))
    print('   first 3 s, rms per 0.25 s: ' + ' '.join('%.0f' % v for v in head))
    print('   last 3 s,  rms per 0.25 s: ' + ' '.join('%.0f' % v for v in tail))
