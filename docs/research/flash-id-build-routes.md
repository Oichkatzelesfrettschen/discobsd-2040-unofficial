# Three build routes to an RP2040 image, measured

`tools/flash-id` in the notes repository builds against the Pico SDK
through CMake, outside this tree's build. This measures what that route
costs against the alternatives, so a decision about where the probe lives
rests on numbers rather than on the impression that CMake is heavy.

`tools/bench-flash-id-build.sh` produces every figure below.

## The decisive finding

CMake is not a driver for the Pico SDK. It is a code generator the build
cannot proceed without.

Route 2 set out to compile the same sources with no CMake at all. Not one
SDK translation unit compiles: `pico/pico.h` includes `pico/version.h`,
which does not exist in the SDK tree. `pico_sdk_init()` writes it, along
with `pico/config_autogen.h`, into the build directory, and the boot2
stage is likewise compiled, linked, padded and checksummed into
`bs2_default_padded_checksummed.S` during the CMake build. Route 2 runs
only once those three generated inputs are supplied from route 1.

So a CMake-free SDK build is not one of the options. The choice is CMake
plus Ninja against CMake plus some other driver, and the driver is worth
0.05 s of a 1.3 s build.

## Measurements

Median of three runs. cmake 4.4.3, ninja 1.13.2, arm-none-eabi-gcc 16.2.0,
picotool 2.3.1, Pico SDK at `/usr/share/pico-sdk` as a system package,
12 hardware threads.

| route | configure | cold build | warm no-op | output |
| --- | --- | --- | --- | --- |
| 1: SDK, CMake, Ninja | 0.47 s | 0.86 s | under 0.01 s | text 35132, bss 2840 |
| 2: same commands, make | generated inputs from route 1 | 0.81 s | under 0.01 s | text 35132, bss 2840 |
| 3: the port, bmake, romprobe | none | 0.06 s | 0.02 s | 6200-byte a.out |

Route 1 compiles 91 objects, route 2 the 87 that reach the link; the
difference is the separately built boot2 stage.

Routes 1 and 2 produce identical `text`, `data` and `bss` to the byte. The
ELF files differ, and only in the build paths that route 1 and route 2
embed in debug info. The compiler does the same work either way, which is
what makes the driver comparison fair.

## Prediction against outcome

The prediction was recorded before the first measurement. Two of its five
claims failed.

Configure was predicted at 3 to 15 s and measured 0.47 s. The prediction
assumed the SDK arrives as a clone or a submodule, where the first
configure pays for fetching it. Here the SDK is an installed system
package and `pico_sdk_import.cmake` resolves `PICO_SDK_PATH` to
`/usr/share/pico-sdk`, so configure does header generation and nothing
else. A host that clones the SDK per project pays that cost once per
checkout, outside this measurement.

Warm no-op was predicted at 50 to 150 ms for ninja and 10 to 50 ms for
make. Both measured below the 10 ms resolution of the timer. The claim
that ninja carries meaningfully more no-op overhead than make does not
survive at this project size.

The size claim held exactly: identical `text`, `data` and `bss`. The claim
that route 2 would be under twice route 1's cold time held at the near
end, 0.81 s against 0.86 s. The claim that romprobe builds far faster and
far smaller held: 14 times faster cold, and an a.out a fifth the size of
route 1's text.

## What the numbers mean for where flash-id lives

Build time is not the argument. Every route finishes in about a second,
and the driver accounts for a twentieth of that.

The argument is the dependency and the image. Route 1 requires cmake,
ninja, the Pico SDK and its generated tree; route 3 requires this tree's
own `bmake` and a built `lib/crt0.o`. flash-id links `no_flash`, so its
35132 bytes of text and 2840 of bss run entirely from the 264 KB of SRAM,
and most of that is the TinyUSB device stack and `printf` that
`pico_enable_stdio_usb` pulls in, not the probe. The probe itself is two
`flash_do_cmd` calls.

The SDK is not a barrier to either. It is BSD 3-Clause, and this tree
already carries SDK-derived code: `sys/arch/rp2040/boot2/boot2_w25q080.S`
is the Pico SDK second-stage boot code, attributed in NOTICE section 4.
Redistribution was never the obstacle, and the dependency is toolchain
surface -- cmake, ninja and a checkout of the SDK -- which a pinned fetch
resolves the same way `tools/renode/fetch-renode-rp2040.sh` resolves the
Renode peripheral models. `check-flash-id` builds the probe against an SDK
resolved through `tools/pico-sdk/sdk-path.sh`, and stands outside `check`
for the same reason `check-renode` does.

What a move would cost is a rewrite, not a build-file change. The port has
its own CDC-ACM driver in `sys/arch/rp2040/dev/usb.c` and its own flash
driver in `dev/flash.c`; a port-native probe would be written against
those rather than against `pico_stdlib` and `hardware_flash`, and the
measurements say it would buy an image in romprobe's size class rather
than route 1's, at no meaningful change in build time.

So flash-id stays in the notes repository because nothing requires it to
move, not because the SDK keeps it out.

## Reproducing

```sh
PORT_ROOT=/path/to/built/port-tree \
    sh tools/bench-flash-id-build.sh /path/to/notes/tools/flash-id
```

The script reports `not run` with the missing prerequisite named when
cmake, ninja, the cross compiler, `PICO_SDK_PATH`, the flash-id source,
the port's built tools, or `lib/crt0.o` is absent, rather than failing
inside a build. Route 3 links against `lib/crt0.o`, so `PORT_ROOT` must
name a tree that `bmake MACHINE=rp2040 build` has populated.
