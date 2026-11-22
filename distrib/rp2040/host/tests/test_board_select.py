"""Board selection in discobsd-flash and discobsd-connect with two boards attached.

A bare Pico and a PicoCalc both enumerate as 2e8a:000a with the serial
string "rp2040", so only the USB port tells them apart. The scripts run
against a fake sysfs tree and a PATH that holds stub picotool, terminal,
uname, findmnt and sleep programs and symlinks to the few coreutils the
scripts use; no real picotool or serial node is reachable. The stub picotool
changes the fake sysfs the way a real reset does (the product id flips and
the bus address advances), so a call that carries a stale address misses the
device. Each rule has a mutant: the script is copied with the rule removed
and the scenario check must then fail.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HOST = Path(__file__).resolve().parents[1]
FLASH = HOST / "discobsd-flash"
CONNECT = HOST / "discobsd-connect"

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX shell scripts")

COREUTILS = ("cat", "head", "mkdir", "sed", "tr", "wc")

STUB_PICOTOOL = """#!/bin/sh
# Fake picotool over the fake sysfs. Logs argv and the device it reached.
root=$DISCOBSD_SYSFS_USB
cmd=$1
bus= addr= up=
for a in "$@"; do
	case $prev in
	--bus) bus=$a ;;
	--address) addr=$a ;;
	esac
	case $a in -u) up=1 ;; esac
	prev=$a
done
dev=
for d in "$root"/*; do
	[ -e "$d/idVendor" ] || continue
	if [ -n "$bus" ]; then
		[ "$(cat "$d/busnum")" = "$bus" ] && [ "$(cat "$d/devnum")" = "$addr" ] || continue
	fi
	dev=$d
	break
done
echo "$* => ${dev##*/}" >> "$PT_LOG"
[ -n "$dev" ] || exit 1
bump() {
	echo $(($(cat "$dev/devnum") + 1)) > "$dev/devnum"
}
act() {
case $cmd in
info)
	key=info
	[ "$(cat "$dev/idProduct")" = 0003 ]
	;;
reboot)
	key=reboot
	if [ -n "$up" ]; then
		key=bootsel
		echo 0003 > "$dev/idProduct"
	else
		[ "$(cat "$dev/idProduct")" = 0003 ] || exit 1
		echo 000a > "$dev/idProduct"
	fi
	bump
	;;
load)
	[ "$(cat "$dev/idProduct")" = 0003 ] || exit 1
	key=load
	bump
	;;
