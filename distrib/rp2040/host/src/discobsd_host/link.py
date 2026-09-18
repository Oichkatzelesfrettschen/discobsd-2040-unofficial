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

from . import __version__, ports


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
    p.add_argument(
        "--to",
        metavar="URL",
        help="the tokenized console URL (deprecated: a token embedded here is "
        "visible to every local user via ps; use --host with DISCOBSD_WEB_TOKEN "
        "set or a systemd credential named web-token instead)",
    )
    p.add_argument(
        "--host",
        metavar="URL",
        help="the console URL with no token, e.g. http://192.0.2.5:7681/; the "
        "token is read from DISCOBSD_WEB_TOKEN or a systemd credential named "
        "web-token and appended as a query string",
    )
    p.add_argument("--port", type=int, default=42069, help="TCP port (default 42069)")
    p.add_argument("--bind", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    p.add_argument("--version", action="version", version="discobsd-link " + __version__)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    target = args.to
    if target:
        if "token=" in target:
            sys.stderr.write(
                "discobsd-link: --to with an embedded token is deprecated and "
                "visible to every local user via ps; use --host with "
                "DISCOBSD_WEB_TOKEN set or a systemd credential named web-token "
                "instead\n"
            )
    elif args.host:
        token = ports.credential(ports.TOKEN_CREDENTIAL, ports.TOKEN_ENV)
        if not token:
            sys.stderr.write(
                "discobsd-link: --host given but no token in DISCOBSD_WEB_TOKEN "
                "or a systemd credential named web-token\n"
            )
            return 1
        sep = "&" if "?" in args.host else "?"
        target = f"{args.host}{sep}token={token}"
    else:
        sys.stderr.write("discobsd-link: --to or --host is required\n")
        return 1
    httpd = RedirectServer((args.bind, args.port), target)
    print("discobsd-link: http://%s:%d/ -> %s" % (args.bind, args.port, target), flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
