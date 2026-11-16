import threading
import types
import urllib.error
import urllib.request

import pytest

from discobsd_host import console, link, ports, term, web


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
        # Windows: the PnP manager upper-cases the instance ID the serial
        # number is read from, and usbser.sys exposes no product string.
        info("COM3", ports.USB_VID, ports.USB_PID, "RP2040", None),
        # A kernel that read its flash chip reports the unique ID before
        # the suffix, upper-cased on Windows.
        info("/dev/ttyACM5", ports.USB_VID, ports.USB_PID, "e66488c15f098435-rp2040", None),
        info("COM9", ports.USB_VID, ports.USB_PID, "E66488C15F098435-RP2040", None),
        # A Pico SDK program shares the vendor and product and reports the
        # bare ID.
        info("COM11", ports.USB_VID, ports.USB_PID, "E66488C15F098435", None),
    ]
    assert ports.list_boards(lambda: listing) == [
        "/dev/ttyACM3",
        "/dev/ttyACM5",
        "COM3",
        "COM7",
        "COM9",
    ]
    assert ports.find_board(lambda: listing, environ={}) == "/dev/ttyACM3"


def test_environment_override_wins():
    assert ports.find_board(lambda: [], environ={"DISCOBSD_PORT": "/dev/cu.x"}) == "/dev/cu.x"


def test_no_board_is_none(monkeypatch):
    monkeypatch.setattr(ports.glob, "glob", lambda pattern: [])
    assert ports.find_board(lambda: [], environ={}) is None


def test_credential_prefers_the_systemd_credential_file(tmp_path):
    (tmp_path / ports.TOKEN_CREDENTIAL).write_text("from-credential\n")
    environ = {"CREDENTIALS_DIRECTORY": str(tmp_path), ports.TOKEN_ENV: "from-env"}
    assert ports.credential(ports.TOKEN_CREDENTIAL, ports.TOKEN_ENV, environ) == "from-credential"


def test_credential_falls_back_to_the_environment_variable(tmp_path):
    # CREDENTIALS_DIRECTORY is set (as it is under any systemd credential),
    # but this particular credential was never loaded into it.
    environ = {"CREDENTIALS_DIRECTORY": str(tmp_path), ports.TOKEN_ENV: "from-env"}
    assert ports.credential(ports.TOKEN_CREDENTIAL, ports.TOKEN_ENV, environ) == "from-env"


def test_credential_is_none_when_neither_source_has_it():
    assert ports.credential(ports.TOKEN_CREDENTIAL, ports.TOKEN_ENV, {}) is None


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

    monkeypatch.setattr(ports, "find_board", lambda **_: "FAKE")
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


def test_link_requires_target(capsys):
    # Neither --to (deprecated, token embedded) nor --host (token resolved
    # from the environment or a systemd credential) was given.
    assert link.main([]) == 1
    assert "--to or --host is required" in capsys.readouterr().err


def test_link_host_needs_a_token(capsys, monkeypatch):
    monkeypatch.delenv(ports.TOKEN_ENV, raising=False)
    monkeypatch.delenv("CREDENTIALS_DIRECTORY", raising=False)
    assert link.main(["--host", "http://10.0.0.5:7681/"]) == 1
    assert "no token" in capsys.readouterr().err


def test_link_host_builds_the_tokenized_target(monkeypatch):
    monkeypatch.setenv(ports.TOKEN_ENV, "abc")
    captured = {}

    class FakeServer:
        def __init__(self, address, target):
            captured["target"] = target

        def serve_forever(self):
            raise KeyboardInterrupt()

        def server_close(self):
            pass

    monkeypatch.setattr(link, "RedirectServer", FakeServer)
    assert link.main(["--host", "http://10.0.0.5:7681/"]) == 0
    assert captured["target"] == "http://10.0.0.5:7681/?token=abc"


def test_link_to_with_embedded_token_is_deprecated_but_still_works(capsys, monkeypatch):
    class FakeServer:
        def __init__(self, address, target):
            pass

        def serve_forever(self):
            raise KeyboardInterrupt()

        def server_close(self):
            pass

    monkeypatch.setattr(link, "RedirectServer", FakeServer)
    assert link.main(["--to", "http://10.0.0.5:7681/?token=abc"]) == 0
    assert "deprecated" in capsys.readouterr().err


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


