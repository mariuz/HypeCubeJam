"""A small streaming AVI writer (Motion-JPEG video + 16-bit PCM audio) and a parser that reads such a file back.

Pure Python: no ffmpeg on the capture machine. The file is a plain AVI 1.0: RIFF 'AVI ', LIST hdrl (avih, two LIST strl
with strh + strf), LIST movi with interleaved 00dc / 01wb chunks, idx1. Good for files below 2 GB (the writer refuses more).

    python avi_mux.py check <file.avi> [dump dir]      parse a file back, print what is in it, decode sample frames
"""
import io, os, struct, sys

AVIF_HASINDEX, AVIF_ISINTERLEAVED = 0x10, 0x100
AVIIF_KEYFRAME = 0x10
HEADER_ROOM = 2048   # the headers are written last, into this fixed space at the start of the file (padded with a JUNK chunk)


def _chunk(fourcc, data):
    return fourcc + struct.pack('<I', len(data)) + data + (b'\x00' if len(data) & 1 else b'')


def _list(kind, data):
    return b'LIST' + struct.pack('<I', len(data) + 4) + kind + data


class AviWriter:
    """with AviWriter(path, w, h, fps, rate, channels) as avi: avi.add_frame(jpeg_bytes, pcm_bytes) ..."""

    def __init__(self, path, width, height, fps, audio_rate=0, audio_channels=2):
        self.path, self.width, self.height, self.fps = path, width, height, fps
        self.rate, self.channels = audio_rate, audio_channels
        self.block = audio_channels * 2
        self.fh = open(path, 'wb')
        self.fh.write(b'\x00' * HEADER_ROOM)
        self.fh.write(b'LIST' + struct.pack('<I', 0) + b'movi')
        self.movi_start = HEADER_ROOM + 8          # position of the 'movi' fourcc: idx1 offsets are relative to it
        self.index = []
        self.frames = 0
        self.audio_bytes = 0
        self.biggest = 0
        self.biggest_audio = 0

    def _add(self, fourcc, data):
        offset = self.fh.tell() - self.movi_start
        self.index.append(fourcc + struct.pack('<III', AVIIF_KEYFRAME, offset, len(data)))
        self.fh.write(_chunk(fourcc, data))

    def add_frame(self, jpeg, pcm=b''):
        self._add(b'00dc', jpeg)
        self.frames += 1
        self.biggest = max(self.biggest, len(jpeg))
        if self.rate and pcm:
            assert len(pcm) % self.block == 0
            self._add(b'01wb', pcm)
            self.audio_bytes += len(pcm)
            self.biggest_audio = max(self.biggest_audio, len(pcm))

    def close(self):
        fh = self.fh
        movi_end = fh.tell()
        fh.write(_chunk(b'idx1', b''.join(self.index)))
        end = fh.tell()
        if end >= 0x7FFFFFF0:
            raise SystemExit('file too big for a plain AVI')
        fps_num, fps_den = (int(self.fps), 1) if float(self.fps).is_integer() else (int(round(self.fps * 1000)), 1000)
        has_audio = self.rate and self.audio_bytes
        byte_rate = int(self.biggest * self.fps) + (self.rate * self.block if has_audio else 0)
        avih = struct.pack('<14I', int(round(1000000 / self.fps)), byte_rate, 0, AVIF_HASINDEX | AVIF_ISINTERLEAVED,
                           self.frames, 0, 2 if has_audio else 1, self.biggest, self.width, self.height, 0, 0, 0, 0)
        vstrh = b'vids' + b'MJPG' + struct.pack('<IHHIIIIIIII4h', 0, 0, 0, 0, fps_den, fps_num, 0, self.frames, self.biggest,
                                                0xFFFFFFFF, 0, 0, 0, self.width, self.height)
        vstrf = struct.pack('<IiiHH4sIiiII', 40, self.width, self.height, 1, 24, b'MJPG', self.width * self.height * 3, 0, 0, 0, 0)
        hdrl = _chunk(b'avih', avih) + _list(b'strl', _chunk(b'strh', vstrh) + _chunk(b'strf', vstrf))
        if has_audio:
            astrh = b'auds' + struct.pack('<IIHHIIIIIIII4h', 0, 0, 0, 0, 0, self.block, self.rate * self.block, 0,
                                          self.audio_bytes // self.block, self.biggest_audio, 0xFFFFFFFF, self.block, 0, 0, 0, 0)
            astrf = struct.pack('<HHIIHHH', 1, self.channels, self.rate, self.rate * self.block, self.block, 16, 0)
            hdrl += _list(b'strl', _chunk(b'strh', astrh) + _chunk(b'strf', astrf))
        head = b'RIFF' + struct.pack('<I', end - 8) + b'AVI ' + _list(b'hdrl', hdrl)
        room = HEADER_ROOM - len(head) - 8
        assert room >= 0
        head += b'JUNK' + struct.pack('<I', room) + b'\x00' * room
        fh.seek(0)
        fh.write(head)
        fh.write(b'LIST' + struct.pack('<I', movi_end - HEADER_ROOM - 8) + b'movi')
        fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if exc[0] is None:
            self.close()
        else:
            self.fh.close()


def parse(path, decode_dir=None, samples=6):
    """Reads the file back from its own structure. Returns a dict of what was found; raises on anything inconsistent."""
    size = os.path.getsize(path)
    info = {'file_bytes': size, 'streams': []}
    with open(path, 'rb') as fh:
        riff, riff_size, form = struct.unpack('<4sI4s', fh.read(12))
        assert riff == b'RIFF' and form == b'AVI ', 'not a RIFF AVI'
        assert riff_size + 8 == size, 'RIFF size %d + 8 != file size %d' % (riff_size, size)
        movi_pos = None
        idx = []
        pos = 12
        while pos < size:
            fh.seek(pos)
            fourcc, n = struct.unpack('<4sI', fh.read(8))
            if fourcc == b'LIST':
                kind = fh.read(4)
                if kind == b'hdrl':
                    data = fh.read(n - 4)
                    p = 0
                    while p < len(data):
                        cc, m = struct.unpack('<4sI', data[p:p + 8])
                        if cc == b'avih':
                            v = struct.unpack('<14I', data[p + 8:p + 8 + 56])
                            info['avih'] = {'usec_per_frame': v[0], 'max_bytes_per_sec': v[1], 'flags': v[3], 'total_frames': v[4],
                                            'streams': v[6], 'width': v[8], 'height': v[9]}
                        elif cc == b'LIST' and data[p + 8:p + 12] == b'strl':
                            sub = data[p + 12:p + 8 + m]
                            q = 0
                            stream = {}
                            while q < len(sub):
                                c2, m2 = struct.unpack('<4sI', sub[q:q + 8])
                                body = sub[q + 8:q + 8 + m2]
                                if c2 == b'strh':
                                    stream['type'] = body[0:4].decode()
                                    stream['handler'] = body[4:8]
                                    scale, rate, start, length, bufsize, quality, sample_size = struct.unpack('<7I', body[20:48])
                                    stream.update(scale=scale, rate=rate, length=length, sample_size=sample_size)
                                elif c2 == b'strf':
                                    if stream.get('type') == 'vids':
                                        bi = struct.unpack('<IiiHH4sI', body[:24])
                                        stream.update(width=bi[1], height=bi[2], bits=bi[4], compression=bi[5].decode())
                                    else:
                                        wf = struct.unpack('<HHIIHH', body[:16])
                                        stream.update(format_tag=wf[0], channels=wf[1], sample_rate=wf[2], byte_rate=wf[3], block=wf[4], bits=wf[5])
                                q += 8 + m2 + (m2 & 1)
                            info['streams'].append(stream)
                        p += 8 + m + (m & 1)
                elif kind == b'movi':
                    movi_pos = pos + 8
                    info['movi_bytes'] = n - 4
            elif fourcc == b'idx1':
                raw = fh.read(n)
                idx = [struct.unpack('<4sIII', raw[k:k + 16]) for k in range(0, n, 16)]
            pos += 8 + n + (n & 1)
        assert movi_pos is not None, 'no movi list'
        assert idx, 'no idx1 index'
        video = [e for e in idx if e[0] == b'00dc']
        audio = [e for e in idx if e[0] == b'01wb']
        # every index entry must point at a chunk with that fourcc and size
        for e in idx[:50] + idx[len(idx) // 2:len(idx) // 2 + 50] + idx[-50:]:
            fh.seek(movi_pos + e[2])
            cc, m = struct.unpack('<4sI', fh.read(8))
            assert cc == e[0] and m == e[3], 'index entry does not match the chunk at %d' % e[2]
        # walk the movi list chunk by chunk: it must hold exactly the indexed chunks
        walked = 0
        p = movi_pos + 4
        end = movi_pos + info['movi_bytes']
        while p < end:
            fh.seek(p)
            cc, m = struct.unpack('<4sI', fh.read(8))
            assert cc in (b'00dc', b'01wb'), 'unexpected chunk %r in movi' % cc
            walked += 1
            p += 8 + m + (m & 1)
        assert walked == len(idx), 'movi holds %d chunks, the index %d' % (walked, len(idx))
        info['video_chunks'] = len(video)
        info['audio_chunks'] = len(audio)
        info['audio_bytes'] = sum(e[3] for e in audio)
        info['video_bytes'] = sum(e[3] for e in video)
        info['largest_frame'] = max(e[3] for e in video)
        info['interleaved'] = all(idx[k][0] != idx[k + 1][0] for k in range(min(len(idx), 2000) - 1)) if audio else True
        vs = next(s for s in info['streams'] if s['type'] == 'vids')
        info['fps'] = vs['rate'] / vs['scale']
        info['video_seconds'] = len(video) / info['fps']
        assert vs['length'] == len(video) == info['avih']['total_frames'], 'frame counts disagree'
        if audio:
            au = next(s for s in info['streams'] if s['type'] == 'auds')
            info['audio_seconds'] = info['audio_bytes'] / au['byte_rate']
            assert au['length'] * au['block'] == info['audio_bytes'], 'audio length disagrees'
        # decode sample frames with Pillow
        from PIL import Image
        decoded = []
        for k in [int(i * (len(video) - 1) / max(1, samples - 1)) for i in range(samples)]:
            fh.seek(movi_pos + video[k][2] + 8)
            jpeg = fh.read(video[k][3])
            im = Image.open(io.BytesIO(jpeg))
            im.load()
            assert im.size == (vs['width'], vs['height']), 'frame %d is %s' % (k, im.size)
            decoded.append(k)
            if decode_dir:
                os.makedirs(decode_dir, exist_ok=True)
                im.save(os.path.join(decode_dir, 'check_%05d.jpg' % k), quality=85)
        info['decoded_frames'] = decoded
    return info


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'check':
        result = parse(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
        for key, value in result.items():
            print('%-18s %s' % (key, value))
        if 'audio_seconds' in result:
            print('audio - video      %+.4f s' % (result['audio_seconds'] - result['video_seconds']))
    else:
        print(__doc__)
