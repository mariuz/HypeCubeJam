# A plain static file server WITHOUT cross-origin isolation headers, to imitate GitHub Pages locally:
# no COOP / COEP / CORP, no Content-Encoding, correct MIME types, single byte ranges answered 206.
#
#   <engine python> docs/tools/web_plain_server.py <folder> [port]        (port 8080-8099; default 8090)
#
# With it the page only starts when its service worker injects the isolation headers
# ([/Script/WasmTargetPlatform.WasmTargetSettings] bInjectIsolationHeaders=True) - the path GitHub Pages needs.
# The engine's own serve.py always sends the headers, so it cannot test that. Listens on 127.0.0.1 only.
import http.server
import os
import re
import sys

TYPES = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.json': 'application/json',
    '.wasm': 'application/wasm', '.png': 'image/png', '.ico': 'image/x-icon', '.txt': 'text/plain; charset=utf-8',
    '.webmanifest': 'application/manifest+json', '.css': 'text/css',
}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    root = '.'

    def log_message(self, format, *args):
        pass

    def do_HEAD(self):
        self.answer(False)

    def do_GET(self):
        self.answer(True)

    def answer(self, body):
        path = self.path.split('?', 1)[0].split('#', 1)[0]
        if path.endswith('/'):
            path += 'index.html'
        full = os.path.normpath(os.path.join(self.root, path.lstrip('/').replace('/', os.sep)))
        if not full.startswith(self.root) or not os.path.isfile(full):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        size = os.path.getsize(full)
        start, end = 0, size - 1
        status = 200
        match = re.match(r'bytes=(\d*)-(\d*)$', self.headers.get('Range', ''))
        if match and (match.group(1) or match.group(2)):
            if match.group(1):
                start = int(match.group(1))
                end = min(int(match.group(2)), size - 1) if match.group(2) else size - 1
            else:
                start = max(0, size - int(match.group(2)))
            if start > end or start >= size:
                self.send_response(416)
                self.send_header('Content-Range', 'bytes */%d' % size)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            status = 206
        self.send_response(status)
        self.send_header('Content-Type', TYPES.get(os.path.splitext(full)[1].lower(), 'application/octet-stream'))
        self.send_header('Content-Length', str(end - start + 1))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'max-age=600')   # as GitHub Pages
        if status == 206:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, size))
        self.end_headers()
        if not body:
            return
        try:
            with open(full, 'rb') as handle:
                handle.seek(start)
                left = end - start + 1
                while left > 0:
                    block = handle.read(min(left, 1 << 20))
                    if not block:
                        break
                    self.wfile.write(block)
                    left -= len(block)
        except (ConnectionError, OSError):
            pass


def main():
    if len(sys.argv) < 2:
        print(__doc__ or 'usage: web_plain_server.py <folder> [port]')
        return 2
    Handler.root = os.path.abspath(sys.argv[1])
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8090
    server = http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler)
    print('plain server (no isolation headers): http://127.0.0.1:%d/ from %s  pid %d' % (port, Handler.root, os.getpid()), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
