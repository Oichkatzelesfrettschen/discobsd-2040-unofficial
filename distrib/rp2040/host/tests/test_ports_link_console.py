import threading
import types
import urllib.error
import urllib.request

import pytest

from discobsd_host import console, link, ports, term


def info(device, vid=None, pid=None, serial_number=None, product=None):
    return types.SimpleNamespace(
        device=device, vid=vid, pid=pid, serial_number=serial_number, product=product
    )


def test_board_is_selected_by_usb_identity():
    listing = [
        info("/dev/ttyUSB0", 0x0403, 0x6001, "FT1", "FT232"),
        info("/dev/ttyACM3", ports.USB_VID, ports.USB_PID, "rp2040", "DiscoBSD RP2040 console"),
        info("/dev/ttyACM1", ports.USB_VID, 0x0003, "E66", "Pico"),  # BOOTSEL bootrom
        info("COM7", ports.USB_VID, ports.USB_PID, "rp2040", None),
    ]
    assert ports.list_boards(lambda: listing) == ["/dev/ttyACM3", "COM7"]
    assert ports.find_board(lambda: listing, environ={}) == "/dev/ttyACM3"


def test_environment_override_wins():
    assert ports.find_board(lambda: [], environ={"DISCOBSD_PORT": "/dev/cu.x"}) == "/dev/cu.x"


def test_no_board_is_none(monkeypatch):
    monkeypatch.setattr(ports.glob, "glob", lambda pattern: [])
    assert ports.find_board(lambda: [], environ={}) is None


def test_windows_arrow_keys_become_vt100():
    assert term.translate_windows_key(0xE0, 0x48) == b"\x1b[A"
    assert term.translate_windows_key(0xE0, 0x4B) == b"\x1b[D"
    assert term.translate_windows_key(0xE0, 0x99) == b""


def test_term_cli_list_and_probe(capsys, monkeypatch):
    monkeypatch.setattr(ports, "list_boards", lambda: ["COM7"])
    assert term.main(["--list"]) == 0
    assert capsys.readouterr().out.strip() == "COM7"
    monkeypatch.setattr(ports, "list_boards", lambda: [])
    assert term.main(["--list"]) == 1

    class Line:
        def __init__(self):
            self.out = b"\r\nlogin: "

        def write(self, d):
            pass

        def read(self, n):
            d, self.out = self.out, b""
            return d

        def close(self):
            pass

    monkeypatch.setattr(ports, "find_board", lambda: "FAKE")
    assert term.probe(open_line=lambda dev: Line(), seconds=0.2) == 0
    assert "reachable" in capsys.readouterr().err


@pytest.fixture
def redirect():
    httpd = link.RedirectServer(("127.0.0.1", 0), "http://10.0.0.5:7681/?token=abc")
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        yield httpd
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_link_redirects_to_the_console(redirect):
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None

    opener = urllib.request.build_opener(NoRedirect)
    with pytest.raises(urllib.error.HTTPError) as err:
        opener.open("http://127.0.0.1:%d/anything" % redirect.server_address[1])
    assert err.value.code == 302
    assert err.value.headers["Location"] == "http://10.0.0.5:7681/?token=abc"
    assert err.value.headers["Cache-Control"] == "no-store"


def test_link_requires_target():
    with pytest.raises(SystemExit):
        link.main([])


def test_config_dir_per_platform(tmp_path):
    assert console.config_dir({}, "linux", tmp_path) == tmp_path / ".config" / "discobsd"
    assert (
        console.config_dir({"XDG_CONFIG_HOME": "/x"}, "linux", tmp_path).as_posix() == "/x/discobsd"
    )
    assert console.config_dir({}, "darwin", tmp_path) == (
        tmp_path / "Library" / "Application Support" / "discobsd"
    )
    win = console.config_dir({"APPDATA": str(tmp_path / "Roaming")}, "win32", tmp_path)
    assert win == tmp_path / "Roaming" / "discobsd"


def test_env_file_round_trip_is_private(tmp_path):
    path = tmp_path / "discobsd" / "web.env"
    console.write_env(path, {"DISCOBSD_WEB_TOKEN": "abc", "DISCOBSD_HOST_IP": "10.0.0.5"})
    assert console.read_env(path) == {"DISCOBSD_WEB_TOKEN": "abc", "DISCOBSD_HOST_IP": "10.0.0.5"}
    import os
    import sys

    if sys.platform != "win32":
        assert oct(os.stat(path).st_mode & 0o777) == "0o600"
    assert console.read_env(tmp_path / "missing") == {}


def test_urls_carry_token_and_short_link():
    out = console.urls("10.0.0.5", "abc", 7681, 42069)
    assert out["short"] == "http://10.0.0.5:42069/"
    assert out["full"] == "http://10.0.0.5:7681/?token=abc"
    assert out["mdns"].endswith(".local:42069/")


def test_console_cli_requires_a_command():
    with pytest.raises(SystemExit):
        console.main([])


def test_escape_menu_quits_sends_and_helps():
    e = term.Escape()
    assert e.feed(b"ls\r") == (b"ls\r", None)
    assert e.feed(b"\x1d") == (b"", None)  # armed, nothing sent
    assert e.feed(b"_") == (b"\x1f", None)  # leave V6
    assert e.feed(b"\x1dd") == (b"\x7f", None)  # DEL
    assert e.feed(b"\x1d\\") == (b"\x1c", None)  # Ctrl-backslash
    assert e.feed(b"\x1d]") == (b"\x1d", None)  # literal Ctrl-]
    assert e.feed(b"\x1d?") == (b"", "help")
    assert e.feed(b"ab\x1dq") == (b"ab", "quit")
    assert term.Escape().feed(b"\x1d\x1d") == (b"", "quit")


def test_escape_arms_across_reads():
    e = term.Escape()
    assert e.feed(b"\x1d") == (b"", None)
    assert e.feed(b"q") == (b"", "quit")
