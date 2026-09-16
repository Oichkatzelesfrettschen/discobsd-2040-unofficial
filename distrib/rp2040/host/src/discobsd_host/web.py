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
import select
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
# A viewer that says nothing for this long is sent a ping (RFC 6455
# section 5.5.2); one that answers nothing for another interval is gone,
# and its session is released. A page kept alive without its socket being
# closed (a tab restored from the back-forward cache, a laptop that left
# the LAN) otherwise holds the one console forever.
PING_INTERVAL = 15.0
XTERM = "https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.min.js"
XTERM_CSS = "https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.min.css"

PAGE = f"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,user-scalable=no">
<title>DiscoBSD console</title>
<link rel=stylesheet href="{XTERM_CSS}">
<style>html,body{{margin:0;height:100%;width:100%;background:#000;overflow:hidden}}
#t{{position:absolute;top:0;left:0}}
#s{{position:fixed;top:4px;right:8px;color:#6a6;font:12px monospace;z-index:9}}
#k{{position:fixed;bottom:0;left:0;right:0;display:block;background:#111;padding:4px;z-index:9}}
#k button.on{{background:#6a6;color:#000}}
#k button:focus-visible,#show:focus-visible{{outline:3px solid #fc0;outline-offset:1px}}
#show{{display:none;position:fixed;bottom:0;right:0;font:16px monospace;color:#ddd;background:#333;border:1px solid #555;padding:6px 10px;z-index:9}}
#k .g{{color:#888;font:12px monospace;margin:0 4px 0 8px}}
#h{{display:none;position:fixed;left:8px;right:8px;bottom:56px;background:#222;color:#ddd;font:14px monospace;padding:8px;border:1px solid #555;z-index:10}}
#k button{{font:16px monospace;color:#ddd;background:#333;border:1px solid #555;margin:2px;padding:6px 10px}}
</style></head><body>
<div id=s role=status aria-live=polite>connecting</div><div id=t role=application aria-label="DiscoBSD console"></div>
<div id=k><span class=g>DiscoBSD</span><button data-k="&#27;">Esc</button><button data-k="&#9;">Tab</button><button id=ctl aria-pressed=false>Ctrl</button><button data-k="&#3;">^C</button><button data-k="&#4;">^D</button><button data-k="&#26;">^Z</button><button data-k="&#12;">^L</button><button data-k="&#21;">^U</button><button data-k="&#18;">^R</button><span class=g>V6</span><button data-k="&#127;" title="V6 interrupt: DEL">DEL intr</button><button data-k="#" title="V6 erase one character: #"># erase</button><button data-k="@" title="V6 erase the line: @">@ kill</button><button data-k="&#28;" aria-label="Control backslash, quit in V6" title="V6 quit: Ctrl-backslash">^&#92; quit</button><button data-k="&#31;" title="leave the V6 emulator: Ctrl-underscore">^_ exit V6</button><span class=g></span><button id=paste>Paste</button><button data-k="&#27;[A">&uarr;</button><button data-k="&#27;[B">&darr;</button><button data-k="&#27;[D">&larr;</button><button data-k="&#27;[C">&rarr;</button><button id=bye title="sync, leave V6 if inside it, sync, log out of DiscoBSD, then close the session">Sync &amp; leave</button><button id=help aria-expanded=false aria-controls=h>keys</button><button id=reader aria-pressed=false title="screen reader mode: announce output and expose the screen to assistive technology">reader</button><button id=hide>hide</button></div><button id=show aria-label="show the key bar">keys</button><div id=h role=region aria-label="key reference"><b>DiscoBSD</b> ($ or # prompt, whoami works): Ctrl-C interrupt, DEL erase, Ctrl-U kill line, Ctrl-D log out, Ctrl-&#92; quit, Ctrl-Z suspend, Ctrl-L redraw, Ctrl-R history.<br><b>V6</b> (# prompt, whoami not found, dates in 1970): DEL interrupt (Backspace sends DEL), # erase, @ kill line, Ctrl-D log out, Ctrl-&#92; quit. Ctrl-C, Ctrl-U and arrows do nothing.<br><b>Leave</b>: in V6 type sync then press ^_ exit V6 (or type ~. at a line start); in DiscoBSD type exit to reach login:. Sync &amp; leave does all of that and closes the session. Unplug only after sync.</div>
<script src="{XTERM}"></script>
<script>
var reader=false;try{{reader=localStorage.getItem("reader")==="1";}}catch(e){{}}
var term=new Terminal({{cols:80,rows:24,fontFamily:"monospace",fontSize:14,
 cursorBlink:true,theme:{{background:"#000000"}},minimumContrastRatio:4.5,
 screenReaderMode:reader}});
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
ws.onopen=function(){{stat.textContent="connected -- DiscoBSD";grab();}};
var byebye=false;
ws.onclose=function(){{if(!byebye)stat.textContent="disconnected -- reload to retry";}};
// Leaving the page releases the console at once; a page the browser
// brings back from its back-forward cache reloads and reconnects rather
// than resuming a socket the server has since dropped.
window.addEventListener("pagehide",function(){{byebye=true;try{{ws.close();}}catch(x){{}}}});
window.addEventListener("pageshow",function(e){{if(e.persisted)location.reload();}});
// The page tells DiscoBSD from V6 by the emulator's own banner and exit
// line, and says so in the status line for a screen reader or a glance.
var inv6=false;
ws.onmessage=function(e){{
 var d=typeof e.data==="string"?e.data:
  new TextDecoder("latin1").decode(new Uint8Array(e.data));
 if(!inv6&&d.indexOf("pdp11: ")>=0&&d.indexOf("RK05")>=0){{inv6=true;stat.textContent="connected -- inside V6 (pdp11)";}}
 else if(inv6&&d.indexOf("[pdp11: ")>=0){{inv6=false;stat.textContent="connected -- DiscoBSD";}}
 term.write(d);}};
// Key bar: every browser keeps some Ctrl combinations for itself (Ctrl-C
// with a selection copies, Ctrl-D bookmarks, Ctrl-W closes the tab, Ctrl
// with minus or underscore zooms), and a touch keyboard has none, so each
// button sends the bytes the key would. The V6 group is what Sixth
// Edition inside pdp11 answers to: DEL interrupts, Ctrl-backslash quits,
// and Ctrl-_ leaves the emulator.
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
ctl.addEventListener("click",function(e){{e.preventDefault();ctrlArmed=!ctrlArmed;ctl.className=ctrlArmed?"on":"";
 ctl.setAttribute("aria-pressed",ctrlArmed?"true":"false");grab();}});
// Accessible names for the keys: the label says what the key does in the
// system that reads it, so a screen reader hears more than "caret C".
var NAMES={{"Esc":"Escape","Tab":"Tab","^C":"Control C, interrupt in DiscoBSD","^D":"Control D, end of input or log out",
 "^Z":"Control Z, suspend in DiscoBSD","^L":"Control L, redraw","^U":"Control U, erase the line in DiscoBSD","^R":"Control R, history search in DiscoBSD",
 "DEL intr":"Delete, interrupt in V6","# erase":"number sign, erase a character in V6","@ kill":"at sign, erase the line in V6",
 "^\\ quit":"Control backslash, quit in V6","^_ exit V6":"Control underscore, leave the V6 emulator","Paste":"paste from the clipboard",
 "\u2191":"up arrow","\u2193":"down arrow","\u2190":"left arrow","\u2192":"right arrow","Sync & leave":"sync, leave V6, log out, close the session",
 "keys":"show the key reference","reader":"screen reader mode","hide":"hide the key bar","Ctrl":"control modifier for the next key"}};
Array.prototype.forEach.call(document.querySelectorAll("#k button"),function(b){{var n=NAMES[b.textContent];if(n)b.setAttribute("aria-label",n);}});
var rb=document.getElementById("reader");
function setreader(on){{reader=on;term.options.screenReaderMode=on;rb.className=on?"on":"";rb.setAttribute("aria-pressed",on?"true":"false");
 try{{localStorage.setItem("reader",on?"1":"0");}}catch(e){{}}stat.textContent=on?"screen reader mode on":"screen reader mode off";}}
rb.addEventListener("click",function(e){{e.preventDefault();setreader(!reader);grab();}});
if(reader){{rb.className="on";rb.setAttribute("aria-pressed","true");}}
document.getElementById("paste").addEventListener("click",function(e){{e.preventDefault();
 if(navigator.clipboard&&navigator.clipboard.readText){{navigator.clipboard.readText().then(function(t){{sendkeys(t);grab();}});}}
 else{{var t=window.prompt("Paste text to send:");if(t!==null)sendkeys(t);grab();}}}});
// Sync & leave types the clean exit from either system, since the page
// cannot tell which one has the console: sync (both systems), Ctrl-_
// (leaves V6, nothing in DiscoBSD), sync again (now DiscoBSD if we were
// in V6), exit (back to login:), then closes the socket so the server
// frees the console for the next session. It assumes a shell prompt.
var leaving=false;
function later(f,ms){{return new Promise(function(r){{setTimeout(function(){{f();r();}},ms);}});}}
document.getElementById("bye").addEventListener("click",function(e){{e.preventDefault();
 if(leaving)return;leaving=true;
 function say(m){{stat.textContent=m;term.write("\\r\\n\\x1b[33m[console: "+m+"]\\x1b[0m\\r\\n");}}
 later(function(){{say("sync");sendkeys("\\r");}},0)
 .then(function(){{return later(function(){{sendkeys("sync\\r");}},400);}})
 .then(function(){{return later(function(){{say("leaving V6 if inside it");sendkeys("\\x1f");}},2500);}})
 .then(function(){{return later(function(){{say("sync");sendkeys("\\r");}},1500);}})
 .then(function(){{return later(function(){{sendkeys("sync\\r");}},400);}})
 .then(function(){{return later(function(){{say("logging out of DiscoBSD");sendkeys("exit\\r");}},2500);}})
 .then(function(){{return later(function(){{say("session closed -- reload to reconnect");byebye=true;try{{ws.close();}}catch(x){{}}}},1500);}});}});
var help=document.getElementById("h");
document.getElementById("help").addEventListener("click",function(e){{e.preventDefault();
 var on=help.style.display!=="block";help.style.display=on?"block":"none";
 e.target.setAttribute("aria-expanded",on?"true":"false");if(on)help.focus();else grab();}});
help.setAttribute("tabindex","-1");
// hide folds the bar down to one "keys" tab, so a phone gets the screen
// back and still has a way to the keys and to leaving.
var showb=document.getElementById("show");
document.getElementById("hide").addEventListener("click",function(e){{e.preventDefault();
 document.getElementById("k").style.display="none";showb.style.display="block";fit();grab();}});
showb.addEventListener("click",function(e){{e.preventDefault();
 document.getElementById("k").style.display="block";showb.style.display="none";fit();grab();}});
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


class Silence(Exception):
    """No byte from the viewer for one ping interval, at a frame boundary."""


class FrameReader:
    """Bytes from the viewer's socket for ws_read, with silence detection.

    The HTTP handler's rfile cannot serve here: a socket file object
    refuses every read after one timeout ("cannot read from timed out
    object"), so a timed-out read would end the session on the spot. This
    reader waits with select() instead and raises Silence when nothing
    arrives within the interval at the start of a frame; inside a frame it
    keeps waiting, so a keystroke split across segments is never lost.
    `pre` is whatever the handler had already buffered past the handshake.
    """

    def __init__(self, sock, pre: bytes, interval: float):
        self.sock = sock
        self.buf = bytearray(pre)
        self.interval = interval

    def read(self, n: int) -> bytes:
        while len(self.buf) < n:
            ready, _, _ = select.select([self.sock], [], [], self.interval)
            if not ready:
                if not self.buf:
                    raise Silence()
                continue
            d = self.sock.recv(65536)
            if not d:
                break
            self.buf += d
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out


def buffered_after_handshake(rfile, sock) -> bytes:
    """Bytes the request handler read ahead of the handshake, without
    blocking: the socket is made non-blocking for one read1(), which
    returns the buffer if there is one and nothing if there is not."""
    sock.setblocking(False)
    try:
        return rfile.read1(MAX_CLIENT_FRAME) or b""
    except (BlockingIOError, OSError):
        return b""
    finally:
        sock.setblocking(True)


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
        # Two threads write to the socket, the serial reader and the
        # control replies below; a frame must not be interleaved with
        # another.
        send_lock = threading.Lock()

        def send(frame):
            with send_lock:
                sock.sendall(frame)

        def reader():
            while alive[0]:
                try:
                    d = ser.read(4096)
                except Exception:
                    break
                if d:
                    try:
                        send(ws_frame(d))
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
        # Silence for one interval draws a ping; silence through a second
        # one ends the session. The socket timeout bounds sendall in the
        # reader thread, so a viewer that stops draining is dropped from
        # that side as well.
        frames = FrameReader(
            sock, buffered_after_handshake(self.rfile, sock), srv.ping_interval
        )
        sock.settimeout(srv.ping_interval)
        awaiting_pong = False
        try:
            while alive[0]:
                try:
                    op, data = ws_read(frames)
                except Silence:
                    if awaiting_pong:
                        break
                    try:
                        send(ws_frame(b"", 0x9))
                    except OSError:
                        break
                    awaiting_pong = True
                    continue
                awaiting_pong = False
                if op is None or op == 0x8:
                    break
                if op == 0x9:
                    try:
                        send(ws_frame(data, 0xA))
                    except OSError:
                        break
                    continue
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
        self.ping_interval = PING_INTERVAL


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
