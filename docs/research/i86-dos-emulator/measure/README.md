# Counting Thumb instructions per interpreter step

These two files produced the interpreter cost figures of
`sys/arch/rp2040/doc/I86.md`. They are measurement tools, not gates.

`count.c` is a QEMU TCG plugin (QEMU 11 plugin interface) that adds one to a
counter for every executed instruction, keyed by the ELF symbol that
contains it. `bench.c` links the emulator's `pc.c`, `cpu.c`, `mem.c` and `ems.c`
with host stubs that draw nothing and never sleep, boots a floppy image
with the fixed clock, types an optional key script and runs `main.c`'s loop (runs of
`pc_slice` steps, a poll after each) until a given number of
`i86_step()` calls, a halt with interrupts off, or as many polls.

```sh
top=$(git rev-parse --show-toplevel)
src=$top/usr.bin/i86
work=$(mktemp -d)
cc -shared -fPIC -O2 $(pkg-config --cflags glib-2.0) -o "$work/libcount.so" count.c
# The qemu-user support file reports no file size and does not seek;
# the bench needs both for the disk image.
sed -e 's/st->st_mode = S_IFCHR;/st->st_mode = S_IFCHR; st->st_size = 1474560;/' \
    -e 's/(void)off;/return fixup(sys3(19, fd, off, whence));/' \
    "$top/tests/libbn/qemu_support.c" >"$work/support.c"
# newlib lacks the directory reader of pc.c's disk menu and two calls of
# the DiscoBSD libc; newlib/ supplies them, and the panel and serial units
# link with the rest.
arm-none-eabi-gcc -std=gnu17 -O2 -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
    -nostartfiles -Inewlib -include newlib/bench.h -I"$src" -idirafter "$top/sys" \
    -o "$work/bench" bench.c "$src/pc.c" "$src/cpu.c" "$src/mem.c" "$src/gfx.c" \
    "$src/serial.c" "$src/ems.c" "$work/support.c" -lc -lgcc
qemu-arm -cpu cortex-m0 -plugin "$work/libcount.so" "$work/bench" fd.img 12000000 2 "$keys"
```

The image and key script of the recorded figures: the FreeDOS 1.1 floppy of
the 8086tiny distribution (`fd.img`, 1.44 MB) and
`dir`, `ver`, `dir`, `type config.sys`, then `dir` eight times, each ended
by a carriage return, for 12,000,000 steps on the 80286 model. A step is
one guest instruction, except that a repeated string instruction runs up
to 256 iterations in a step and a step may only deliver an interrupt.
Built for the host with `-DCOMPOSITION`, `bench.c` prints how many steps
of the same run were repeated string instructions and how many iterations
they carried (the run is deterministic, so the host and the ARM build
execute the same steps): 46,910 and 824,104 for the build of the fifth row of the table in
`sys/arch/rp2040/doc/I86.md`, counted with the earlier loop.
The count includes the firmware services, the display memory and the
polling of the keyboard service while the key script waits; it excludes
terminal output and host sleeps, which the stubs remove. The plugin's total
was checked once against a `-d exec,nochain -one-insn-per-tb` trace of the
same run: both gave 134,252,058 instructions for 600,000 steps
of the first measured build.

`bench.c` copied `main.c`'s loop when the 8254 timer arrived
(`../board-speed-pico.txt`). The FreeDOS figures above (46,910 and
824,104) and the first six rows of the table of
`sys/arch/rp2040/doc/I86.md` were counted with the earlier loop, which
polled every 2048 steps and stepped on through a halt. The table's last
two rows count the same recipe with `main.c`'s loop: 2,100,670,992 Thumb
instructions at 3366e0256 and 2,125,385,362 at dd65f4524, whose run carried
46,910 repeated instructions with 824,098 iterations. The same recipe with
the earlier loop counts 2,028,485,068 at 3366e0256.
