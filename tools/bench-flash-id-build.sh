#!/bin/sh
# Measure the three build routes that reach an RP2040 image, and print one
# row per route: configure, cold build, warm no-op, and output size.
#
# The routes differ in what drives the compiler, not in what the compiler
# does. Route 1 is the Pico SDK through CMake and Ninja, as tools/flash-id
# builds. Route 2 replays that build's own
# compile and link commands from a Makefile, which isolates the driver:
# CMake still supplies the generated headers and the boot2 stage, because
# the SDK sources do not compile without them. Route 3 is the port's own
# bmake reaching a board binary through share/mk/sys.mk.
#
# Each timing is the median of three runs. A single run measures the page
# cache as much as the build.
#
# Usage: sh tools/bench-flash-id-build.sh [flash-id source directory]
# The source directory defaults to tools/flash-id.
# Route 3 links against lib/crt0.o and so wants a tree that `bmake
# MACHINE=rp2040 build` has already populated; PORT_ROOT names that tree
# when it is not the one holding this script. The SDK comes from
# tools/pico-sdk/sdk-path.sh, the same resolver check-flash-id uses.

set -eu

PYTHON=${PYTHON:-python3}
TOPSRC=$(cd "$(dirname "$0")/.." && pwd)
PORT_ROOT=${PORT_ROOT:-$TOPSRC}
FLASH_ID=${1:-$TOPSRC/tools/flash-id}
RUNS=3

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

note() { printf '%s\n' "$*" >&2; }

# Median of RUNS timings of a command, in seconds, to two decimals.
median_time() {
	: > "$WORK/times"
	_i=0
	while [ "$_i" -lt "$RUNS" ]; do
		# -o keeps the timing out of the command's own stderr.
		/usr/bin/time -f '%e' -o "$WORK/one" "$@" >/dev/null 2>/dev/null || {
			printf 'failed' ; return 0 ; }
		cat "$WORK/one" >> "$WORK/times"
		_i=$((_i + 1))
	done
	sort -n "$WORK/times" | awk 'NR==2{printf "%.2f", $1}'
}

have() { command -v "$1" >/dev/null 2>&1; }

for t in cmake ninja arm-none-eabi-gcc arm-none-eabi-size; do
	have "$t" || { note "bench: $t absent; not run"; exit 1; }
done
[ -f "$FLASH_ID/CMakeLists.txt" ] || {
	note "bench: no flash-id source at $FLASH_ID; not run"
	exit 1
}
# One resolver for the gate and the benchmark, so they cannot measure and
# build against different SDKs.
PICO_SDK_PATH=$(sh "$TOPSRC/tools/pico-sdk/sdk-path.sh") || {
	note "bench: no complete Pico SDK; not run"
	exit 1
}
export PICO_SDK_PATH

printf 'tool versions\n'
printf '  cmake            %s\n' "$(cmake --version | head -1)"
printf '  ninja            %s\n' "$(ninja --version)"
printf '  arm-none-eabi-gcc %s\n' "$(arm-none-eabi-gcc -dumpversion)"
have picotool && printf '  picotool         %s\n' "$(picotool version 2>&1 | head -1)"
printf '  PICO_SDK_PATH    %s\n' "$PICO_SDK_PATH"
printf '\n'

# Route 1: the Pico SDK through CMake and Ninja.
V1=$WORK/route1
mkdir -p "$V1"
cp "$FLASH_ID/main.c" "$FLASH_ID/CMakeLists.txt" "$FLASH_ID/pico_sdk_import.cmake" "$V1/"
cfg=$(cd "$V1" && median_time sh -c 'rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release')
(cd "$V1" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1)
cold=$(cd "$V1" && median_time sh -c 'ninja -C build -t clean >/dev/null && ninja -C build')
warm=$(cd "$V1" && median_time ninja -C build)
objs=$(find "$V1/build" -name '*.o' -o -name '*.obj' | wc -l)
set -- $(arm-none-eabi-size "$V1/build/flash_id.elf" | awk 'NR==2{print $1, $2, $3}')
printf 'route 1  SDK + CMake + Ninja\n'
printf '  configure %ss   cold %ss   warm %ss   objects %s\n' "$cfg" "$cold" "$warm" "$objs"
printf '  text %s  data %s  bss %s\n\n' "$1" "$2" "$3"

