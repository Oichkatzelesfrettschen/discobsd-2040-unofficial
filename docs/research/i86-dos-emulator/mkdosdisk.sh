#!/bin/sh
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
# mkdosdisk.sh: make a bootable SvarDOS hard disk image for i86(1).
#
#	mkdosdisk.sh I86 SVDOS-DISK-1.IMG OUTPUT.IMG MEGABYTES [FILE ...]
#
# I86 is a host build of the emulator (bmake -C usr.bin/i86 host), which
# this script uses as the installer: it boots the SvarDOS floppy with the
# new disk attached and runs SYS C: there, so the kernel and boot sector
# are placed by SvarDOS itself. SVDOS-DISK-1.IMG is the first disk of the
# SvarDOS 360 KB floppy set (svardos.org). The image gets one FAT
# partition of 16 heads and 63 sectors a track, 2 to 500 megabytes, the
# partition sector of mbr.hexdump beside this script, CONFIG.SYS and
# AUTOEXEC.BAT for a machine with no memory above 640 KB, the small tools
# of the floppy, and each FILE named. The last step boots the image and
# requires the shell's banner.
#
# It needs mtools (mformat, mcopy, mdel, mdir, minfo) and PYTHON, and
# writes OUTPUT.IMG, and a file mktemp names beside it while it works.
set -eu

if [ "$#" -lt 4 ]; then
	echo "usage: mkdosdisk.sh I86 SVDOS-DISK-1.IMG OUTPUT.IMG MEGABYTES [FILE ...]" >&2
	exit 2
fi
: "${PYTHON:?set PYTHON to the intended interpreter}"
i86=$1
floppy=$2
out=$3
megabytes=$4
shift 4
here=$(cd "$(dirname "$0")" && pwd)
for tool in mformat mcopy mdel mdir minfo; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "mkdosdisk: $tool not found (package mtools)" >&2
		exit 2
	fi
done
case $megabytes in
*[!0-9]* | "")
	echo "mkdosdisk: MEGABYTES must be a number" >&2
	exit 2
	;;
esac
if [ "$megabytes" -lt 2 ] || [ "$megabytes" -gt 500 ]; then
	echo "mkdosdisk: MEGABYTES must be 2 to 500" >&2
	exit 2
fi

# The image is built beside OUTPUT.IMG under another name and takes that
# name only when it boots, so an OUTPUT.IMG that is, or links to, the
# floppy or one of the files is read whole before anything replaces it.
work=$(mktemp -d)
img=$(mktemp "$out.new.XXXXXX")
trap 'rm -f "$work/tool.img" "$work/autoexec.bat" "$work/config.sys" \
    "$work/page" "$work/file" "$work/screen" "$img"; rmdir "$work"' EXIT

# 1008 sectors a cylinder; the partition starts on the second track.
cylinders=$((megabytes * 2048 / 1008))
if [ "$cylinders" -gt 1024 ]; then
	cylinders=1024
fi
total=$((cylinders * 1008))
: >"$img"
"$PYTHON" -I - "$img" "$total" <<'EOF'
import sys
with open(sys.argv[1], "wb") as image:
    image.truncate(int(sys.argv[2]) * 512)
EOF
mformat -i "$img@@32256" -T $((total - 63)) -h 16 -s 63 -H 63 -v I86DISK ::
fat=$(minfo -i "$img@@32256" :: | sed -n 's/.*disk type="\(FAT[0-9]*\).*/\1/p')
"$PYTHON" -I - "$img" "$total" "$fat" "$here/mbr.hexdump" <<'EOF'
import struct
import sys

path, total, fat, code = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
with open(code) as handle:
    boot = bytes.fromhex("".join(handle.read().split()))
if len(boot) != 440:
    sys.exit("mkdosdisk: mbr.hexdump is not 440 bytes")


def chs(lba):
    cylinder, rest = divmod(lba, 1008)
    head, sector = divmod(rest, 63)
    cylinder = min(cylinder, 1023)
    return bytes([head, (sector + 1) | (cylinder >> 8) << 6, cylinder & 0xFF])


kind = 0x01 if fat == "FAT12" else 0x04 if total < 65536 else 0x06
entry = bytes([0x80]) + chs(63) + bytes([kind]) + chs(total - 1)
entry += struct.pack("<II", 63, total - 63)
with open(path, "r+b") as image:
    image.write(boot)
    image.seek(446)
    image.write(entry + bytes(48) + b"\x55\xaa")
EOF

# The floppy that installs: the SvarDOS disk without its installer.
cp "$floppy" "$work/tool.img"
for name in autoexec.bat config.sys install.com install.lng install.lst \
    pkg.exe display.exe fdapm.com boot.mbr ega.cpx ega3.cpx ega6.cpx \
    ega9.cpx ega10.cpx move.svp more.svp; do
	mdel -i "$work/tool.img" "::$name" 2>/dev/null || true
done
printf '@ECHO OFF\r\n' >"$work/autoexec.bat"
printf 'FILES=8\r\nBUFFERS=4\r\nSHELL=COMMAND.COM /e:256 /p\r\n' \
    >"$work/config.sys"
mcopy -i "$work/tool.img" "$work/autoexec.bat" ::AUTOEXEC.BAT
mcopy -i "$work/tool.img" "$work/config.sys" ::CONFIG.SYS
"$i86" -q -T -c 286 -s "$work/page" -a "$work/tool.img" -h "$img" \
    -k "$(printf 'sys c:\r')"
if ! mdir -i "$img@@32256" ::KERNEL.SYS >/dev/null 2>&1; then
	echo "mkdosdisk: SYS C: left no kernel on the disk" >&2
	exit 1
fi

# Every buffer and environment byte is guest memory the emulator pages, and
# no memory exists above 640 KB, so no memory manager is loaded.
printf '@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\\r\nVER\r\n' >"$work/autoexec.bat"
printf 'FILES=20\r\nBUFFERS=4\r\nLASTDRIVE=E\r\nSHELL=C:\\COMMAND.COM /E:512 /P\r\n' \
    >"$work/config.sys"
mcopy -o -i "$img@@32256" "$work/autoexec.bat" ::AUTOEXEC.BAT
mcopy -o -i "$img@@32256" "$work/config.sys" ::CONFIG.SYS
for name in mem.exe mode.com more.com sved.com format.exe fdisk.exe sys.com; do
	if mcopy -n -i "$work/tool.img" "::$name" "$work/file" 2>/dev/null; then
		mcopy -o -i "$img@@32256" "$work/file" "::$name"
		rm -f "$work/file"
	fi
done
for name in "$@"; do
	mcopy -o -i "$img@@32256" "$name" ::
done

"$i86" -d -T -c 286 -s "$work/page" -h "$img" -k "$(printf 'ver\r')" \
    >"$work/screen"
if ! grep -q 'SvarCOM' "$work/screen"; then
	echo "mkdosdisk: the image does not boot to the SvarDOS shell" >&2
	exit 1
fi
mv -f "$img" "$out"
echo "mkdosdisk: $out boots: $megabytes MB, $fat, $cylinders cylinders"
