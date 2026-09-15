import io
import socket
import threading
import urllib.error
import urllib.request

import pytest

from discobsd_host import web


def masked(payload: bytes, opcode: int = 0x1, mask: bytes = b"\x01\x02\x03\x04") -> bytes:
    n = len(payload)
    head = bytearray([0x80 | opcode])
    if n < 126:
        head.append(0x80 | n)
    elif n < 65536:
        head.append(0x80 | 126)
        head += n.to_bytes(2, "big")
    else:
        head.append(0x80 | 127)
        head += n.to_bytes(8, "big")
    body = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes(head) + mask + body


def test_frame_lengths_use_the_three_encodings():
    assert web.ws_frame(b"a")[:2] == b"\x81\x01"
    f = web.ws_frame(b"x" * 300)
    assert f[1] == 126 and f[2:4] == (300).to_bytes(2, "big")
    f = web.ws_frame(b"x" * 70000)
    assert f[1] == 127 and f[2:10] == (70000).to_bytes(8, "big")


def test_masked_client_frame_round_trips():
    op, data = web.ws_read(io.BytesIO(masked(b"ls -l\r")))
    assert (op, data) == (0x1, b"ls -l\r")
    op, data = web.ws_read(io.BytesIO(masked(b"y" * 1000)))
    assert (op, data) == (0x1, b"y" * 1000)


def test_oversized_and_64bit_frames_are_refused_before_payload():
    assert web.ws_read(io.BytesIO(masked(b"z" * 70000))) == (None, None)
    truncated = io.BytesIO(b"\x81\xfe")
    assert web.ws_read(truncated) == (None, None)
    assert web.ws_read(io.BytesIO(b"")) == (None, None)


def test_close_opcode_is_reported():
    op, _ = web.ws_read(io.BytesIO(masked(b"", opcode=0x8)))
    assert op == 0x8


def test_accept_key_matches_rfc_6455_example():
    assert web.ws_accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="


def test_token_sources():
    assert web.token_from_request("/?token=abc", "") == "abc"
    assert web.token_from_request("/", "Bearer xyz") == "xyz"
    assert web.token_from_request("/", "") is None


def test_authorization_rules():
    assert web.authorized(True, None, None)
    assert not web.authorized(False, None, None)
    assert web.authorized(False, "s3cret", "s3cret")
    assert not web.authorized(False, "s3cret", "wrong")
    assert not web.authorized(True, "s3cret", None)
    assert not web.authorized(False, "s3cret", None)


def test_origin_must_match_host():
    assert web.origin_ok(None, "10.0.0.5:7681")
    assert web.origin_ok("http://10.0.0.5:7681", "10.0.0.5:7681")
    assert not web.origin_ok("http://evil.example", "10.0.0.5:7681")


class FakeLine:
    """A serial line that echoes what it is sent and emits a banner."""

    def __init__(self):
        self.out = b"login: "
        self.written = b""
        self.closed = False

    def read(self, n):
        d, self.out = self.out[:n], self.out[n:]
        return d

    def write(self, data):
        self.written += data
        self.out += data

    def close(self):
        self.closed = True


@pytest.fixture
def server():
    lines = []

    def opener(device):
        line = FakeLine()
        lines.append(line)
        return line

    httpd = web.ConsoleServer(("127.0.0.1", 0), "FAKE", "tok", open_serial=opener)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        yield httpd, lines
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_page_requires_token_off_loopback(server):
    httpd, _ = server
    httpd.is_loopback = False
    base = "http://127.0.0.1:%d/" % httpd.server_address[1]
    with pytest.raises(urllib.error.HTTPError) as err:
        urllib.request.urlopen(base)
    assert err.value.code == 401
    page = urllib.request.urlopen(base + "?token=tok").read()
    assert b"DiscoBSD console" in page and b"xterm" in page
    req = urllib.request.Request(base, headers={"Authorization": "Bearer tok"})
    assert urllib.request.urlopen(req).status == 200


def test_page_has_no_raw_control_characters():
    assert not any(0 < b < 0x20 and b not in (9, 10, 13) for b in web.PAGE.encode())
    assert 'data-k="&#27;"' in web.PAGE
    for code in (127, 28, 31):  # DEL, Ctrl-backslash, Ctrl-underscore: the V6 keys
        assert 'data-k="&#%d;"' % code in web.PAGE
    assert 'id=bye' in web.PAGE


def ws_connect(port, token="tok", origin=None):
    s = socket.create_connection(("127.0.0.1", port))
    headers = [
        f"GET /ws?token={token} HTTP/1.1",
        "Host: 127.0.0.1:%d" % port,
        "Upgrade: websocket",
        "Connection: Upgrade",
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==",
        "Sec-WebSocket-Version: 13",
    ]
    if origin:
        headers.append("Origin: " + origin)
    s.sendall(("\r\n".join(headers) + "\r\n\r\n").encode())
    s.settimeout(3)
    status = s.recv(4096)
    return s, status


# The handshake response and the first frames the server writes can arrive
# in one TCP segment, so ws_connect's recv carries part of the payload. The
# caller seeds the accumulator with what it already holds; without the seed
# the wanted bytes sit in the caller's hand while this loop waits out its
# deadline on a socket that has nothing further to send.
def read_frames(s, want: bytes, timeout=3.0, got: bytes = b""):
    s.settimeout(timeout)
    while want not in got:
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        got += chunk
    return got


def test_websocket_bridges_bytes_and_holds_one_session(server):
    httpd, lines = server
    port = httpd.server_address[1]
    s, status = ws_connect(port)
    assert status.startswith(b"HTTP/1.1 101")
    assert b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in status
    got = read_frames(s, b"login: ", got=status)
    assert b"login: " in got
    s.sendall(masked(b"operator\r"))
    got = read_frames(s, b"operator\r")
    assert lines[0].written.endswith(b"operator\r")
    second, status2 = ws_connect(port)
    assert status2.startswith(b"HTTP/1.1 101")
    busy = read_frames(second, b"in use", got=status2)
    assert b"in use" in busy
    second.close()
    s.sendall(masked(b"", opcode=0x8))
    s.close()
    deadline = 50
    while not lines[0].closed and deadline:
        threading.Event().wait(0.05)
        deadline -= 1
    assert lines[0].closed
    third, status3 = ws_connect(port)
    assert status3.startswith(b"HTTP/1.1 101")
    assert b"login: " in read_frames(third, b"login: ", got=status3)
    third.close()


def test_websocket_rejects_foreign_origin(server):
    httpd, _ = server
    s, status = ws_connect(httpd.server_address[1], origin="http://evil.example")
    assert status.startswith(b"HTTP/1.1 403")
    s.close()


def test_cli_refuses_lan_bind_without_token(capsys, monkeypatch):
    monkeypatch.setattr(web.ports, "find_board", lambda: "FAKE")
    assert web.main(["--bind", "0.0.0.0"]) == 1
    assert "refusing" in capsys.readouterr().err