# Route 2: the same compiles and the same link, driven by make. CMake still
# generates pico/version.h, pico/config_autogen.h and the padded checksummed
# boot2 stage; the SDK sources do not compile without them.
V2=$WORK/route2
mkdir -p "$V2/build"
cp "$V1/main.c" "$V2/"
cp -r "$V1/build/generated" "$V2/build/"
mkdir -p "$V2/build/pico-sdk/src/rp2040/boot_stage2"
cp "$V1"/build/pico-sdk/src/rp2040/boot_stage2/bs2_default_padded_checksummed.S \
	"$V2/build/pico-sdk/src/rp2040/boot_stage2/"
(cd "$V1" && ninja -C build -t commands flash_id.elf) > "$WORK/commands.txt" 2>/dev/null
"$PYTHON" - "$WORK/commands.txt" "$V2" "$V1" <<'PY'
import sys, re, pathlib
cmds = pathlib.Path(sys.argv[1]).read_text().splitlines()
v2, v1 = pathlib.Path(sys.argv[2]), sys.argv[3]
objs, rules, link = [], [], None
for c in cmds:
    c = c.strip()
    if c.startswith(': && '):
        c = c[5:].split(' && ')[0]
    if ' -c ' in c and ' -o ' in c:
        o = re.search(r'-o (\S+)', c).group(1)
        objs.append(o)
        rules.append((o, c.split()[-1], c.replace(v1, str(v2))))
    elif 'flash_id.elf' in c and ' -c ' not in c and '-o' in c:
        link = c.replace(v1, str(v2))
mk = ["all: build/flash_id.elf", ""]
for o, src, c in rules:
    mk += ["%s: %s" % (o, src), "\t@mkdir -p $(@D)", "\t@%s" % c, ""]
# The recorded link writes a relative -o because ninja runs it from build/.
# The objects are relative to the project root, so retarget -o rather than
# cd into build/, which would put the objects out of reach.
link = link.replace("-o flash_id.elf", "-o build/flash_id.elf")
mk += ["build/flash_id.elf: %s" % " ".join(objs), "\t@%s" % link, ""]
(v2 / "Makefile").write_text("\n".join(mk))
PY
nproc_n=$(nproc 2>/dev/null || echo 4)
cold2=$(cd "$V2" && median_time sh -c "find . -name '*.o' -delete; make -s -j$nproc_n")
warm2=$(cd "$V2" && median_time make -s -j"$nproc_n")
printf 'route 2  SDK sources, same commands, driven by make\n'
printf '  configure (generated inputs come from route 1)   cold %ss   warm %ss\n' "$cold2" "$warm2"
if [ -f "$V2/build/flash_id.elf" ]; then
	set -- $(arm-none-eabi-size "$V2/build/flash_id.elf" | awk 'NR==2{print $1, $2, $3}')
	printf '  text %s  data %s  bss %s\n' "$1" "$2" "$3"
	if cmp -s "$V2/build/flash_id.elf" "$V1/build/flash_id.elf"; then
		printf '  ELF byte-identical to route 1\n\n'
	else
		printf '  ELF differs from route 1 in debug-info build paths\n\n'
	fi
else
	printf '  did not link; not run\n\n'
fi

# Route 3: the port's own build reaching a board binary.
R=$PORT_ROOT/tests/rp2040/romprobe
if ! have bmake; then
	printf 'route 3  not run: bmake absent\n'
elif [ ! -x "$PORT_ROOT/tools/bin/elf2aout" ]; then
	printf 'route 3  not run: %s holds no built tools\n' "$PORT_ROOT"
elif [ ! -f "$PORT_ROOT/lib/crt0.o" ]; then
	printf 'route 3  not run: %s is not built; romprobe links lib/crt0.o\n' "$PORT_ROOT"
else
	cold3=$(cd "$R" && median_time sh -c 'bmake MACHINE=rp2040 clean >/dev/null 2>&1; bmake MACHINE=rp2040')
	warm3=$(cd "$R" && median_time bmake MACHINE=rp2040)
	size3=$(wc -c < "$R/romprobe")
	(cd "$R" && bmake MACHINE=rp2040 clean >/dev/null 2>&1)
	printf 'route 3  the port, bmake, romprobe\n'
	printf '  configure none   cold %ss   warm %ss\n' "$cold3" "$warm3"
	printf '  a.out %s bytes\n' "$size3"
fi
