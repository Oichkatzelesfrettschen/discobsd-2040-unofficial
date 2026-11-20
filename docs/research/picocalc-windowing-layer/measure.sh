#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Rebuilds the sizes quoted in docs/research/picocalc-windowing-layer.md.
#
# usage: measure.sh OUTDIR
#   MW_DIR    Microwindows checkout (ghaerr/microwindows)
#   UGUI_DIR  uGUI checkout (achimdoebler/UGUI)
#   LVGL_DIR  LVGL checkout (lvgl/lvgl)
#   CC, SIZE, NM  tool names (default arm-none-eabi-gcc, -size, -nm)
#   JOBS      parallel compiles for LVGL (default 4)
# Nothing is cloned and nothing outside OUTDIR is written. OUTDIR must not exist
# or must be empty, so no object from an earlier run reaches a link. Each
# checkout must be the root of a git work tree at the revision pinned below,
# with no modified tracked file; any other state stops the script before a
# compile. Each figure is one line: FIG <name> text=<n> data=<n> bss=<n>
# (arm-none-eabi-size, Berkeley); STRUCT <name> bytes=<n> gives a type size.
set -eu

# Donor revisions measured; README.md states the same three values.
MW_REV=36c8d0edfa5423600027438b8f4bf0aa5bb49d28
UGUI_REV=ce0bccb5b7d4877c42081419fccadf7aa5727303
LVGL_REV=622f63df2f7da0c063eb26e6d1fa2702822f6193

die() { echo "measure.sh: $*" >&2; exit 1; }

