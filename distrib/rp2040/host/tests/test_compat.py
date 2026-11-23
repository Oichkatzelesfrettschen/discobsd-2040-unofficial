# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
"""The former discobsd names: commands, environment variables, config directory."""

from __future__ import annotations

import sys

import pytest
from test_board_select import CONNECT, FLASH, HOST, log, make_world, run

from gizmobsd_host import compat, console, link, ports, release, sramdump, term, web


def test_env_value_prefers_the_new_name(capsys, monkeypatch):
    monkeypatch.setattr(ports, "_WARNED_OLD_ENV", set())
    env = {"GIZMOBSD_PORT": "/dev/new", "DISCOBSD_PORT": "/dev/old"}
    assert ports.env_value("GIZMOBSD_PORT", env) == "/dev/new"
    assert capsys.readouterr().err == ""


def test_env_value_reads_the_old_name_once_with_one_line(capsys, monkeypatch):
    monkeypatch.setattr(ports, "_WARNED_OLD_ENV", set())
    env = {"DISCOBSD_USB_PORT": "1-3"}
    assert ports.env_value("GIZMOBSD_USB_PORT", env) == "1-3"
    assert ports.env_value("GIZMOBSD_USB_PORT", env) == "1-3"
    err = capsys.readouterr().err
    assert err.count("\n") == 1 and "DISCOBSD_USB_PORT" in err and "GIZMOBSD_USB_PORT" in err


def test_env_value_unset_is_none(capsys):
    assert ports.env_value("GIZMOBSD_PORT", {}) is None
    assert capsys.readouterr().err == ""


def test_find_board_honors_the_old_port_variable(monkeypatch):
    monkeypatch.setattr(ports, "_WARNED_OLD_ENV", set())
    assert ports.find_board(environ={"DISCOBSD_PORT": "/dev/ttyOLD"}) == "/dev/ttyOLD"


def test_read_env_maps_old_keys(tmp_path):
    path = tmp_path / "web.env"
    path.write_text("DISCOBSD_WEB_TOKEN=abc\nGIZMOBSD_HOST_IP=10.0.0.5\nDISCOBSD_HOST_IP=1.1.1.1\n")
    values = console.read_env(path)
    assert values["GIZMOBSD_WEB_TOKEN"] == "abc"
    assert values["GIZMOBSD_HOST_IP"] == "10.0.0.5"


def test_config_dir_falls_back_to_the_former_directory(tmp_path):
    (tmp_path / ".config" / "discobsd").mkdir(parents=True)
    assert console.config_dir({}, "linux", tmp_path) == tmp_path / ".config" / "discobsd"
    (tmp_path / ".config" / "gizmobsd").mkdir()
    assert console.config_dir({}, "linux", tmp_path) == tmp_path / ".config" / "gizmobsd"


@pytest.mark.parametrize(
    "alias, module",
    [
        (compat.term, term),
        (compat.web, web),
        (compat.link, link),
        (compat.console, console),
        (compat.release, release),
        (compat.sramdump, sramdump),
    ],
)
def test_python_aliases_name_the_new_command_and_forward(alias, module, capsys, monkeypatch):
    seen = []
    monkeypatch.setattr(module, "main", lambda argv=None: seen.append(argv) or 7)
    assert alias(["--x"]) == 7
    assert seen == [["--x"]]
    tool = module.__name__.rsplit(".", 1)[1]
    assert capsys.readouterr().err == "discobsd-%s is renamed gizmobsd-%s; use the new name\n" % (
        tool,
        tool,
    )


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX shell scripts")
@pytest.mark.parametrize("tool, new", [("flash", FLASH), ("connect", CONNECT)])
def test_shell_wrappers_forward_with_one_line(tool, new, tmp_path):
    sysfs, devfs, bindir, uf2 = make_world(tmp_path, {"1-3": ("000a", 7)})
    args = ["--usb-port", "../bad", str(uf2)] if tool == "flash" else ["--usb-port", "../bad"]
    old = HOST / ("discobsd-" + tool)
    direct = run(new, args, tmp_path, sysfs, devfs, bindir)
    via = run(old, args, tmp_path, sysfs, devfs, bindir)
    assert via.returncode == direct.returncode == 1
    assert via.stderr.splitlines()[0] == (
        "discobsd-%s is renamed gizmobsd-%s; use the new name" % (tool, tool)
    )
    assert via.stderr.splitlines()[1:] == direct.stderr.splitlines()


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX shell scripts")
def test_flash_reads_the_old_port_variable_with_one_line(tmp_path):
    sysfs, devfs, bindir, uf2 = make_world(tmp_path, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    new = run(FLASH, [str(uf2)], tmp_path, sysfs, devfs, bindir, GIZMOBSD_USB_PORT="1-3")
    new_log = log(tmp_path)
    assert new.returncode == 0 and new_log
    (tmp_path / "second").mkdir()
    sysfs, devfs, bindir, uf2 = make_world(
        tmp_path / "second", {"1-3": ("000a", 7), "1-4": ("000a", 3)}
    )
    old = run(FLASH, [str(uf2)], tmp_path / "second", sysfs, devfs, bindir, DISCOBSD_USB_PORT="1-3")
    assert old.returncode == new.returncode
    second = [
        line.replace(str(tmp_path / "second"), str(tmp_path)) for line in log(tmp_path / "second")
    ]
    assert second == new_log
    assert "DISCOBSD_USB_PORT is renamed GIZMOBSD_USB_PORT" in old.stderr
    assert "renamed" not in new.stderr