@pytest.mark.parametrize("systemd", [False, True])
def test_console_default_loopback_applies_to_every_launch_path(tmp_path, monkeypatch, systemd):
    monkeypatch.setattr(console, "config_dir", lambda: tmp_path)
    monkeypatch.setattr(console, "systemd_units_installed", lambda: systemd)
    monkeypatch.setattr(console, "_http_answers", lambda *a: True)
    monkeypatch.setattr(console, "_stop_pidfile", lambda *a: False)
    monkeypatch.setattr(console, "_tool", lambda name: [name])
    monkeypatch.setattr(console.subprocess, "call", lambda *a: 0)
    spawned = []

    def spawn(command, *args):
        spawned.append(command)
        return 123

    monkeypatch.setattr(console, "_spawn_detached", spawn)
    assert console.main(["up"]) == 0
    values = console.read_env(tmp_path / "web.env")
    assert values["DISCOBSD_WEB_BIND"] == "127.0.0.1"
    assert values["DISCOBSD_HOST_IP"] == "127.0.0.1"
    for command in spawned:
        assert command[command.index("--bind") + 1] == "127.0.0.1"


def test_escape_menu_quits_sends_and_helps():
    e = term.Escape()
    assert e.feed(b"ls\r") == (b"ls\r", None)
    assert e.feed(b"\x1d") == (b"", None)  # armed, nothing sent
    assert e.feed(b"_") == (b"", "help")
    assert e.feed(b"\x1dd") == (b"", "help")
    assert e.feed(b"\x1d\\") == (b"", "help")
    assert e.feed(b"\x1d]") == (b"\x1d", None)  # literal Ctrl-]
    assert e.feed(b"\x1d?") == (b"", "help")
    assert e.feed(b"ab\x1dq") == (b"ab", "quit")
    assert term.Escape().feed(b"\x1d\x1d") == (b"", "quit")


def test_escape_arms_across_reads():
    e = term.Escape()
    assert e.feed(b"\x1d") == (b"", None)
    assert e.feed(b"q") == (b"", "quit")


def _up(monkeypatch, tmp_path, usb_port):
    import types

    # config_dir() reads XDG_CONFIG_HOME on Linux alone; macOS and Windows
    # resolve the user's real Library or AppData directory, so the helper
    # pins it to tmp_path on every platform.
    monkeypatch.setattr(console, "config_dir", lambda *a, **k: tmp_path / "discobsd")
    monkeypatch.delenv("DISCOBSD_WEB_TOKEN", raising=False)
    monkeypatch.delenv("DISCOBSD_HOST_IP", raising=False)
    if usb_port is None:
        monkeypatch.delenv("DISCOBSD_USB_PORT", raising=False)
    else:
        monkeypatch.setenv("DISCOBSD_USB_PORT", usb_port)
    monkeypatch.setattr(console, "systemd_units_installed", lambda home=None: True)
    monkeypatch.setattr(console.subprocess, "call", lambda argv: 0)
    monkeypatch.setattr(console.time, "sleep", lambda s: None)
    monkeypatch.setattr(console, "_http_answers", lambda url, timeout=3.0: True)
    args = types.SimpleNamespace(
        host_ip="10.0.0.5", port=7681, link_port=42069, bind="0.0.0.0", detached=False
    )
    return console.cmd_up(args), tmp_path / "discobsd" / "web.env"


def test_up_writes_the_usb_port_into_the_unit_environment_file(monkeypatch, tmp_path):
    rc, env_path = _up(monkeypatch, tmp_path, "1-3")
    assert rc == 0
    assert console.read_env(env_path)["DISCOBSD_USB_PORT"] == "1-3"
    # The selector is dropped from the file when the caller no longer sets it.
    rc, env_path = _up(monkeypatch, tmp_path, None)
    assert rc == 0
    assert "DISCOBSD_USB_PORT" not in console.read_env(env_path)
    rc, env_path = _up(monkeypatch, tmp_path, "")
    assert "DISCOBSD_USB_PORT" not in console.read_env(env_path)


