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
# mksvardos.sh: make a SvarDOS boot floppy for i86(1) from the first disk of
# the SvarDOS 360 KB floppy set (svardos.org, svdos-360K-disk-1.img).
#
#	mksvardos.sh SVDOS-DISK-1.IMG OUTPUT.IMG
#
# The output keeps the boot sector, the kernel, the command interpreter and
# the small tools of the source disk, drops the installer and its packages,
# and replaces AUTOEXEC.BAT and CONFIG.SYS so the disk boots to a prompt.
# It needs mtools (mcopy, mdel) and writes OUTPUT.IMG only.
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: mksvardos.sh SVDOS-DISK-1.IMG OUTPUT.IMG" >&2
	exit 2
fi
src=$1
out=$2
for tool in mcopy mdel; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "mksvardos: $tool not found (package mtools)" >&2
		exit 2
	fi
done

work=$(mktemp -d)
trap 'rm -f "$work/autoexec.bat" "$work/config.sys"; rmdir "$work"' EXIT

cp "$src" "$out"
for name in autoexec.bat config.sys install.com install.lng install.lst \
    fdisk.exe format.exe sys.com pkg.exe display.exe fdapm.com boot.mbr \
    ega.cpx ega3.cpx ega6.cpx ega9.cpx ega10.cpx move.svp more.svp; do
	mdel -i "$out" "::$name" 2>/dev/null || true
done

printf '@ECHO OFF\r\nPROMPT $P$G\r\nVER\r\n' >"$work/autoexec.bat"
printf 'FILES=8\r\nBUFFERS=4\r\nSHELL=COMMAND.COM /e:256 /p\r\n' \
    >"$work/config.sys"
mcopy -i "$out" "$work/autoexec.bat" ::AUTOEXEC.BAT
mcopy -i "$out" "$work/config.sys" ::CONFIG.SYS
