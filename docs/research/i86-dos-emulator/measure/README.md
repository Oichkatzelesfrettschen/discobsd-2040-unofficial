# Counting Thumb instructions per guest instruction

These two files produced the interpreter cost figures of
`sys/arch/rp2040/doc/I86.md`. They are measurement tools, not gates.

`count.c` is a QEMU TCG plugin (QEMU 11 plugin interface) that adds one to a
counter for every executed instruction, keyed by the ELF symbol that
contains it. `bench.c` links the emulator's `pc.c`, `cpu.c` and `mem.c`
with host stubs that draw nothing and never sleep, boots a floppy image
with the fixed clock, types an optional key script and stops after a given
number of `i86_step()` calls.

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
arm-none-eabi-gcc -std=gnu17 -O2 -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
    -nostartfiles -I"$src" -o "$work/bench" bench.c "$src/pc.c" "$src/cpu.c" \
    "$src/mem.c" "$work/support.c" -lc -lgcc
qemu-arm -cpu cortex-m0 -plugin "$work/libcount.so" "$work/bench" fd.img 12000000 2 "$keys"
```

The image and key script of the recorded figures: the FreeDOS 1.1 floppy of
the 8086tiny distribution (`fd.img`, 1.44 MB) and
`dir`, `ver`, `dir`, `type config.sys`, then `dir` eight times, each ended
by a carriage return, for 12,000,000 guest instructions on the 80286 model.
The count includes the firmware services, the display memory and the
polling of the keyboard service while the key script waits; it excludes
terminal output and host sleeps, which the stubs remove. The plugin's total
was checked once against a `-d exec,nochain -one-insn-per-tb` trace of the
same run: both gave 134,252,058 instructions for 600,000 guest instructions
of the first measured build.