esac
}
rc=0
act || rc=$?
# STUB_BUMP_OTHER names the call (load, bootsel or reboot) after which the
# device on the other port gets a new address.
if [ -n "${STUB_BUMP_OTHER:-}" ] && [ "$STUB_BUMP_OTHER" = "${key:-}" ]; then
	for o in "$root"/*; do
		[ "$o" = "$dev" ] || echo 99 > "$o/devnum"
	done
fi
# STUB_ADD_DEVICE names the call after which a second board appears on 1-5.
if [ -n "${STUB_ADD_DEVICE:-}" ] && [ "$STUB_ADD_DEVICE" = "${key:-}" ]; then
	mkdir -p "$root/1-5"
	echo 2e8a > "$root/1-5/idVendor"
	echo 0003 > "$root/1-5/idProduct"
	echo 1 > "$root/1-5/busnum"
	echo 11 > "$root/1-5/devnum"
	echo board 1-5 > "$root/1-5/product"
fi
exit $rc
"""


def _stub(path: Path, text: str) -> None:
    path.write_text(text)
    path.chmod(0o755)


def make_world(tmp: Path, devices: dict[str, tuple[str, int]]):
    """Fake sysfs with devices {port: (product, address)}, stubs and a log."""
    sysfs = tmp / "sys"
    devfs = tmp / "dev"
    bindir = tmp / "bin"
    for d in (sysfs, devfs, bindir):
        d.mkdir()
    for n, (product, addr) in devices.items():
        node = sysfs / n
        node.mkdir()
        (node / "idVendor").write_text("2e8a\n")
        (node / "idProduct").write_text(product + "\n")
        (node / "busnum").write_text("1\n")
        (node / "devnum").write_text(f"{addr}\n")
        (node / "product").write_text(f"board {n}\n")
        tty = node / f"{n}:1.0" / "tty" / f"ttyACM{n[-1]}"
        tty.mkdir(parents=True)
        (devfs / f"ttyACM{n[-1]}").write_text("")
    for name in COREUTILS:
        found = shutil.which(name)
        if found:
            (bindir / name).symlink_to(found)
    _stub(bindir / "picotool", STUB_PICOTOOL)
    _stub(bindir / "sleep", "#!/bin/sh\nexit 0\n")
    _stub(bindir / "uname", "#!/bin/sh\necho Linux\n")
    # FAKE_MOUNT makes the boot ROM volume look mounted for a label lookup.
    _stub(
        bindir / "findmnt",
        '#!/bin/sh\n[ -n "${FAKE_MOUNT:-}" ] || exit 1\n'
        'case "$*" in *TARGET*) echo /mnt/boot ;; *SOURCE*) echo /dev/sdz1 ;; esac\n',
    )
    for name in ("udisksctl", "umount", "diskutil"):
        _stub(bindir / name, f'#!/bin/sh\necho "{name} $*" >> "$UNMOUNT_LOG"\n')
    _stub(bindir / "tio", '#!/bin/sh\necho "tio $*" >> "$TERM_LOG"\n')
    # What the unaddressed path would pick: the first board's node.
    # TERM_ADD_DEVICE makes a second board appear while discovery runs.
    _stub(
        bindir / "discobsd-term",
        '#!/bin/sh\nif [ -n "${TERM_ADD_DEVICE:-}" ]; then\n'
        '\tmkdir -p "$DISCOBSD_SYSFS_USB/1-5"\n'
        '\techo 2e8a > "$DISCOBSD_SYSFS_USB/1-5/idVendor"\n'
        '\techo 000a > "$DISCOBSD_SYSFS_USB/1-5/idProduct"\nfi\n'
        f'echo "{devfs}/ttyACM3"\n',
    )
    (devfs / "ttyACM3").write_text("")
    uf2 = tmp / "image.uf2"
    uf2.write_text("uf2")
    return sysfs, devfs, bindir, uf2


def run(script: Path, args, tmp: Path, sysfs: Path, devfs: Path, bindir: Path, **extra):
    env = {
        "PATH": str(bindir),
        "DISCOBSD_SYSFS_USB": str(sysfs),
        "DISCOBSD_DEVFS": str(devfs),
        "PT_LOG": str(tmp / "picotool.log"),
        "TERM_LOG": str(tmp / "term.log"),
        "UNMOUNT_LOG": str(tmp / "unmount.log"),
    }
    env.update(extra)
    return subprocess.run(
        ["/bin/sh", str(script), *args],
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )


def log(tmp: Path, name: str = "picotool.log") -> list[str]:
    p = tmp / name
    return p.read_text().splitlines() if p.exists() else []


def mutate(script: Path, tmp: Path, old: str, new: str) -> Path:
    text = script.read_text()
    if old not in text:
        raise AssertionError(f"mutant anchor missing from {script.name}: {old!r}")
    out = tmp / f"mutant-{script.name}"
    out.write_text(text.replace(old, new, 1))
    return out


# --- discobsd-flash scenarios; each returns normally when the rule holds ---


def scenario_flash_single(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7)})
    r = run(script, [str(uf2)], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    assert log(tmp) == [
        "info => 1-3",
        "reboot -u -f => 1-3",
        "info => 1-3",
        f"load -v {uf2} => 1-3",
        "reboot => 1-3",
    ]


def scenario_flash_second_device_appears(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7)})
    r = run(script, [str(uf2)], tmp, sysfs, devfs, bindir, STUB_ADD_DEVICE="info")
    assert r.returncode == 2, r.stderr
    # Only the first call ran; the reset that would follow stays unissued.
    assert log(tmp) == ["info => 1-3"]
    assert "1-5" in r.stderr


def scenario_flash_eject_second_board(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("0003", 7)})
    r = run(script, ["--eject"], tmp, sysfs, devfs, bindir, STUB_ADD_DEVICE="info", FAKE_MOUNT="1")
    assert r.returncode == 2, r.stderr
    assert log(tmp) == ["info => 1-3"]
    assert log(tmp, "unmount.log") == []
    # One board: the same run unmounts its volume and reboots it.
    (tmp / "one").mkdir()
    sysfs, devfs, bindir, _ = make_world(tmp / "one", {"1-3": ("0003", 7)})
    r = run(script, ["--eject"], tmp / "one", sysfs, devfs, bindir, FAKE_MOUNT="1")
    assert r.returncode == 0, r.stderr
    assert log(tmp / "one", "unmount.log") == ["udisksctl unmount -b /dev/sdz1"]


def scenario_flash_empty_selector(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7)})
    # An empty option operand is an error, never "no selector".
    r = run(script, ["--usb-port", "", str(uf2)], tmp, sysfs, devfs, bindir)
    assert r.returncode == 1 and log(tmp) == []
    # An empty environment variable is unset: one device runs unchanged.
    r = run(script, [str(uf2)], tmp, sysfs, devfs, bindir, DISCOBSD_USB_PORT="")
    assert r.returncode == 0, r.stderr
    assert log(tmp)[0] == "info => 1-3"


def scenario_flash_refuses(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(script, [str(uf2)], tmp, sysfs, devfs, bindir)
    assert r.returncode == 2, r.stderr
    assert log(tmp) == []
    assert "1-3" in r.stderr and "1-4" in r.stderr
    # BOOTSEL devices count too, and every mode refuses.
    for args in (["--bootsel"], ["--eject"]):
        r = run(script, args, tmp, sysfs, devfs, bindir)
        assert r.returncode == 2 and log(tmp) == []


def scenario_flash_selected(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(script, ["--usb-port", "1-3", str(uf2)], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    # The address advances at every call; each call carries the current one.
    assert log(tmp) == [
        "reboot -u -f --bus 1 --address 7 => 1-3",
        f"load -v {uf2} --bus 1 --address 8 => 1-3",
        "reboot --bus 1 --address 9 => 1-3",
    ]
    assert (sysfs / "1-4" / "devnum").read_text() == "3\n"
    assert not any("--address 3" in line for line in log(tmp))


def scenario_flash_environment_selector(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(script, [str(uf2)], tmp, sysfs, devfs, bindir, DISCOBSD_USB_PORT="1-4")
    assert r.returncode == 0, r.stderr
    assert log(tmp)[0] == "reboot -u -f --bus 1 --address 3 => 1-4"
    assert all("=> 1-4" in line for line in log(tmp))


def scenario_flash_bystander_moves(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, uf2 = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    second = tmp / "second.uf2"
    second.write_text("uf2")
    args = ["--usb-port", "1-3", str(uf2), str(second)]
    r = run(script, args, tmp, sysfs, devfs, bindir, STUB_BUMP_OTHER="load")
    assert r.returncode != 0
    assert "another USB port changed" in r.stderr
    # The first load ran; neither the second load nor the reboot did.
    assert [line.split()[0] for line in log(tmp)] == ["reboot", "load"]


def scenario_flash_bystander_in_other_modes(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(
        script,
        ["--usb-port", "1-3", "--bootsel"],
        tmp,
        sysfs,
        devfs,
        bindir,
        STUB_BUMP_OTHER="bootsel",
    )
    assert r.returncode != 0 and "another USB port changed" in r.stderr
    assert [line.split()[0] for line in log(tmp)] == ["reboot"]
    # --eject: the guard speaks although the caller discards picotool's output.
    (tmp / "eject").mkdir()
    sysfs, devfs, bindir, _ = make_world(tmp / "eject", {"1-3": ("0003", 7), "1-4": ("000a", 3)})
    r = run(
        script,
        ["--usb-port", "1-3", "--eject"],
        tmp / "eject",
        sysfs,
        devfs,
        bindir,
        STUB_BUMP_OTHER="reboot",
    )
    assert r.returncode != 0 and "another USB port changed" in r.stderr


FLASH_SCENARIOS = {
    "single": scenario_flash_single,
    "refuses": scenario_flash_refuses,
    "appears": scenario_flash_second_device_appears,
    "eject": scenario_flash_eject_second_board,
    "empty": scenario_flash_empty_selector,
    "selected": scenario_flash_selected,
    "environment": scenario_flash_environment_selector,
    "bystander": scenario_flash_bystander_moves,
    "modes": scenario_flash_bystander_in_other_modes,
}


@pytest.mark.parametrize("name", sorted(FLASH_SCENARIOS))
def test_flash_scenario(name, tmp_path):
    FLASH_SCENARIOS[name](FLASH, tmp_path)


def test_flash_selector_rejects_a_path_escape(tmp_path):
    sysfs, devfs, bindir, uf2 = make_world(tmp_path, {"1-3": ("000a", 7)})
    for bad in ("../1-3", "1-3/..", "1-3 ", "-3", "1-", "usb1", "1-3-4", "1-3."):
        r = run(FLASH, ["--usb-port", bad, str(uf2)], tmp_path, sysfs, devfs, bindir)
        assert r.returncode == 1 and log(tmp_path) == [], bad


def test_flash_selector_on_empty_port_stops_without_picotool(tmp_path):
    sysfs, devfs, bindir, uf2 = make_world(tmp_path, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(FLASH, ["--usb-port", "1-5", str(uf2)], tmp_path, sysfs, devfs, bindir)
    assert r.returncode == 1 and log(tmp_path) == []


FLASH_MUTANTS = {
    # rule: the selector-less run refuses when several 2e8a devices are attached
    "refuses": ('\t[ -z "$usb_port" ] || return 0\n\tdevs=', "\treturn 0\n\tdevs="),
    # rule: the selector-less run checks again before every picotool call
    "appears": ("\t\trefuse_ambiguous\n\t\tpicotool", "\t\tpicotool"),
    # rule: a volume is not unmounted once the device set has changed
    "eject": ("\trecheck_devices\n\techo", "\techo"),
    # rule: an empty --usb-port operand is refused
    "empty": ('[ $# -ge 2 ] && [ -n "$2" ] ||', "[ $# -ge 2 ] ||"),
    # rule: every picotool call carries the resolved bus and address
    "selected": ('picotool "$@" --bus "$dev_bus" --address "$dev_addr"', 'picotool "$@"'),
    # rule: the address is read again at each call
    "selected-cached": ("\tresolve_port || {", '\t[ -n "${dev_addr:-}" ] || resolve_port || {'),
    # rule: another port's device must not change during the run
    "bystander": ('[ "$now" = "$others_before" ] ||', "true ||"),
    # rule: the check follows every picotool call, so it stops a later load
    "bystander-after": ("|| rc=$?\n\tcheck_others\n", "|| rc=$?\n"),
}


@pytest.mark.parametrize("name", sorted(FLASH_MUTANTS))
def test_flash_mutant_is_caught(name, tmp_path):
    old, new = FLASH_MUTANTS[name]
    mutant = mutate(FLASH, tmp_path, old, new)
    scenarios = {
        "bystander": ("bystander", "modes"),
        "bystander-after": ("modes",),
        "refuses": ("refuses", "appears"),
    }.get(name, (name.split("-")[0],))
    for key in scenarios:
        run_dir = tmp_path / f"run-{key}"
        run_dir.mkdir()
        with pytest.raises(AssertionError):
            FLASH_SCENARIOS[key](mutant, run_dir)


@pytest.fixture(autouse=True)
def _run_dirs(tmp_path):
    (tmp_path / "run").mkdir()


# --- discobsd-connect ---


def scenario_connect_single(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7)})
    r = run(script, [], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    assert log(tmp, "term.log") == [f"tio -b 115200 -d 8 -p none -s 1 -f none {devfs}/ttyACM3"]


def scenario_connect_refuses(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("0003", 3)})
    r = run(script, [], tmp, sysfs, devfs, bindir)
    assert r.returncode == 2, r.stderr
    assert log(tmp, "term.log") == []
    assert "1-3" in r.stderr and "1-4" in r.stderr
    # An explicit device path names the board itself.
    r = run(script, [str(devfs / "ttyACM4")], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr


def scenario_connect_selected(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(script, ["--usb-port", "1-4"], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    assert log(tmp, "term.log") == [f"tio -b 115200 -d 8 -p none -s 1 -f none {devfs}/ttyACM4"]
    r = run(script, [], tmp, sysfs, devfs, bindir, DISCOBSD_USB_PORT="1-3")
    assert r.returncode == 0, r.stderr
    assert log(tmp, "term.log")[-1].endswith(f"{devfs}/ttyACM3")


def scenario_connect_empty_selector(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    r = run(script, ["--usb-port", ""], tmp, sysfs, devfs, bindir, DISCOBSD_USB_PORT="1-4")
    assert r.returncode == 1 and log(tmp, "term.log") == []
    # An empty environment variable is unset: two boards still refuse.
    r = run(script, [], tmp, sysfs, devfs, bindir, DISCOBSD_USB_PORT="")
    assert r.returncode == 2 and log(tmp, "term.log") == []


def scenario_connect_second_board_during_discovery(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7)})
    r = run(script, [], tmp, sysfs, devfs, bindir, TERM_ADD_DEVICE="1")
    assert r.returncode == 2 and "1-5" in r.stderr
    assert log(tmp, "term.log") == []


def scenario_connect_by_path(script: Path, tmp: Path) -> None:
    sysfs, devfs, bindir, _ = make_world(tmp, {"1-3": ("000a", 7), "1-4": ("000a", 3)})
    by_path = devfs / "serial" / "by-path"
    by_path.mkdir(parents=True)
    (by_path / "pci-0000:00:14.0-usb-0:3:1.0").symlink_to("../../ttyACM3")
    (by_path / "pci-0000:00:14.0-usb-0:4:1.0").symlink_to("../../ttyACM4")
    r = run(script, ["--usb-port", "1-4"], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    want = f"tio -b 115200 -d 8 -p none -s 1 -f none {by_path}/pci-0000:00:14.0-usb-0:4:1.0"
    assert log(tmp, "term.log") == [want]
    # A by-path link that names another tty is not the port's console.
    (by_path / "pci-0000:00:14.0-usb-0:4:1.0").unlink()
    (by_path / "pci-0000:00:14.0-usb-0:4:1.0").symlink_to("../../ttyACM3")
    r = run(script, ["--usb-port", "1-4"], tmp, sysfs, devfs, bindir)
    assert r.returncode == 0, r.stderr
    assert log(tmp, "term.log")[-1].endswith(f" {devfs}/ttyACM4")


CONNECT_SCENARIOS = {
    "empty": scenario_connect_empty_selector,
    "appears": scenario_connect_second_board_during_discovery,
    "by-path": scenario_connect_by_path,
    "single": scenario_connect_single,
    "refuses": scenario_connect_refuses,
    "selected": scenario_connect_selected,
}


@pytest.mark.parametrize("name", sorted(CONNECT_SCENARIOS))
def test_connect_scenario(name, tmp_path):
    CONNECT_SCENARIOS[name](CONNECT, tmp_path)


def test_connect_bootsel_port_has_no_console(tmp_path):
    sysfs, devfs, bindir, _ = make_world(tmp_path, {"1-3": ("0003", 7), "1-4": ("000a", 3)})
    for t in (sysfs / "1-3" / "1-3:1.0" / "tty").iterdir():
        t.rmdir()
    r = run(CONNECT, ["--usb-port", "1-3"], tmp_path, sysfs, devfs, bindir)
    assert r.returncode == 1 and "no console tty" in r.stderr
    assert log(tmp_path, "term.log") == []


def test_connect_rejects_option_and_path_together(tmp_path):
    sysfs, devfs, bindir, _ = make_world(tmp_path, {"1-3": ("000a", 7)})
    r = run(CONNECT, ["--usb-port", "1-3", "/dev/null"], tmp_path, sysfs, devfs, bindir)
    assert r.returncode == 1


CONNECT_MUTANTS = {
    "refuses": ("\t\texit 2\n", "\t\t:\n"),
    "selected": (
        'for t in "$sysfs_usb/$usb_port/$usb_port":1.0/tty/*; do',
        'for t in "$sysfs_usb"/*/*:1.0/tty/*; do',
    ),
    "by-path": ('"$devfs"/serial/by-path/', '"$devfs"/serial/gone/'),
    "empty": ('[ $# -ge 2 ] && [ -n "$2" ] ||', "[ $# -ge 2 ] ||"),
    "appears": (
        'elif [ -z "$named" ]; then\n\trefuse_ambiguous',
        'elif [ -z "$named" ]; then\n\t:',
    ),
}


@pytest.mark.parametrize("name", sorted(CONNECT_MUTANTS))
def test_connect_mutant_is_caught(name, tmp_path):
    old, new = CONNECT_MUTANTS[name]
    mutant = mutate(CONNECT, tmp_path, old, new)
    with pytest.raises(AssertionError):
        CONNECT_SCENARIOS[name](mutant, tmp_path / "run")


def test_scripts_are_executable():
    assert os.access(FLASH, os.X_OK) and os.access(CONNECT, os.X_OK)
