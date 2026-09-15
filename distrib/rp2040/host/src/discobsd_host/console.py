"""discobsd-console: run the web console and its short link on any host.

`discobsd-console up` starts discobsd-web on port 7681 behind a generated
token and discobsd-link on port 42069 redirecting to the tokenized URL,
then prints the URLs to hand out. `down` stops them and `status` reports.
On a Linux host where the packaged systemd user units are installed, the
units carry the processes; everywhere else the two servers run detached
from this shell with pid files under the per-user configuration directory:
$XDG_CONFIG_HOME/discobsd on Linux and BSD, ~/Library/Application Support/
discobsd on macOS, %APPDATA%\\discobsd on Windows.

The configuration file web.env holds DISCOBSD_WEB_TOKEN and
DISCOBSD_HOST_IP. It is created with owner-only permissions on the first
run and read by every later run and by the systemd units.
"""

from __future__ import annotations

import argparse
import os
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from . import __version__

WEB_PORT = 7681
LINK_PORT = 42069
UNITS = ("discobsd-web.service", "discobsd-link.service")


def config_dir(environ=None, platform=None, home=None) -> Path:
    environ = os.environ if environ is None else environ
    platform = sys.platform if platform is None else platform
    home = Path.home() if home is None else Path(home)
    if platform == "win32":
        base = environ.get("APPDATA")
        return Path(base) / "discobsd" if base else home / "AppData" / "Roaming" / "discobsd"
    if platform == "darwin":
        return home / "Library" / "Application Support" / "discobsd"
    base = environ.get("XDG_CONFIG_HOME")
    return Path(base) / "discobsd" if base else home / ".config" / "discobsd"


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        text = path.read_text()
    except OSError:
        return values
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip().strip("'\"")
    return values