[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
OUT=$1
: "${MW_DIR:?set MW_DIR}" "${UGUI_DIR:?set UGUI_DIR}" "${LVGL_DIR:?set LVGL_DIR}"
CC=${CC:-arm-none-eabi-gcc}
SIZE=${SIZE:-arm-none-eabi-size}
NM=${NM:-arm-none-eabi-nm}
JOBS=${JOBS:-4}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
[ -z "$(ls -A "$OUT")" ] || die "output directory $OUT is not empty"
OUT=$(cd "$OUT" && pwd)
MW=$(cd "$MW_DIR" && pwd -P)
UGUI=$(cd "$UGUI_DIR" && pwd -P)
LVGL=$(cd "$LVGL_DIR" && pwd -P)

# A checkout names its own work tree root (a subdirectory of another
# repository would report that repository's HEAD) and its pinned revision.
pin() { # name dir revision
	top=$(git -C "$2" rev-parse --show-toplevel 2>/dev/null) || die "$1: $2 is not a git checkout"
	[ "$top" = "$2" ] || die "$1: $2 is not the root of a git work tree"
	head=$(git -C "$2" rev-parse HEAD)
	[ "$head" = "$3" ] || die "$1: checkout is at $head, pinned revision is $3"
	[ -z "$(git -C "$2" status --porcelain --untracked-files=no)" ] || die "$1: checkout has modified tracked files"
}
pin Microwindows "$MW" "$MW_REV"
pin uGUI "$UGUI" "$UGUI_REV"
pin LVGL "$LVGL" "$LVGL_REV"

ARCH="-Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections"
LD="-mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles -Wl,--gc-sections"

fig() { # name elf
	set -- "$1" $("$SIZE" "$2" | awk 'NR == 2 { print $1, $2, $3 }')
	printf 'FIG %s text=%s data=%s bss=%s\n' "$1" "$2" "$3" "$4"
}

# Dummy definitions: libc, driver and unused-depth tables become absolute
# symbols, so the figures cover the engine and server code only.
SYMS_ALL="calloc free malloc realloc memcpy memset memmove strlen strncpy strcasecmp strcmp strcpy GdError GdDelay GdGetTickCount GdPlatformInit _exit kbddev mousedev panel_flush fprintf printf sprintf strncmp abs atoi getenv strchr strdup time gettimeofday snprintf qsort select signal exit open close read write ioctl"
SYMS_FLOOR="$SYMS_ALL mwstdpal8 fblinear8 fblinear16 fblinear24 fblinear32bgra fblinear32rgba"
lnk() { # elf, then linker arguments; ld warnings go to a log next to the output
	e=$1; shift
	"$CC" "$@" -o "$e" 2> "$e.log" || { cat "$e.log" >&2; exit 1; }
}
defsyms() { for s in $1; do printf ' -Wl,--defsym=%s=0x20000001' "$s"; done; }

# --- Microwindows ----------------------------------------------------------
MWBASE="$ARCH -Iinclude -Idrivers -Wall -Wno-unused -DNONETWORK=1 -DNDEBUG=1 -DHAVE_SELECT=0 -DHAVE_SIGNAL=0 -DHAVE_MMAP=0 -DHAVE_FLOAT=0 -DHAVE_FILEIO=0 -DUNIX=0 -DMWPIXEL_FORMAT=MWPF_PALETTE -DSCREEN_PIXTYPE=MWPF_PALETTE -DMW_FEATURE_IMAGES=0 -DUSE_ALLOCA=0 -DMW_FEATURE_TIMERS=0 -DSCREEN_DEPTH=4"
TINYF="-DMW_FEATURE_TINY=1 -DMW_FEATURE_SHAPES=0 -DMW_FEATURE_INTL=0 -DPOLYREGIONS=0 -DMW_FEATURE_PORTRAIT=0"
FULLF="-DMW_FEATURE_TINY=0 -DMW_FEATURE_SHAPES=1 -DMW_FEATURE_INTL=0 -DPOLYREGIONS=1 -DMW_FEATURE_PORTRAIT=0"
NXF="$FULLF -DNANOWM=1 -Inanox"

ENG_TINY="engine/devopen engine/devdraw engine/devblit engine/convblit_8888 engine/convblit_mask engine/convblit_frameb engine/devfont engine/devclip engine/devrgn engine/devpal1 engine/devpal2 engine/devpal4 engine/devlist engine/devstipple engine/devmouse engine/devkbd engine/devtimer engine/selfont engine/font_dbcs drivers/fb drivers/fblin4 drivers/fblin1 drivers/fblin2 drivers/genmem drivers/genfont drivers/rasterops fonts/compiled/X6x13 fonts/compiled/winFreeSansSerif11x13"
ENG_FULL="$ENG_TINY engine/devarc engine/devpoly engine/devrgn2"
NX_SRV="nanox/srvmain nanox/srvfunc nanox/srvevent nanox/srvutil nanox/srvclip nanox/srvnonet nanox/nxutil nanox/nxdraw nanox/wmaction nanox/wmclients nanox/wmevents nanox/wmutil nanox/nxpaintnc"
UNPATCHED_EXTRA="drivers/fblin8 drivers/fblin16 drivers/fblin24 drivers/fblin32 engine/devpal8 drivers/osdep"

mwobj() { # dir flags source-without-.c ; source may be under MW/src or HERE
	o=$1/$(printf '%s' "$3" | tr / _).o
	if [ -f "$MW/src/$3.c" ]; then src="$MW/src/$3.c"; else src="$HERE/$3.c"; fi
	(cd "$MW/src" && $CC $2 -c "$src" -o "$o")
	printf '%s\n' "$o"
}
mwbuild() { # dir flags names...
	d=$1; f=$2; shift 2
	mkdir -p "$d"
	for n in "$@"; do mwobj "$d" "$f" "$n" >> "$d/objs.txt"; done
}

# 1. engine closure: 4-bpp palette, ten-call stub (main_stub.c)
T=$OUT/mw_tiny; mkdir -p "$T"; : > "$T/objs.txt"
mwbuild "$T" "$MWBASE $TINYF" $ENG_TINY scr_stub main_stub
lnk "$T/a.elf" $LD -Wl,-e,app $(defsyms "$SYMS_FLOOR") $(cat "$T/objs.txt") -lgcc
fig mw_engine_closure "$T/a.elf"

# 2. Nano-X server (NONETWORK) plus nanowm: floor, then unpatched, then ceiling
N=$OUT/mw_nx; mkdir -p "$N"; : > "$N/objs.txt"
mwbuild "$N" "$MWBASE $NXF" $ENG_FULL $NX_SRV scr_stub main_nx
lnk "$N/floor.elf" $LD -Wl,-e,app $(defsyms "$SYMS_FLOOR") $(cat "$N/objs.txt") -lgcc
fig mw_nanox_wm_floor "$N/floor.elf"
# Per-object sizes in the Nano-X flag set (all shapes and polygon regions on).
for n in engine_devrgn engine_devdraw engine_devfont drivers_fblin4 engine_devclip drivers_genmem drivers_rasterops engine_convblit_frameb; do
	fig "mw_nx_obj_$n" "$N/$n.o"
done
mwbuild "$N" "$MWBASE $NXF" $UNPATCHED_EXTRA
lnk "$N/unpatched.elf" $LD -Wl,-e,app $(defsyms "$SYMS_ALL") $(cat "$N/objs.txt") -lgcc
fig mw_nanox_wm_unpatched "$N/unpatched.elf"
$SIZE $(cat "$N/objs.txt") | awk 'NR > 1 && $1 ~ /^[0-9]+$/ { t += $1; d += $2; b += $3 }
	END { printf "FIG mw_nanox_wm_all_objects text=%d data=%d bss=%d\n", t, d, b }'

# Type sizes of the Nano-X server structures and the region types (sizes.c
# declares one constant array per type; its symbol size is sizeof).
S=$OUT/sizes; mkdir -p "$S"
(cd "$MW/src" && $CC $MWBASE $NXF -c "$HERE/sizes.c" -o "$S/sizes.o")
"$NM" -S "$S/sizes.o" | awk '$4 ~ /^sz_/ { print $4, $2 }' | while read -r name hex; do
	printf 'STRUCT %s bytes=%d\n' "${name#sz_}" "0x$hex"
done

# --- purpose-built core prototype -------------------------------------------
P=$OUT/proto; mkdir -p "$P"
$CC -Os -mcpu=cortex-m0plus -mthumb -Wall -Wextra -ffunction-sections -fdata-sections -c "$HERE/purpose.c" -o "$P/purpose.o"
fig own_core_prototype "$P/purpose.o"

# --- uGUI --------------------------------------------------------------------
U=$OUT/ugui; mkdir -p "$U"
(cd "$UGUI" && $CC -Os -mcpu=cortex-m0plus -mthumb -ffunction-sections -I"$HERE" -c ugui.c -o "$U/ugui.o")
fig ugui_whole_library "$U/ugui.o"

# --- LVGL: label + button, 10-row partial buffer, 24 KiB pool ---------------
V=$OUT/lvgl; mkdir -p "$V/obj" "$V/conf"
awk '
/^#if 0 \/\* Set this to "1" to enable content \*\/$/ { print "#if 1 /* Set this to \"1\" to enable content */"; next }
/^#define LV_USE_[A-Z0-9_]+[ \t]+1([ \t]|$)/ {
	n = $2
	if (n != "LV_USE_STDLIB_MALLOC" && n != "LV_USE_STDLIB_STRING" && n != "LV_USE_STDLIB_SPRINTF" && n != "LV_USE_DRAW_SW" && n != "LV_USE_LABEL" && n != "LV_USE_BUTTON")
		$3 = "0"
}
/^#define LV_DRAW_SW_SUPPORT_[A-Z0-9_]+[ \t]+1([ \t]|$)/ && $2 != "LV_DRAW_SW_SUPPORT_RGB565" { $3 = "0" }
/^#define LV_MEM_SIZE / { print "#define LV_MEM_SIZE (24U * 1024U)"; next }
{ print }' "$LVGL/lv_conf_template.h" > "$V/conf/lv_conf.h"
LVF="$ARCH -I$V/conf -I$LVGL -DLV_CONF_INCLUDE_SIMPLE"
(cd "$LVGL" && {
	find src -name '*.c' | grep -v '^src/drivers\|^src/libs\|^src/others\|^src/osal\|^src/debugging\|^src/fs\|/vg_lite\|/nxp\|/renesas\|/opengles\|/espressif\|/arm2d\|/dma2d\|/sdl\|/convert\|/ppa\|/g2d\|/ambiq\|/eve\|/nema\|/vglite\|/pxp\|/stm32\|/ti\|/cairo\|/blend/neon\|/blend/helium\|/blend/riscv\|/blend/arm2d\|/helium' || true
	find src \( -name lv_tick.c -o -name lv_fs.c -o -name lv_os_none.c -o -name 'lv_draw_buf_convert*.c' -o -name lv_draw_sw_utils.c \)
} | sort -u > "$V/files.txt")
export CC LVF V LVGL
(cd "$LVGL" && xargs -P "$JOBS" -I{} sh -c 'o=$V/obj/$(echo {} | tr / _ | sed "s/\.c\$/.o/"); $CC $LVF -c {} -o "$o"' < "$V/files.txt")
$CC $LVF -c "$HERE/main_lv.c" -o "$V/obj/main_lv.o"
lnk "$V/lvgl.elf" $LD -Wl,-e,app $(defsyms "memcpy memset strlen memmove strcmp memcmp strcpy lv_lock lv_unlock lv_os_init") "$V"/obj/*.o -lgcc
fig lvgl_label_button "$V/lvgl.elf"
