"""USB port selection in gizmobsd-term and gizmobsd-web with two boards attached.

Both tools resolve their board through ports.find_board(), the one rule that
gizmobsd-flash and gizmobsd-connect implement in shell: more than one 2e8a
device with none named is a refusal that lists the ports, and --usb-port or
DISCOBSD_USB_PORT names the board by its sysfs port. The tests run against a
fake sysfs and devfs tree; no serial device is opened (serial.Serial is
replaced) and no real board is read. gizmobsd-link never resolves a board (it
only redirects to a URL), so it takes zero selectors and has zero cases here.
"""

from __future__ import annotations

import sys
import types

import pytest
from test_web import masked

from gizmobsd_host import ports, term, web

PORT_A = "1-3"
PORT_B = "1-4"


def make_tree(tmp_path, boards):
    """Fake sysfs and devfs with {usb_port: tty_name} boards; returns the env.

    The tree copies Linux sysfs, whose interface directories are named
    "<port>:<config>.<interface>". Windows rejects ':' in a file name, and
    the selector reads sysfs only on Linux, so a test built on this tree is
    skipped there; test_selector_without_sysfs_is_unsupported covers the
    selector on a host without sysfs.
    """
    if sys.platform == "win32":
        pytest.skip("the fake tree models Linux sysfs; ':' is not a Windows file name character")
    sysfs = tmp_path / "sys"
    devfs = tmp_path / "dev"
    sysfs.mkdir()
    devfs.mkdir()
    for usb_port, tty_name in boards.items():
        node = sysfs / usb_port
        (node / f"{usb_port}:1.0" / "tty" / tty_name).mkdir(parents=True)
        (node / "idVendor").write_text("2e8a\n")
        (node / "idProduct").write_text("000a\n")
        (node / "product").write_text("GizmoBSD RP2040 console\n")
        (devfs / tty_name).write_text("")
    # An unrelated USB device must not count as a board.
    other = sysfs / "1-1"
    other.mkdir()
    (other / "idVendor").write_text("046d\n")
    return {"DISCOBSD_SYSFS_USB": str(sysfs), "DISCOBSD_DEVFS": str(devfs)}


class FakeSerial:
    opened: list[str] = []

    def __init__(self, device, *args, **kwargs):
        FakeSerial.opened.append(device)

    def write(self, data):
        return len(data)

    def read(self, n):
        return b"login: "

    def close(self):
        pass


@pytest.fixture
def launch(monkeypatch, tmp_path):
    """Return run(tool, argv, env) -> (exit code, device the tool used, stderr)."""
    import serial

    monkeypatch.setattr(serial, "Serial", FakeSerial)
    FakeSerial.opened = []
    # Discovery with one board attached and nothing named.
    monkeypatch.setattr(ports, "list_boards", lambda comports=None: ["/dev/discovered"])
    ticks = iter(range(0, 10**6, 2))
    monkeypatch.setattr(
        term, "time", types.SimpleNamespace(time=lambda: next(ticks), sleep=lambda s: None)
    )
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT, ports.TOKEN_ENV):
        monkeypatch.delenv(name, raising=False)

    def run(tool, argv, env, capsys):
        for key, value in env.items():
            monkeypatch.setenv(key, value)
        FakeSerial.opened = []
        if tool == "term":
            rc = term.main(["--probe", *argv])
            device = FakeSerial.opened[0] if FakeSerial.opened else None
        else:

            class FakeServer:
                def __init__(self, address, device, token, open_serial=None, usb_port=None):
                    self.token = token
                    self.is_loopback = True

                def serve_forever(self):
                    raise KeyboardInterrupt()

                def server_close(self):
                    pass

            monkeypatch.setattr(web, "ConsoleServer", FakeServer)
            rc = web.main(argv)
            device = None
        err = capsys.readouterr().err
        if tool == "web":
            # The banner names the board resolved at startup.
            marker = "DiscoBSD web console for "
            for line in err.splitlines():
                if line.startswith(marker):
                    device = line[len(marker) :]
        return rc, device, err

    return run


TOOLS = ("term", "web")


