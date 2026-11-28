"""gizmobsd-link: a short URL for the gizmobsd-web console.

gizmobsd-web needs its token in the URL, which is long to type on a phone.
This serves one page on a memorable port that redirects to the full
tokenized console URL. The request must present the same token, so the
redirect cannot disclose console authority to an unauthenticated peer.
"""

from __future__ import annotations

import argparse
import hmac
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

from . import __version__, ports


class RedirectServer(HTTPServer):
    allow_reuse_address = True

    def __init__(self, address, target: str, token: str):
        super().__init__(address, RedirectHandler)
        self.target = target
        self.token = token


class RedirectHandler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(5.0)

    def do_GET(self):
        presented = (parse_qs(urlparse(self.path).query).get("token") or [None])[0]
        if presented is None or not hmac.compare_digest(
            presented.encode(), self.server.token.encode()
        ):
            self.send_response(401)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(302)
        self.send_header("Location", self.server.target)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_HEAD(self):
        self.do_GET()

    def log_message(self, fmt, *a):
        pass


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gizmobsd-link", description="Redirect a short URL to the gizmobsd-web console."
    )
    p.add_argument(
        "--to",
        metavar="URL",
        help="the tokenized console URL (deprecated: a token embedded here is "
        "visible to every local user via ps; use --host with GIZMOBSD_WEB_TOKEN "
        "set or a systemd credential named web-token instead)",
    )
    p.add_argument(
        "--host",
        metavar="URL",
        help="the console URL with no token, e.g. http://192.0.2.5:7681/; the "
        "token is read from GIZMOBSD_WEB_TOKEN or a systemd credential named "
        "web-token and appended as a query string",
    )
    p.add_argument("--port", type=int, default=42069, help="TCP port (default 42069)")
    p.add_argument("--bind", default="127.0.0.1", help="bind address (default 127.0.0.1)")
    p.add_argument("--version", action="version", version="gizmobsd-link " + __version__)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    token = ports.credential(ports.TOKEN_CREDENTIAL, ports.TOKEN_ENV)
    target = args.to
    if target:
        if "token=" in target:
            sys.stderr.write(
                "gizmobsd-link: --to with an embedded token is deprecated and "
                "visible to every local user via ps; use --host with "
                "GIZMOBSD_WEB_TOKEN set or a systemd credential named web-token "
                "instead\n"
            )
    elif args.host:
        if not token:
            sys.stderr.write(
                "gizmobsd-link: --host given but no token in GIZMOBSD_WEB_TOKEN "
                "or a systemd credential named web-token\n"
            )
            return 1
        sep = "&" if "?" in args.host else "?"
        target = f"{args.host}{sep}token={token}"
    else:
        sys.stderr.write("gizmobsd-link: --to or --host is required\n")
        return 1
    if not token:
        sys.stderr.write("gizmobsd-link: a token is required for the redirect service\n")
        return 1
    httpd = RedirectServer((args.bind, args.port), target, token)
    print(
        "gizmobsd-link: listening on http://%s:%d/ (token required)"
        % (args.bind, args.port),
        flush=True,
    )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
