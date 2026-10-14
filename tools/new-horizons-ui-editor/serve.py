#!/usr/bin/env python3
"""Loopback-only static prototype server; no uploads or browser launch."""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent
FILES = {'index.html', 'style.css', 'model.mjs', 'editor.mjs', 'translation.mjs', 'level-up.example.json', 'layout.schema.json'}

def admitted_path(path):
    name = urlsplit(path).path
    if name == '/':
        return 'index.html'
    return name[1:] if name.startswith('/') and name[1:] in FILES else None

class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, '.mjs': 'text/javascript'}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def send_head(self):
        name = admitted_path(self.path)
        if name is None:
            self.send_error(404)
            return None
        self.path = '/' + name
        return super().send_head()

    def end_headers(self):
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8766)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be 1024..65535')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Prototype editor: http://127.0.0.1:{args.port} (Ctrl-C stops)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
