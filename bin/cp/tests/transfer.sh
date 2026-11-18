#!/bin/sh
set -eu
here=$(CDPATH='' cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' 0
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