@pytest.mark.parametrize("value", ["1-3\nExecStart=x", "usb1", "1-", "../x"])
def test_up_refuses_a_malformed_usb_port_before_writing(monkeypatch, tmp_path, value, capsys):
    rc, env_path = _up(monkeypatch, tmp_path, value)
    assert rc == 1
    assert not env_path.exists()
    assert "DISCOBSD_USB_PORT" in capsys.readouterr().err


def _up_bind(monkeypatch, tmp_path, bind, host_ip=None, keep_env_ip=False, systemd=True):
    import types

    monkeypatch.setattr(console, "config_dir", lambda *a, **k: tmp_path / "discobsd")
    monkeypatch.delenv("DISCOBSD_WEB_TOKEN", raising=False)
    if not keep_env_ip:
        monkeypatch.delenv("DISCOBSD_HOST_IP", raising=False)
    monkeypatch.delenv("DISCOBSD_USB_PORT", raising=False)
    monkeypatch.setattr(console, "systemd_units_installed", lambda home=None: systemd)
    monkeypatch.setattr(console.subprocess, "call", lambda argv: 0)
    monkeypatch.setattr(console.time, "sleep", lambda s: None)
    monkeypatch.setattr(console, "_http_answers", lambda url, timeout=3.0: True)
    monkeypatch.setattr(console, "lan_ip", lambda: "10.0.0.9")
    args = types.SimpleNamespace(
        host_ip=host_ip, port=7681, link_port=42069, bind=bind, detached=False
    )
    assert console.cmd_up(args) == 0
    return console.read_env(tmp_path / "discobsd" / "web.env")


def test_up_lan_after_loopback_advertises_the_lan_address(monkeypatch, tmp_path):
    # A loopback run stores 127.0.0.1; a later LAN bind must not hand that
    # address to LAN clients, which would reach their own host.
    assert _up_bind(monkeypatch, tmp_path, "127.0.0.1")["DISCOBSD_HOST_IP"] == "127.0.0.1"
    assert _up_bind(monkeypatch, tmp_path, "0.0.0.0")["DISCOBSD_HOST_IP"] == "10.0.0.9"
    # A stored LAN address is kept for the next LAN run, and --host-ip wins.
    monkeypatch.setattr(console, "lan_ip", lambda: "10.0.0.77")
    assert _up_bind(monkeypatch, tmp_path, "0.0.0.0")["DISCOBSD_HOST_IP"] == "10.0.0.9"
    assert (
        _up_bind(monkeypatch, tmp_path, "0.0.0.0", host_ip="10.0.0.5")["DISCOBSD_HOST_IP"]
        == "10.0.0.5"
    )


def test_up_loopback_bind_keeps_an_explicit_host_ip(monkeypatch, tmp_path):
    # A loopback bind behind a tunnel or proxy still advertises the address
    # the operator named, from --host-ip or from DISCOBSD_HOST_IP.
    values = _up_bind(monkeypatch, tmp_path, "127.0.0.1", host_ip="203.0.113.4")
    assert values["DISCOBSD_HOST_IP"] == "203.0.113.4"
    monkeypatch.setenv("DISCOBSD_HOST_IP", "203.0.113.5")
    values = _up_bind(monkeypatch, tmp_path, "127.0.0.1", keep_env_ip=True)
    assert values["DISCOBSD_HOST_IP"] == "203.0.113.5"


def _fail_on_resolve(monkeypatch):
    """Record every name lookup; a host name bind must never reach one."""
    lookups = []

    def lookup(host, *a, **kw):
        lookups.append(host)
        return [(2, 1, 6, "", ("127.0.0.1", 0))]

    monkeypatch.setattr(console.socket, "getaddrinfo", lookup)
    monkeypatch.setattr(console.socket, "gethostbyname", lambda host: lookups.append(host))
    return lookups


