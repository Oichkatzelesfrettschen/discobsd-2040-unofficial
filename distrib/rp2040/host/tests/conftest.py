"""Keep the Python tools off the real USB tree and the real services during tests.

ports.find_board() refuses to choose when the host's /sys/bus/usb/devices
holds several 2e8a devices, so a test that reaches discovery on a machine
with two boards attached would fail for a reason that is not under test.
Pointing the default at an empty directory makes discovery hermetic; a test
that needs a populated tree passes its own through the environment mapping.

gizmobsd-console up restarts the user's installed systemd units, or starts
detached servers, after it writes web.env. A test that reaches either path
unmocked would restart the console the developer is using, so both entry
points fail the test unless the test replaces them itself.
"""

import pytest

from gizmobsd_host import console, ports


@pytest.fixture(autouse=True)
def _empty_usb_sysfs(tmp_path, monkeypatch):
    empty = tmp_path / "empty-usb-sysfs"
    empty.mkdir()
    monkeypatch.setattr(ports, "DEFAULT_SYSFS", str(empty))


@pytest.fixture(autouse=True)
def _no_real_console_services(monkeypatch):
    def refuse(*args, **kwargs):
        raise AssertionError(
            "test reached the real service launch path; replace "
            "console.systemd_units_installed and console._spawn_detached"
        )

    monkeypatch.setattr(console, "systemd_units_installed", refuse)
    monkeypatch.setattr(console, "_spawn_detached", refuse)
