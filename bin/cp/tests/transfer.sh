#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -eu
here=$(CDPATH='' cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
cleanup()
{
    rm -f "$work/cp.o" "$work/cp" "$work/source" "$work/dest" "$work/err"
    rmdir "$work"
}
trap cleanup 0
trap 'exit 130' HUP INT TERM
cc=${CC:-cc}
"$cc" -std=gnu17 -DMAXBSIZE=1024 -Dread=cp_read -Dwrite=cp_write \
    -Dclose=cp_close -c "$here/../cp.c" -o "$work/cp.o"
"$cc" "$work/cp.o" "$here/transfer.c" -o "$work/cp"
cd "$work"
awk 'BEGIN { for (i = 0; i < 400; i++) print i }' >source
for mode in short interrupt; do
    CP_TEST=$mode ./cp source dest
    cmp source dest
done
for mode in zero full close; do
    if CP_TEST=$mode ./cp -p source dest 2>err; then
        echo "cp accepted $mode" >&2
        exit 1
    fi
    grep -q 'dest' err
done
