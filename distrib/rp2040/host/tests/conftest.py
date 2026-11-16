"""Keep the Python tools off the real USB tree during tests.

ports.find_board() refuses to choose when the host's /sys/bus/usb/devices
holds several 2e8a devices, so a test that reaches discovery on a machine
with two boards attached would fail for a reason that is not under test.
Pointing the default at an empty directory makes discovery hermetic; a test
that needs a populated tree passes its own through the environment mapping.
"""

import pytest

from discobsd_host import ports


@pytest.fixture(autouse=True)
def _empty_usb_sysfs(tmp_path, monkeypatch):
    empty = tmp_path / "empty-usb-sysfs"
    empty.mkdir()
    monkeypatch.setattr(ports, "DEFAULT_SYSFS", str(empty))
