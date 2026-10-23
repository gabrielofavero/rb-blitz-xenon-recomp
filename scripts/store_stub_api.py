# A stand-in for the RhythmVerse API, for measurements that need the site to fail and then answer.
#
# P9-10 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md owes a boot where the
# catalogue cannot be reached: the press of the main menu's RhythmVerse row must draw the retail
# connection popup and ask for a fresh walk, a second press must do it again, and a later press -
# once the site answers, in the same boot - must open the store. Blocking the real host from the
# game would need the machine's hosts file or an admin firewall rule, and would make the "later
# answers" half impossible inside one boot. So the boot is pointed at this stub instead:
#
#   * with the fail flag present, every request is answered `503` (a failure the catalogue's own
#     backoff treats as retryable, and which a press's `RequestCatalogueRefreshNow` bypasses);
#   * with the flag gone, every request is answered `200` with a captured real list page, so the
#     walk lands and its offers are what the store lists.
#
# The flag is a file rather than a timer, so the moment the site "comes back" is the script's
# decision and not a race with the game's own retry.
#
# Usage (execution policy is Restricted on this machine, but python is not a script host):
#   python scripts/store_stub_api.py --port 8765 --payload out/_swap/p910/list-page1.json \
#       --fail-flag out/_swap/p910/serve-fail.flag --log out/_swap/p910/stub.log
#
# The payload is the API's own response body, captured once with the same form body the client
# sends (`BuildFormBody`); serving it verbatim keeps this stub free of assumptions about the
# schema, which `src/store/rhythmverse_client.cpp` owns.
#
# Diagnostics only: the game never starts this, and `store_api_base` (the cvar that points the
# game here) refuses anything that is not a bare http(s) origin.

import argparse
import http.server
import os
import sys
import time
import urllib.parse


def main() -> int:
    parser = argparse.ArgumentParser(description="A RhythmVerse API stand-in that fails on demand.")
    parser.add_argument("--port", type=int, default=8765, help="the port to listen on (127.0.0.1)")
    parser.add_argument("--payload", required=True, help="the JSON body to serve when it is up")
    parser.add_argument("--fail-flag", required=True,
                        help="while this file exists every request is answered 503")
    parser.add_argument("--log", default="", help="a file to append one line per request to")
    parser.add_argument("--ready-flag", default="",
                        help="created once the socket is listening (the boot script waits for it)")
    args = parser.parse_args()

    with open(args.payload, "rb") as handle:
        payload = handle.read()

    log_path = args.log

    def note(text: str) -> None:
        line = f"{time.strftime('%H:%M:%S')} {text}"
        print(line, flush=True)
        if log_path:
            with open(log_path, "a", encoding="utf-8") as handle:
                handle.write(line + "\n")

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_POST(self) -> None:  # noqa: N802 - the base class's spelling
            length = int(self.headers.get("Content-Length", "0") or 0)
            body = self.rfile.read(length) if length else b""
            form = urllib.parse.parse_qs(body.decode("utf-8", "replace"))
            page = form.get("page", ["?"])[0]
            records = form.get("records", ["?"])[0]
            text = form.get("text", [""])[0]
            failing = os.path.exists(args.fail_flag)
            if failing:
                # No Retry-After on purpose: the politeness wait this boot is about is the
                # catalogue's own, and the press path is supposed to bypass it.
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len('{"status":"error"}')))
                self.end_headers()
                self.wfile.write(b'{"status":"error"}')
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            note(f"{'503' if failing else '200'} {self.path} page={page} records={records}"
                 f"{' text=' + text if text else ''}")

        def do_GET(self) -> None:  # noqa: N802 - the base class's spelling
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            note(f"404 {self.path}")

        def log_message(self, fmt: str, *values) -> None:
            pass  # `note` is the record; the base class's stderr line would be noise

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    if args.ready_flag:
        with open(args.ready_flag, "w", encoding="utf-8") as handle:
            handle.write("listening\n")
    note(f"stub: listening on http://127.0.0.1:{args.port} "
         f"({'failing' if os.path.exists(args.fail_flag) else 'serving'}, "
         f"flag '{args.fail_flag}')")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