@pytest.mark.parametrize(
    ("bind", "written"),
    [
        ("127.0.0.1", "127.0.0.1"),
        ("10.0.0.7", "10.0.0.7"),
        ("0.0.0.0", "0.0.0.0"),
    ],
)
@pytest.mark.parametrize("systemd", [False, True])
def test_up_passes_the_parsed_ip_literal_to_every_consumer(
    monkeypatch, tmp_path, bind, written, systemd
):
    lookups = _fail_on_resolve(monkeypatch)
    spawned = []
    monkeypatch.setattr(console, "_stop_pidfile", lambda *a: False)
    monkeypatch.setattr(console, "_tool", lambda name: [name])
    monkeypatch.setattr(
        console, "_spawn_detached", lambda command, *a: spawned.append(command) or 1
    )
    values = _up_bind(monkeypatch, tmp_path, bind, systemd=systemd)
    assert values["DISCOBSD_WEB_BIND"] == written
    for command in spawned:
        assert command[command.index("--bind") + 1] == written
    assert len(spawned) == (0 if systemd else 2)
    assert lookups == []


@pytest.mark.parametrize(
    "bind",
    [
        "localhost",
        "board.example",
        "no-such-host.invalid",
        "bad\nDISCOBSD_WEB_TOKEN=x",
        "a b",
        "",
        "fe80::1%eth0",
        "127.1",
        "::1",
        "0:0:0:0:0:0:0:1",
    ],
)
def test_up_refuses_a_bind_that_is_not_an_ip_literal(monkeypatch, tmp_path, capsys, bind):
    import types

    # Every name resolves to loopback here: code that resolves a host name
    # would accept it and write it to web.env, so the verdict needs the
    # refusal, the empty lookup log and the untouched web.env together.
    lookups = _fail_on_resolve(monkeypatch)
    conf = tmp_path / "discobsd"
    monkeypatch.setattr(console, "config_dir", lambda *a, **k: conf)
    monkeypatch.delenv("DISCOBSD_USB_PORT", raising=False)
    args = types.SimpleNamespace(
        host_ip=None, port=7681, link_port=42069, bind=bind, detached=False
    )
    assert console.cmd_up(args) == 1
    err = capsys.readouterr().err
    assert "--bind" in err
    assert "127.0.0.1" in err and "0.0.0.0" in err
    assert not (conf / "web.env").exists()
    assert lookups == []


def test_url_host_brackets_an_ipv6_literal():
    assert console.url_host("10.0.0.5") == "10.0.0.5"
    assert console.url_host("::1") == "[::1]"
    assert console.urls("::1", "tok", 7681, 42069)["short"] == "http://[::1]:42069/"
    assert console.urls("::1", "tok", 7681, 42069)["full"] == "http://[::1]:7681/?token=tok"


def _bind_or_skip(make):
    try:
        return make()
    except OSError as exc:
        pytest.skip("no IPv6 loopback on this host: %s" % exc)


def test_servers_bind_an_ipv6_literal():
    httpd = _bind_or_skip(lambda: link.RedirectServer(("::1", 0), "http://x/"))
    try:
        assert httpd.server_address[0] == "::1"
    finally:
        httpd.server_close()
    httpd = _bind_or_skip(lambda: web.ConsoleServer(("::1", 0), None, "tok"))
    try:
        assert httpd.server_address[0] == "::1"
        assert httpd.is_loopback
    finally:
        httpd.server_close()


@pytest.mark.parametrize(
    "variable, value",
    [
        ("host_ip", "10.0.0.5\nDISCOBSD_WEB_BIND=0.0.0.0"),
        ("host_ip", "10.0.0.5 extra"),
        ("host_ip", 'a"b'),
        ("DISCOBSD_HOST_IP", "10.0.0.5\rx"),
        ("DISCOBSD_WEB_TOKEN", "abc\nDISCOBSD_WEB_BIND=0.0.0.0"),
    ],
)
def test_up_refuses_an_environment_file_value_outside_the_allow_list(
    monkeypatch, tmp_path, capsys, variable, value
):
    import types

    # web.env is a systemd EnvironmentFile: a newline adds an assignment, and
    # a space, quote or backslash changes how the line parses. Every value is
    # checked against the allow-list before the file is written.
    conf = tmp_path / "discobsd"
    monkeypatch.setattr(console, "config_dir", lambda *a, **k: conf)
    for name in ("DISCOBSD_WEB_TOKEN", "DISCOBSD_HOST_IP", "DISCOBSD_USB_PORT"):
        monkeypatch.delenv(name, raising=False)
    host_ip = None
    if variable == "host_ip":
        host_ip = value
    else:
        monkeypatch.setenv(variable, value)
    args = types.SimpleNamespace(
        host_ip=host_ip, port=7681, link_port=42069, bind="127.0.0.1", detached=False
    )
    assert console.cmd_up(args) == 1
    assert "web.env" in capsys.readouterr().err
    assert not (conf / "web.env").exists()