def write_env(path: Path, values: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = "".join(f"{k}={v}\n" for k, v in values.items())
    tmp = path.with_suffix(".tmp")
    fd = os.open(str(tmp), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(body)
    os.replace(tmp, path)


def lan_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
        finally:
            s.close()
    except OSError:
        return "127.0.0.1"


def urls(ip: str, token: str, web_port: int, link_port: int) -> dict[str, str]:
    full = "http://%s:%d/?token=%s" % (ip, web_port, token)
    out = {"short": "http://%s:%d/" % (ip, link_port), "full": full}
    name = socket.gethostname().split(".")[0].lower()
    if name:
        out["mdns"] = "http://%s.local:%d/" % (name, link_port)
    return out


def systemd_units_installed(home=None) -> bool:
    if sys.platform != "linux" or shutil.which("systemctl") is None:
        return False
    home = Path.home() if home is None else Path(home)
    candidates = [
        Path("/usr/lib/systemd/user"),
        Path("/usr/local/lib/systemd/user"),
        home / ".config" / "systemd" / "user",
    ]
    return any((d / UNITS[0]).exists() for d in candidates)


def _pid_alive(pid: int) -> bool:
    if sys.platform == "win32":
        import ctypes

        handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED
        if not handle:
            return False
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _spawn_detached(argv: list[str], log: Path) -> int:
    log.parent.mkdir(parents=True, exist_ok=True)
    out = open(log, "ab")
    kwargs: dict = {"stdin": subprocess.DEVNULL, "stdout": out, "stderr": subprocess.STDOUT}
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x00000008 | 0x00000200  # DETACHED_PROCESS | NEW_PROCESS_GROUP
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen(argv, **kwargs)
    out.close()
    return proc.pid


def _tool(name: str) -> list[str]:
    """The command for a sibling entry point: the installed script when it is
    on PATH, else this interpreter running the module, which keeps a
    checkout, a venv, and a frozen bundle all working."""
    if getattr(sys, "frozen", False):
        sibling = Path(sys.executable).with_name(name + (".exe" if sys.platform == "win32" else ""))
        if sibling.exists():
            return [str(sibling)]
    found = shutil.which(name)
    if found:
        return [found]
    return [sys.executable, "-m", "discobsd_host." + name.split("-", 1)[1]]


def _stop_pidfile(path: Path) -> bool:
    try:
        pid = int(path.read_text().strip())
    except (OSError, ValueError):
        return False
    stopped = False
    if _pid_alive(pid):
        try:
            if sys.platform == "win32":
                subprocess.call(["taskkill", "/PID", str(pid), "/F"], stdout=subprocess.DEVNULL)
            else:
                os.kill(pid, signal.SIGTERM)
            stopped = True
        except OSError:
            pass
    try:
        path.unlink()
    except OSError:
        pass
    return stopped


def _http_answers(url: str, timeout: float = 3.0) -> bool:
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None

    opener = urllib.request.build_opener(NoRedirect)
    try:
        opener.open(url, timeout=timeout)
        return True
    except urllib.error.HTTPError as exc:
        return exc.code in (301, 302, 401)
    except Exception:
        return False


def cmd_up(args) -> int:
    conf = config_dir()
    env_path = conf / "web.env"
    values = read_env(env_path)
    token = os.environ.get("DISCOBSD_WEB_TOKEN") or values.get("DISCOBSD_WEB_TOKEN")
    if not token:
        token = secrets.token_hex(12)
    ip = args.host_ip or os.environ.get("DISCOBSD_HOST_IP") or values.get("DISCOBSD_HOST_IP")
    if not ip:
        ip = lan_ip()
    write_env(env_path, {"DISCOBSD_WEB_TOKEN": token, "DISCOBSD_HOST_IP": ip})
    links = urls(ip, token, args.port, args.link_port)
    if systemd_units_installed() and not args.detached:
        subprocess.call(["systemctl", "--user", "daemon-reload"])
        rc = subprocess.call(["systemctl", "--user", "restart", *UNITS])
        if rc != 0:
            sys.stderr.write("discobsd-console: systemctl --user restart failed\n")
            return 1
        mode = "systemd user units"
    else:
        for name in ("link", "web"):
            _stop_pidfile(conf / (name + ".pid"))
        web = _tool("discobsd-web") + [
            "--bind",
            args.bind,
            "--port",
            str(args.port),
            "--token",
            token,
        ]
        link = _tool("discobsd-link") + [
            "--to",
            links["full"],
            "--port",
            str(args.link_port),
            "--bind",
            args.bind,
        ]
        (conf / "web.pid").write_text(str(_spawn_detached(web, conf / "web.log")))
        (conf / "link.pid").write_text(str(_spawn_detached(link, conf / "link.log")))
        mode = f"detached, pid files in {conf}"
    time.sleep(1.0)
    if not _http_answers(links["short"]):
        sys.stderr.write(
            "discobsd-console: {} is not answering; see the logs in {}\n".format(
                links["short"], conf
            )
        )
        return 1
    print(f"DiscoBSD console is up ({mode}):")
    print("  short:  {}".format(links["short"]))
    if "mdns" in links:
        print("          {}".format(links["mdns"]))
    print("  full:   {}".format(links["full"]))
    print("Log in as operator; su for root. One browser session at a time.")
    return 0


def cmd_down(args) -> int:
    conf = config_dir()
    if systemd_units_installed() and not args.detached:
        subprocess.call(["systemctl", "--user", "stop", *reversed(UNITS)])
        print("DiscoBSD console stopped (systemd user units).")
        return 0
    stopped = sum(_stop_pidfile(conf / (n + ".pid")) for n in ("link", "web"))
    print("DiscoBSD console stopped (%d process(es))." % stopped)
    return 0


def cmd_status(args) -> int:
    conf = config_dir()
    values = read_env(conf / "web.env")
    ip = values.get("DISCOBSD_HOST_IP") or lan_ip()
    short = "http://%s:%d/" % (ip, args.link_port)
    if systemd_units_installed() and not args.detached:
        rc = subprocess.call(["systemctl", "--user", "is-active", "--quiet", UNITS[0]])
        running = rc == 0
    else:
        running = False
        pid_file = conf / "web.pid"
        try:
            running = _pid_alive(int(pid_file.read_text().strip()))
        except (OSError, ValueError):
            pass
    answering = _http_answers(short)
    print("web console: %s" % ("running" if running else "stopped"))
    print("short link:  {} ({})".format(short, "answering" if answering else "silent"))
    print("config:      %s" % (conf / "web.env"))
    return 0 if running and answering else 1


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="discobsd-console",
        description="Run the DiscoBSD web console and its short link.",
    )
    p.add_argument("--version", action="version", version="discobsd-console " + __version__)
    sub = p.add_subparsers(dest="command", required=True)
    up = sub.add_parser("up", help="start or restart the servers and print the URLs")
    up.add_argument("--bind", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    up.add_argument("--port", type=int, default=WEB_PORT, help="web console port")
    up.add_argument("--link-port", type=int, default=LINK_PORT, help="short link port")
    up.add_argument("--host-ip", help="address to advertise (default: autodetect)")
    up.set_defaults(func=cmd_up)
    down = sub.add_parser("down", help="stop the servers")
    down.set_defaults(func=cmd_down)
    status = sub.add_parser("status", help="report whether the console answers")
    status.add_argument("--link-port", type=int, default=LINK_PORT)
    status.set_defaults(func=cmd_status)
    for command in (up, down, status):
        command.add_argument(
            "--detached",
            action="store_true",
            help="run or address detached processes even when the systemd units are installed",
        )
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
