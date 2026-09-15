"""discobsd-web: the DiscoBSD RP2040 console as a browser terminal.

The board has no network of its own, so the host it plugs into serves as
the gateway: this program bridges the serial line to a browser terminal
over a WebSocket, so any device on the local network opens the console at
http://<this-host>:7681/. It needs Python and pyserial; the terminal itself
is xterm.js, loaded from a CDN by the browser.

Security model: a loopback bind needs no token. Any other bind requires a
shared secret as a Bearer header or ?token= query, compared in constant
time, and a WebSocket upgrade must carry the Origin of the page this server
served. The console is one serial line, so one session holds it at a time.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import __version__, ports

WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_CLIENT_FRAME = 65536
XTERM = "https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.min.js"
XTERM_CSS = "https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.min.css"

PAGE = f"""<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,user-scalable=no">
<title>DiscoBSD console</title>
<link rel=stylesheet href="{XTERM_CSS}">
<style>html,body{{margin:0;height:100%;width:100%;background:#000;overflow:hidden}}
#t{{position:absolute;top:0;left:0}}
#s{{position:fixed;top:4px;right:8px;color:#6a6;font:12px monospace;z-index:9}}
#k{{position:fixed;bottom:0;left:0;right:0;display:block;background:#111;padding:4px;z-index:9}}
#k button.on{{background:#6a6;color:#000}}
#k button{{font:16px monospace;color:#ddd;background:#333;border:1px solid #555;margin:2px;padding:6px 10px}}
</style></head><body>
<div id=s>connecting</div><div id=t></div>
<div id=k><button data-k="&#27;">Esc</button><button data-k="&#9;">Tab</button><button id=ctl>Ctrl</button><button data-k="&#3;">^C</button><button data-k="&#4;">^D</button><button data-k="&#26;">^Z</button><button data-k="&#12;">^L</button><button data-k="&#21;">^U</button><button data-k="&#18;">^R</button><button id=paste>Paste</button><button data-k="&#27;[A">&uarr;</button><button data-k="&#27;[B">&darr;</button><button data-k="&#27;[D">&larr;</button><button data-k="&#27;[C">&rarr;</button><button id=hide>hide</button></div>
<script src="{XTERM}"></script>
<script>
var term=new Terminal({{cols:80,rows:24,fontFamily:"monospace",fontSize:14,
 cursorBlink:true,theme:{{background:"#000000"}}}});
term.open(document.getElementById("t"));
var stat=document.getElementById("s");

// Fill the window: keep the console 80x24 and pick the largest font at
// which that grid fits, so the terminal grows and shrinks with the page
// while the board's size never changes. A monospace cell is about 0.6 of
// the font wide and about 1.2 tall.
function fit(){{
 var bar=document.getElementById("k"), bh=bar&&getComputedStyle(bar).display!=="none"?bar.offsetHeight:0;
 var fw=window.innerWidth/(80*0.6), fh=(window.innerHeight-bh)/(24*1.2);
 var fs=Math.max(8,Math.floor(Math.min(fw,fh)));
 if(fs!==term.options.fontSize){{term.options.fontSize=fs;}}
 try{{term.resize(80,24);}}catch(e){{}}
}}
window.addEventListener("resize",fit);
fit();

// Keep the terminal focused so typing goes to the console, not to
// Firefox quick-find, when the page is clicked or returned to.
function grab(){{try{{term.focus();}}catch(e){{}}}}
window.addEventListener("focus",grab);
window.addEventListener("load",grab);
document.addEventListener("click",grab);
document.addEventListener("visibilitychange",function(){{if(!document.hidden)grab();}});

var proto=location.protocol==="https:"?"wss":"ws";
var ws=new WebSocket(proto+"://"+location.host+"/ws"+location.search);
ws.binaryType="arraybuffer";
ws.onopen=function(){{stat.textContent="connected";grab();}};
ws.onclose=function(){{stat.textContent="disconnected -- reload to retry";}};
ws.onmessage=function(e){{
 var d=typeof e.data==="string"?e.data:
  new TextDecoder("latin1").decode(new Uint8Array(e.data));
 term.write(d);}};
// Key bar: every browser keeps some Ctrl combinations for itself (Ctrl-C
// with a selection copies, Ctrl-D bookmarks, Ctrl-W closes the tab), and a
// touch keyboard has none, so each button sends the bytes the key would.
// Ctrl arms a one-shot modifier: the next typed character goes out as its
// control code. Paste reads the clipboard and sends it as typed input.
var ctrlArmed=false, ctl=document.getElementById("ctl");
function sendkeys(d){{if(ws.readyState===1)ws.send(d);}}
term.onData(function(d){{
 if(ctrlArmed&&d.length===1){{ctrlArmed=false;ctl.className="";
  var c=d.toUpperCase().charCodeAt(0)&0x1f;sendkeys(String.fromCharCode(c));return;}}
 sendkeys(d);}});
Array.prototype.forEach.call(document.querySelectorAll("#k button[data-k]"),function(b){{
 b.addEventListener("click",function(e){{e.preventDefault();sendkeys(b.getAttribute("data-k"));grab();}});}});
ctl.addEventListener("click",function(e){{e.preventDefault();ctrlArmed=!ctrlArmed;ctl.className=ctrlArmed?"on":"";grab();}});
document.getElementById("paste").addEventListener("click",function(e){{e.preventDefault();
 if(navigator.clipboard&&navigator.clipboard.readText){{navigator.clipboard.readText().then(function(t){{sendkeys(t);grab();}});}}
 else{{var t=window.prompt("Paste text to send:");if(t!==null)sendkeys(t);grab();}}}});
document.getElementById("hide").addEventListener("click",function(e){{e.preventDefault();
 document.getElementById("k").style.display="none";fit();grab();}});
</script></body></html>"""


def ws_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    """One unmasked server-to-client frame (RFC 6455 section 5.2)."""
    n = len(payload)
    head = bytearray([0x80 | opcode])
    if n < 126:
        head.append(n)
    elif n < 65536:
        head.append(126)
        head += struct.pack(">H", n)
    else:
        head.append(127)
        head += struct.pack(">Q", n)
    return bytes(head) + payload


def ws_read(rf):
    """Read one client frame; return (opcode, payload) or (None, None).

    A 64-bit length marker and any frame above MAX_CLIENT_FRAME are refused
    before their payload is read: keystroke traffic never needs either.
    """
    h = rf.read(2)
    if len(h) < 2:
        return None, None
    op = h[0] & 0x0F
    masked = h[1] & 0x80
    ln = h[1] & 0x7F
    if ln == 126:
        h2 = rf.read(2)
        if len(h2) < 2:
            return None, None
        ln = struct.unpack(">H", h2)[0]
    elif ln == 127:
        return None, None
    if ln > MAX_CLIENT_FRAME:
        return None, None
    mask = rf.read(4) if masked else b"\x00\x00\x00\x00"
    data = rf.read(ln)
    if masked:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    return op, data


def ws_accept(key: str) -> str:
    return base64.b64encode(hashlib.sha1((key + WS_MAGIC).encode()).digest()).decode()


def token_from_request(path: str, authorization: str) -> str | None:
    if authorization.startswith("Bearer "):
        return authorization[7:]
    return (parse_qs(urlparse(path).query).get("token") or [None])[0]


def is_loopback(bind: str) -> bool:
    return bind in ("127.0.0.1", "::1", "localhost")


def authorized(is_loopback_bind: bool, token: str | None, presented: str | None) -> bool:
    if is_loopback_bind and not token:
        return True
    return bool(token) and presented is not None and hmac.compare_digest(presented, token)


def origin_ok(origin: str | None, host: str) -> bool:
    """A WebSocket upgrade may only come from the page this server served."""
    if origin is None:
        return True
    return urlparse(origin).netloc == host


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _authorized(self):
        srv = self.server
        presented = token_from_request(self.path, self.headers.get("Authorization", ""))
        if authorized(srv.is_loopback, srv.token, presented):
            return True
        self.send_response(401)
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def do_GET(self):
        if not self._authorized():
            return
        if (
            self.path.split("?")[0] == "/ws"
            and self.headers.get("Upgrade", "").lower() == "websocket"
        ):
            return self.serve_ws()
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def serve_ws(self):
        if not origin_ok(self.headers.get("Origin"), self.headers.get("Host", "")):
            self.send_response(403)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header(
            "Sec-WebSocket-Accept", ws_accept(self.headers.get("Sec-WebSocket-Key", ""))
        )
        self.end_headers()
        sock = self.connection
        srv = self.server
        # One console at a time: a serial line is a single session. A second
        # viewer is told the console is busy and closed, rather than fighting
        # over the same port.
        if not srv.console_lock.acquire(blocking=False):
            try:
                sock.sendall(
                    ws_frame(
                        b"\r\n  The console is in use by another session.\r\n  Try again later.\r\n"
                    )
                )
                time.sleep(0.2)
            except OSError:
                pass
            return
        try:
            device = srv.device or ports.find_board()
            if not device:
                raise OSError("no board found")
            ser = srv.open_serial(device)
        except Exception as exc:
            try:
                sock.sendall(ws_frame((f"\r\ncannot open the console: {exc}\r\n").encode()))
            except OSError:
                pass
            srv.console_lock.release()
            return
        alive = [True]

        def reader():
            while alive[0]:
                try:
                    d = ser.read(4096)
                except Exception:
                    break
                if d:
                    try:
                        sock.sendall(ws_frame(d))
                    except OSError:
                        break
                else:
                    time.sleep(0.02)
            alive[0] = False

        threading.Thread(target=reader, daemon=True).start()
        # getty printed its prompt before this client connected, so prod the
        # line to redraw the current prompt into the fresh terminal.
        try:
            ser.write(b"\r")
        except Exception:
            pass
        try:
            while alive[0]:
                op, data = ws_read(self.rfile)
                if op is None or op == 0x8:
                    break
                if op in (0x1, 0x2):
                    try:
                        ser.write(data)
                    except Exception:
                        break
        finally:
            alive[0] = False
            try:
                ser.close()
            except Exception:
                pass
            srv.console_lock.release()


class ConsoleServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, device, token, open_serial=ports.open_serial):
        super().__init__(address, Handler)
        self.device = device
        self.token = token
        self.is_loopback = is_loopback(address[0])
        self.console_lock = threading.Lock()
        self.open_serial = open_serial


def lan_ip() -> str:
    """The address this host uses toward the default route; a connected UDP
    socket sends nothing and asks the kernel which source it would pick."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
        finally:
            s.close()
    except OSError:
        return "127.0.0.1"


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="discobsd-web",
        description="Serve the DiscoBSD RP2040 console as a browser terminal.",
    )
    p.add_argument("device", nargs="?", help="serial device (default: find the board)")
    p.add_argument("--port", type=int, default=7681, help="TCP port (default 7681)")
    p.add_argument("--bind", default="127.0.0.1", help="bind address (default 127.0.0.1)")
    p.add_argument("--token", help="shared secret required for a non-loopback bind")
    p.add_argument("--version", action="version", version="discobsd-web " + __version__)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        import serial  # noqa: F401
    except ImportError:
        sys.stderr.write(f"discobsd-web: {ports.pyserial_hint()}\n")
        return 1
    device = args.device or ports.find_board()
    if not device:
        sys.stderr.write("discobsd-web: no board found (plug it in, or set DISCOBSD_PORT)\n")
        return 1
    if not is_loopback(args.bind) and not args.token:
        sys.stderr.write(
            "discobsd-web: refusing a non-loopback bind without --token; a bare "
            "0.0.0.0 bind exposes a login console to the LAN with no authentication\n"
        )
        return 1
    httpd = ConsoleServer((args.bind, args.port), device, args.token)
    q = ("?token=" + args.token) if args.token else ""
    sys.stderr.write(f"DiscoBSD web console for {device}\n")
    if httpd.is_loopback:
        sys.stderr.write(
            "  open http://127.0.0.1:%d/%s (loopback only; pass --bind 0.0.0.0 "
            "--token SECRET for the LAN) -- Ctrl-C to stop\n" % (args.port, q)
        )
    else:
        sys.stderr.write(
            "  open http://%s:%d/%s from the LAN (token required) -- Ctrl-C to stop\n"
            % (lan_ip(), args.port, q)
        )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\nstopped\n")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