def test_write_env_accepts_every_value_the_console_writes(tmp_path):
    path = tmp_path / "web.env"
    values = {
        "DISCOBSD_WEB_TOKEN": "0123456789abcdef01234567",
        "DISCOBSD_HOST_IP": "fd00::5",
        "DISCOBSD_WEB_BIND": "0.0.0.0",
        "DISCOBSD_USB_PORT": "1-3.2",
    }
    console.write_env(path, values)
    assert console.read_env(path) == values
    console.write_env(path, {"DISCOBSD_HOST_IP": "pico-host.local"})
    assert console.read_env(path) == {"DISCOBSD_HOST_IP": "pico-host.local"}
    # A base64 token set by the user keeps working.
    console.write_env(path, {"DISCOBSD_WEB_TOKEN": "q+8/Zw=="})
    assert console.read_env(path) == {"DISCOBSD_WEB_TOKEN": "q+8/Zw=="}


def test_up_concrete_bind_advertises_itself(monkeypatch, tmp_path):
    # The servers listen only on the bound interface, so a stored or
    # autodetected address elsewhere would advertise a dead endpoint.
    assert _up_bind(monkeypatch, tmp_path, "0.0.0.0")["DISCOBSD_HOST_IP"] == "10.0.0.9"
    assert _up_bind(monkeypatch, tmp_path, "10.0.0.7")["DISCOBSD_HOST_IP"] == "10.0.0.7"


def test_urls_offer_mdns_only_when_asked():
    assert "mdns" not in console.urls("127.0.0.1", "tok", 7681, 42069, mdns=False)


@pytest.mark.parametrize("bind, mdns", [("127.0.0.1", False), ("0.0.0.0", True)])
def test_up_prints_the_mdns_link_only_for_a_wildcard_bind(
    monkeypatch, tmp_path, capsys, bind, mdns
):
    monkeypatch.setattr(console.socket, "gethostname", lambda: "pico-host")
    _up_bind(monkeypatch, tmp_path, bind)
    assert ("pico-host.local" in capsys.readouterr().out) == mdns


def test_up_refuses_an_ipv6_advertised_address(monkeypatch, tmp_path, capsys):
    import types

    conf = tmp_path / "discobsd"
    monkeypatch.setattr(console, "config_dir", lambda *a, **k: conf)
    for name in ("DISCOBSD_WEB_TOKEN", "DISCOBSD_HOST_IP", "DISCOBSD_USB_PORT"):
        monkeypatch.delenv(name, raising=False)
    args = types.SimpleNamespace(
        host_ip="fd00::5", port=7681, link_port=42069, bind="0.0.0.0", detached=False
    )
    assert console.cmd_up(args) == 1
    assert "IPv6" in capsys.readouterr().err
    assert not (conf / "web.env").exists()


@pytest.mark.parametrize(
    "bind, shown",
    [("10.0.0.7", "http://10.0.0.7:7681/"), ("fd00::7", "http://[fd00::7]:7681/"),
     ("0.0.0.0", "http://10.0.0.9:7681/")],
)
def test_web_names_the_address_it_serves(monkeypatch, capsys, bind, shown):
    # The startup line names the bound address for a concrete bind and the
    # LAN address only for a wildcard bind.
    class Fake:
        is_loopback = False

        def __init__(self, *a, **k):
            pass

        def serve_forever(self):
            raise KeyboardInterrupt

        def server_close(self):
            pass

    monkeypatch.setattr(web, "ConsoleServer", Fake)
    monkeypatch.setattr(web, "lan_ip", lambda: "10.0.0.9")
    monkeypatch.setenv("DISCOBSD_WEB_TOKEN", "tok")
    assert web.main(["--bind", bind, "--port", "7681", "/dev/null"]) == 0
    assert shown in capsys.readouterr().err