@pytest.mark.parametrize("tool", TOOLS)
def test_one_board_is_taken_without_a_selector(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0"})
    rc, device, _ = launch(tool, [], env, capsys)
    assert (rc, device) == (0, "/dev/discovered")


@pytest.mark.parametrize("tool", TOOLS)
def test_two_boards_unnamed_refuse_and_list_both_ports(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    rc, device, err = launch(tool, [], env, capsys)
    assert rc == 2
    assert device is None
    assert PORT_A in err and PORT_B in err
    assert "--usb-port" in err and "DISCOBSD_USB_PORT" in err


@pytest.mark.parametrize("tool", TOOLS)
def test_flag_names_one_of_two_boards(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    rc, device, _ = launch(tool, ["--usb-port", PORT_B], env, capsys)
    assert (rc, device) == (0, env["DISCOBSD_DEVFS"] + "/ttyACM1")


@pytest.mark.parametrize("tool", TOOLS)
def test_environment_names_one_of_two_boards(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    env["DISCOBSD_USB_PORT"] = PORT_A
    rc, device, _ = launch(tool, [], env, capsys)
    assert (rc, device) == (0, env["DISCOBSD_DEVFS"] + "/ttyACM0")


@pytest.mark.parametrize("tool", TOOLS)
def test_flag_wins_over_environment(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    env["DISCOBSD_USB_PORT"] = PORT_A
    rc, device, _ = launch(tool, ["--usb-port", PORT_B], env, capsys)
    assert (rc, device) == (0, env["DISCOBSD_DEVFS"] + "/ttyACM1")


@pytest.mark.parametrize("tool", TOOLS)
def test_named_port_absent_is_refused(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    rc, device, err = launch(tool, ["--usb-port", "1-9"], env, capsys)
    assert rc == 1
    assert device is None
    assert "1-9 holds no 2e8a device" in err


@pytest.mark.parametrize("tool", TOOLS)
@pytest.mark.parametrize("operand", ["", "1-3/../x", "1-", "usb1", "1-3 "])
def test_malformed_port_is_refused(tool, operand, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    rc, device, err = launch(tool, ["--usb-port", operand], env, capsys)
    assert rc == 1
    assert device is None
    assert "--usb-port" in err or "not a USB port path" in err


@pytest.mark.parametrize("tool", TOOLS)
def test_empty_environment_value_counts_as_unset(tool, launch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0"})
    env["DISCOBSD_USB_PORT"] = ""
    rc, device, _ = launch(tool, [], env, capsys)
    assert (rc, device) == (0, "/dev/discovered")


def test_by_path_link_replaces_the_renumbered_tty(tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    devfs = tmp_path / "dev"
    (devfs / "serial" / "by-path").mkdir(parents=True)
    link = devfs / "serial" / "by-path" / "pci-0000:00:14.0-usb-0:4:1.0"
    link.symlink_to(devfs / "ttyACM1")
    assert ports.find_board(environ=env, usb_port=PORT_B) == str(link)
    assert ports.find_board(environ=env, usb_port=PORT_A) == str(devfs / "ttyACM0")


def test_device_path_and_flag_together_are_refused_by_web(capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0"})
    assert web.main(["/dev/x", "--usb-port", PORT_A]) == 1
    assert "not both" in capsys.readouterr().err
    del env


def test_term_session_refuses_before_going_raw(monkeypatch, capsys, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    monkeypatch.delenv("DISCOBSD_PORT", raising=False)
    monkeypatch.delenv("DISCOBSD_USB_PORT", raising=False)
    called = []
    monkeypatch.setattr(term, "run", lambda **kw: called.append(kw) or 0)
    assert term.main([]) == 2
    assert called == []
    assert PORT_A in capsys.readouterr().err
    assert term.main(["--usb-port", PORT_A]) == 0
    assert called == [{"usb_port": PORT_A}]


def test_term_waits_while_the_named_port_has_no_console(monkeypatch, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    tty = tmp_path / "sys" / PORT_B / f"{PORT_B}:1.0" / "tty" / "ttyACM1"
    tty.rmdir()
    assert term.current_board(PORT_B) is None
    assert term.current_board("1-9") is None
    with pytest.raises(ports.BoardSelectionError) as info:
        term.current_board("not-a-port")
    assert info.value.kind == "invalid"


def _wait_closed(line, tries=60):
    import threading

    while not line.closed and tries:
        threading.Event().wait(0.05)
        tries -= 1
    return line.closed


def test_web_resolves_the_selector_before_each_serial_open(monkeypatch, tmp_path):
    """Port 1-3 holds ttyACM0, then both boards re-enumerate and 1-3 moves to
    ttyACM1 while 1-4 takes ttyACM0. A session after the move must open 1-3's
    tty, not the name cached at startup."""
    import threading

    from test_web import FakeLine, read_frames, ws_connect

    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    # Start: 1-3 -> ttyACM0, 1-4 -> ttyACM1. Both nodes exist in devfs.
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT):
        monkeypatch.delenv(name, raising=False)
    opened = []
    lines = []

    def opener(device):
        opened.append(device)
        line = FakeLine()
        lines.append(line)
        return line

    httpd = web.ConsoleServer(
        ("127.0.0.1", 0), None, "tok", open_serial=opener, usb_port=PORT_A
    )
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        port = httpd.server_address[1]
        s, status = ws_connect(port)
        read_frames(s, b"login: ", got=status)
        s.sendall(masked(b"", opcode=0x8))
        s.close()
        assert _wait_closed(lines[0])
        devfs = tmp_path / "dev"
        assert opened == [str(devfs / "ttyACM0")]

        sysfs = tmp_path / "sys"
        tty_a = sysfs / PORT_A / f"{PORT_A}:1.0" / "tty"
        tty_b = sysfs / PORT_B / f"{PORT_B}:1.0" / "tty"
        (tty_a / "ttyACM0").rename(tty_a / "ttyACM1")
        (tty_b / "ttyACM1").rename(tty_b / "ttyACM0")

        s, status = ws_connect(port)
        read_frames(s, b"login: ", got=status)
        s.close()
        assert opened[-1] == str(devfs / "ttyACM1")
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_web_session_refuses_when_the_named_port_is_gone(monkeypatch, tmp_path):
    import threading

    from test_web import ws_connect

    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT):
        monkeypatch.delenv(name, raising=False)
    opened = []
    httpd = web.ConsoleServer(
        ("127.0.0.1", 0), None, "tok", open_serial=opened.append, usb_port=PORT_A
    )
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        (tmp_path / "sys" / PORT_A / "idVendor").write_text("0000\n")
        s, status = ws_connect(httpd.server_address[1])
        from test_web import read_frames

        got = read_frames(s, b"holds no 2e8a device", got=status)
        assert b"holds no 2e8a device" in got
        assert opened == []
        s.close()
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_web_main_hands_the_selector_not_a_path_to_the_server(monkeypatch, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT, ports.TOKEN_ENV):
        monkeypatch.delenv(name, raising=False)
    got = {}

    class FakeServer:
        is_loopback = True

        def __init__(self, address, device, token, open_serial=None, usb_port=None):
            got.update(device=device, usb_port=usb_port)

        def serve_forever(self):
            raise KeyboardInterrupt()

        def server_close(self):
            pass

    monkeypatch.setattr(web, "ConsoleServer", FakeServer)
    assert web.main(["--usb-port", PORT_B]) == 0
    assert got == {"device": None, "usb_port": PORT_B}
    assert web.main(["/dev/pinned"]) == 0
    assert got == {"device": "/dev/pinned", "usb_port": None}


def test_term_reattach_resolves_the_port_again(monkeypatch, tmp_path):
    env = make_tree(tmp_path, {PORT_A: "ttyACM0", PORT_B: "ttyACM1"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT):
        monkeypatch.delenv(name, raising=False)
    devfs = tmp_path / "dev"
    assert term.wait_for_board(False, PORT_A) == str(devfs / "ttyACM0")
    tty_a = tmp_path / "sys" / PORT_A / f"{PORT_A}:1.0" / "tty"
    tty_b = tmp_path / "sys" / PORT_B / f"{PORT_B}:1.0" / "tty"
    (tty_a / "ttyACM0").rename(tty_a / "ttyACM1")
    (tty_b / "ttyACM1").rename(tty_b / "ttyACM0")
    assert term.wait_for_board(False, PORT_A) == str(devfs / "ttyACM1")


def test_web_session_refuses_a_second_board_attached_after_startup(monkeypatch, tmp_path):
    """Started with one board and no selector, the server resolves the board
    for every session, so a second board attached later is refused there
    instead of being opened by whichever name discovery returns first."""
    import threading

    from test_web import read_frames, ws_connect

    env = make_tree(tmp_path, {PORT_A: "ttyACM0"})
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    for name in (ports.ENV_PORT, ports.ENV_USB_PORT):
        monkeypatch.delenv(name, raising=False)
    opened = []
    httpd = web.ConsoleServer(("127.0.0.1", 0), None, "tok", open_serial=opened.append)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        late = tmp_path / "sys" / PORT_B
        (late / f"{PORT_B}:1.0" / "tty" / "ttyACM1").mkdir(parents=True)
        (late / "idVendor").write_text("2e8a\n")
        (late / "idProduct").write_text("000a\n")
        (late / "product").write_text("GizmoBSD RP2040 console\n")
        s, status = ws_connect(httpd.server_address[1])
        got = read_frames(s, b"more than one 2e8a device", got=status)
        assert b"more than one 2e8a device" in got
        assert opened == []
        s.close()
    finally:
        httpd.shutdown()
        httpd.server_close()


@pytest.mark.parametrize("source", ["flag", "environment"])
def test_selector_without_sysfs_is_unsupported(source, tmp_path):
    """A host without the sysfs tree (macOS, Windows) refuses the selector.

    The refusal names the missing tree instead of falling back to discovery,
    so a named board is never replaced by whichever board enumerates first.
    """
    absent = str(tmp_path / "no-sysfs")
    environ = {"DISCOBSD_SYSFS_USB": absent, "DISCOBSD_DEVFS": str(tmp_path)}
    kwargs = {"usb_port": PORT_A} if source == "flag" else {}
    if source == "environment":
        environ["DISCOBSD_USB_PORT"] = PORT_A
    with pytest.raises(ports.BoardSelectionError) as caught:
        ports.find_board(comports=lambda: [], environ=environ, **kwargs)
    assert caught.value.kind == "unsupported"
    assert caught.value.exit_code == 1
