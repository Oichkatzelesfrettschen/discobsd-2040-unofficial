"""discobsd-link: a short URL for the discobsd-web console.

discobsd-web needs its token in the URL, which is long to type on a phone.
This serves one page on a memorable port that redirects to the full
tokenized console URL, so http://<host>:42069/ is all anyone on the LAN has
to type. Anyone who can reach this port learns the token, so it is only as
private as the network it listens on.
"""

from __future__ import annotations

import argparse
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

from . import __version__


class RedirectServer(HTTPServer):
    allow_reuse_address = True

    def __init__(self, address, target: str):
        super().__init__(address, RedirectHandler)
        self.target = target


class RedirectHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(302)
        self.send_header("Location", self.server.target)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_HEAD(self):
        self.do_GET()

    def log_message(self, fmt, *a):
        sys.stderr.write(f"{self.client_address[0]} - {fmt % a}\n")


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="discobsd-link", description="Redirect a short URL to the discobsd-web console."
    )
    p.add_argument("--to", required=True, metavar="URL", help="the tokenized console URL")
    p.add_argument("--port", type=int, default=42069, help="TCP port (default 42069)")
    p.add_argument("--bind", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    p.add_argument("--version", action="version", version="discobsd-link " + __version__)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    httpd = RedirectServer((args.bind, args.port), args.to)
    print("discobsd-link: http://%s:%d/ -> %s" % (args.bind, args.port, args.to), flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
