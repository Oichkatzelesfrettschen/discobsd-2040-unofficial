# -*- mode: python ; coding: utf-8 -*-
#
# One spec, four one-file executables. discobsd_host.console resolves a
# sibling tool through Path(sys.executable).with_name(name) when the
# process is frozen, so the four executables have to land side by side in
# one directory: four EXE() objects in onefile form (each carrying its own
# binaries, zipfiles and datas, and no COLLECT) put them all in dist/.
#
# pyserial picks its backend and its port enumerator at import time through
# a platform-dependent module name, which the analysis does not follow.
# Naming them as hidden imports keeps --list working in the bundle. The
# modules of the foreign platform are named too: serial.serialwin32 reaches
# for ctypes.windll and serial.tools.list_ports_windows for ctypes.wintypes,
# neither of which imports on POSIX, so those are requested only where they
# resolve and the analysis stays warning-clean.

import sys

block_cipher = None

hiddenimports = [
    "serial",
    "serial.tools",
    "serial.tools.list_ports",
    "serial.tools.list_ports_common",
]
if sys.platform == "win32":
    hiddenimports += ["serial.serialwin32", "serial.tools.list_ports_windows"]
else:
    hiddenimports += ["serial.serialposix", "serial.tools.list_ports_posix"]
    if sys.platform == "darwin":
        hiddenimports += ["serial.tools.list_ports_osx"]
    else:
        hiddenimports += ["serial.tools.list_ports_linux"]

TOOLS = ("term", "web", "link", "console")

analyses = {}
for _name in TOOLS:
    analyses[_name] = Analysis(
        ["entry_%s.py" % _name],
        pathex=[],
        binaries=[],
        datas=[],
        hiddenimports=hiddenimports,
        hookspath=[],
        hooksconfig={},
        runtime_hooks=[],
        excludes=["tkinter", "PIL", "numpy"],
        win_no_prefer_redirects=False,
        win_private_assemblies=False,
        cipher=block_cipher,
        noarchive=False,
    )

pyzs = {name: PYZ(a.pure, a.zipped_data, cipher=block_cipher) for name, a in analyses.items()}

exes = []
for _name in TOOLS:
    _a = analyses[_name]
    exes.append(
        EXE(
            pyzs[_name],
            _a.scripts,
            _a.binaries,
            _a.zipfiles,
            _a.datas,
            [],
            name="discobsd-" + _name,
            debug=False,
            bootloader_ignore_signals=False,
            strip=False,
            upx=False,
            upx_exclude=[],
            runtime_tmpdir=None,
            console=True,
            disable_windowed_traceback=False,
            argv_emulation=False,
            target_arch=None,
            codesign_identity=None,
            entitlements_file=None,
        )
    )
