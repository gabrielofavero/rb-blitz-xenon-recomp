#!/usr/bin/env python3
"""Serve one acceptance feed on loopback; never expose other files."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--feed", type=Path, required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    feed = args.feed.resolve(strict=True)
    args.log.parent.mkdir(parents=True, exist_ok=True)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/feed.json":
                self.send_error(404)
                return
            try:
                body = feed.read_bytes()
            except OSError:
                self.send_error(500)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *values):
            line = f"{datetime.now(timezone.utc).isoformat()} {self.client_address[0]} {format % values}\n"
            with args.log.open("a", encoding="utf-8") as output:
                output.write(line)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Serving {feed} at http://127.0.0.1:{args.port}/feed.json", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
