#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
make_command=${MAKE:-bmake}
nm_command=${NM:-arm-none-eabi-nm}
size_command=${SIZE:-arm-none-eabi-size}

for board in PICO PICO_UART; do
    build="sys/arch/rp2040/compile/$board"
    "$make_command" MACHINE=rp2040 STORAGE_STATS=no -C "$build" all
    "$nm_command" "$build/unix.elf" > "$build/.storage-symbols"
    if awk '$NF == "storage_note" { found=1 } END { exit !found }' \
        "$build/.storage-symbols"; then
        echo "FAIL $board disabled storage counters linked" >&2
        exit 1
    fi
    "$size_command" "$build/unix.elf"
    "$make_command" MACHINE=rp2040 STORAGE_STATS=yes -C "$build" all
    "$nm_command" "$build/unix.elf" > "$build/.storage-symbols"
    for symbol in storage_note storage_snapshot storage_dirty_sample; do
        if ! awk -v symbol="$symbol" '$NF == symbol { found=1 } END { exit !found }' \
            "$build/.storage-symbols"; then
            echo "FAIL $board enabled $symbol absent" >&2
            exit 1
        fi
    done
    "$size_command" "$build/unix.elf"
    "$make_command" MACHINE=rp2040 STORAGE_STATS=no -C "$build" all
    rm -f "$build/.storage-symbols"
done

echo "storage counters: enabled and disabled kernels linked on both boards"
