"""Local server for the TamaPoke installer: python -m http.server, but it tells
the browser never to cache anything. Plain http.server sends no cache headers,
so Chrome/Edge reuse a stale index.html, manifest.json and firmware after a
rebuild -- which showed the old version number."""
import http.server
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
os.chdir(os.path.dirname(os.path.abspath(__file__)))   # serve THIS web folder


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()


http.server.ThreadingHTTPServer.allow_reuse_address = False
try:
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), NoCache)
except OSError as e:
    sys.exit('Could not start on port %d (%s).\n'
             'An older TamaPoke server window is probably still open and is '
             'still serving the old page. Close it and try again.' % (PORT, e))
print('Serving %s at http://localhost:%d  (no caching)' % (os.getcwd(), PORT))
srv.serve_forever()
