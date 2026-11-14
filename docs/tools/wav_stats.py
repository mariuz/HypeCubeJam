"""Prints what can be said about a 16-bit WAV without listening to it: format, length, peak, RMS, clipped samples,
and the RMS of every 5 s block (silence shows up as a very low number).

    python wav_stats.py <file.wav>
"""
import array, math, sys, wave

with wave.open(sys.argv[1], 'rb') as w:
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
s = array.array('h')
s.frombytes(raw[:len(raw) // 2 * 2])
peak = max(max(s), -min(s)) if s else 0
rms = math.sqrt(sum(x * x for x in s) / max(len(s), 1))
clipped = sum(1 for x in s if x >= 32767 or x <= -32768)
print('%d Hz, %d channels, %d-bit, %.2f s' % (rate, ch, width * 8, n / rate))
print('peak %.1f dBFS, rms %.1f dBFS, clipped samples %d (%.4f %%)' % (
    20 * math.log10(max(peak, 1) / 32768), 20 * math.log10(max(rms, 1) / 32768), clipped, 100.0 * clipped / max(len(s), 1)))
block = rate * ch * 5
print('rms per 5 s block (dBFS): ' + ' '.join(
    '%.0f' % (20 * math.log10(max(math.sqrt(sum(x * x for x in s[i:i + block]) / max(len(s[i:i + block]), 1)), 1) / 32768))
    for i in range(0, len(s), block)))
