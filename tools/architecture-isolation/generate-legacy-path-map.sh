#!/bin/sh
# Generate the exact source-to-archive map from a pinned Git revision.

set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 SOURCE_REVISION" >&2
	exit 2
fi

source_revision=$1
source_root=$(git rev-parse --show-toplevel)
temporary_parent=${TMPDIR:-/tmp}
if [ ! -d "$temporary_parent" ]; then
	echo "$temporary_parent: temporary parent is not a directory" >&2
	exit 2
fi
temporary_parent=$(CDPATH= cd "$temporary_parent" && pwd -P)
temporary_directory=$(mktemp -d "$temporary_parent/discobsd-legacy-map-generate.XXXXXX")
rows_path=$temporary_directory/rows.tsv
sorted_rows_path=$temporary_directory/rows.sorted.tsv

cleanup_temporary_directory()
{
	case $temporary_directory in
	"$temporary_parent"/discobsd-legacy-map-generate.*)
		find "$temporary_directory" -depth \
		    \( -type f -o -type l \) -exec unlink {} \; 2>/dev/null || :
		find "$temporary_directory" -depth -type d \
		    -exec rmdir {} \; 2>/dev/null || :
		;;
	*)
		echo "legacy map generator: refusing temporary cleanup outside $temporary_parent" >&2
		;;
	esac
}

trap cleanup_temporary_directory EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

git -C "$source_root" rev-parse --verify "$source_revision^{commit}" >/dev/null
: > "$rows_path"

add_file()
{
	class_name=$1
	source_path=$2
	destination_path=$3
	archive_policy=immutable
	if [ "$class_name" = legacy-pdp11-v6 ]; then
		case $source_path in
		usr.bin/pdp11/COPYING|usr.bin/pdp11/Caldera-license.pdf|usr.bin/pdp11/v6.rk.gz)
			;;
		*)
			archive_policy=maintained
			;;
		esac
	fi

	if ! git -C "$source_root" cat-file -e "$source_revision:$source_path"; then
		echo "$source_revision: missing declared source $source_path" >&2
		exit 1
	fi
	origin_blob=$(git -C "$source_root" rev-parse "$source_revision:$source_path")
	content_sha256=$(
		git -C "$source_root" show "$source_revision:$source_path" |
		    sha256sum | awk '{ print $1 }'
	)
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
	    "$class_name" "$source_path" "$destination_path" \
	    "$origin_blob" "$content_sha256" "$archive_policy" >> "$rows_path"
}

add_tree()
{
	class_name=$1
	destination_prefix=$2
	source_path=$3

	git -C "$source_root" ls-tree -r --name-only "$source_revision" -- \
	    "$source_path" |
	while IFS= read -r tracked_path; do
		add_file "$class_name" "$tracked_path" \
		    "$destination_prefix/$tracked_path"
		printf '%s\n' matched > "$temporary_directory/matched"
	done
	if [ ! -s "$temporary_directory/matched" ]; then
		echo "$source_revision: declaration matched no files: $source_path" >&2
		exit 1
	fi
	: > "$temporary_directory/matched"
}

non_arm_prefix=legacy/non-arm/mips-pic32
for source_path in \
    sys/arch/pic32 \
    distrib/pic32 \
    include/pic32 \
    lib/libc/mips \
    lib/startup-mips \
    lib/elf32-mips.ld \
    lib/libicache \
    share/mk/mips-toolchain.mk \
    tools/virtualmips \
    tools/icache \
    tools/mkrd \
    tools/openbsd/ports/mystuff/devel/mips-elf \
    usr.bin/adb \
    usr.bin/aout \
    usr.bin/cc \
    usr.bin/cpp \
    usr.bin/lcc \
    usr.bin/lcpp \
    usr.bin/lccom \
    usr.bin/ccom \
    usr.bin/smallc \
    usr.bin/glcdtest \
    usr.bin/portio \
    usr.bin/pwm \
    usr.bin/wiznet \
    usr.bin/smux \
    lib/libwiznet \
    lib/libc_aout/libwiznet \
    include/wiznet \
    include/smallc \
    share/examples/asm \
    share/examples/cube \
    share/examples/sensors \
    share/examples/gpanel \
    share/examples/smallc \
    lib/libgpanel \
    lib/libc_aout/libgpanel \
    sys/sys/gpio.h \
    sys/sys/glcd.h \
    sys/sys/gpanel.h \
    sys/sys/pwm.h \
    share/examples/c/adc.c \
    share/examples/c/gpio.c \
    share/examples/c/lcd6.c \
    share/examples/c/tetris.c \
    lib/libc/runtime/sc_case.S
do
	add_tree legacy-non-arm "$non_arm_prefix" "$source_path"
done

for source_path in \
    usr.bin/as/as.c \
    usr.bin/as/mips-instruction-set.txt \
    usr.bin/as/tests/hello.c \
    usr.bin/as/tests/test1.s \
    usr.bin/as/tests/test2.s \
    usr.bin/as/tests/test3.s \
    usr.bin/as/tests/test4.s \
    usr.bin/as/tests/test5.s \
    tools/aoututils/as/as.c \
    tools/aoututils/aout/.gitignore \
    tools/aoututils/aout/Makefile \
    tools/aoututils/aout/aout.c \
    tools/aoututils/aout/mips-dis.c \
    tools/aoututils/aout/mips-opc.c \
    tools/aoututils/aout/mips-opcode.h \
    tools/aoututils/aout/mips16-opc.c \
    usr.bin/smlrc/cgmips.c \
    usr.bin/smlrc/lb.c
do
	add_file legacy-non-arm "$source_path" "$non_arm_prefix/$source_path"
done

add_file legacy-non-arm usr.bin/smlrc/cgx86.c \
    legacy/non-arm/toolchains/usr.bin/smlrc/cgx86.c

x86_win32_prefix=legacy/non-arm/x86-win32
for source_path in \
    usr.bin/pforth/pf_win32.h \
    usr.bin/picoc/msvc/picoc/picoc.sln \
    usr.bin/picoc/msvc/picoc/picoc.vcxproj \
    usr.bin/picoc/msvc/picoc/picoc.vcxproj.filters \
    usr.bin/picoc/platform/library_msvc.c \
    usr.bin/picoc/platform/platform_msvc.c
do
	add_file legacy-non-arm "$source_path" \
	    "$x86_win32_prefix/$source_path"
done

blackfin_surveyor_prefix=legacy/non-arm/blackfin-surveyor
for source_path in \
    usr.bin/picoc/platform/library_srv1.c \
    usr.bin/picoc/platform/library_surveyor.c \
    usr.bin/picoc/platform/platform_surveyor.c
do
	add_file legacy-non-arm "$source_path" \
	    "$blackfin_surveyor_prefix/$source_path"
done

unsupported_platform_prefix=legacy/unsupported-platforms
for source_path in \
    usr.bin/picoc/platform/library_ffox.c \
    usr.bin/picoc/platform/platform_ffox.c
do
	add_file legacy-unsupported-platform "$source_path" \
	    "$unsupported_platform_prefix/$source_path"
done

pdp11_vax_prefix=legacy/non-arm/pdp11-vax
for source_path in \
    include/Makefile.install \
    usr.bin/compress/USERMEM \
    usr.bin/compress/usermem.sh \
    usr.bin/uucp/acucntrl.8 \
    usr.bin/uucp/acucntrl.c \
    share/man/man2/fetchi.2 \
    share/man/man2/fperr.2 \
    share/man/man2/lock.2 \
    share/man/man2/nostk.2 \
    share/man/man2/phys.2 \
    share/man/man4/acc.4 \
    share/man/man4/br.4 \
    share/man/man4/cons.4 \
    share/man/man4/css.4 \
    share/man/man4/de.4 \
    share/man/man4/dh.4 \
    share/man/man4/dhu.4 \
    share/man/man4/dhv.4 \
    share/man/man4/dmc.4 \
    share/man/man4/dr.4 \
    share/man/man4/dz.4 \
    share/man/man4/ec.4 \
    share/man/man4/en.4 \
    share/man/man4/hk.4 \
    share/man/man4/ht.4 \
    share/man/man4/hy.4 \
    share/man/man4/il.4 \
    share/man/man4/imp.4 \
    share/man/man4/impconf.4 \
    share/man/man4/intro.4 \
    share/man/man4/lp.4 \
    share/man/man4/qe.4 \
    share/man/man4/ra.4 \
    share/man/man4/rk.4 \
    share/man/man4/rl.4 \
    share/man/man4/rx.4 \
    share/man/man4/si.4 \
    share/man/man4/sri.4 \
    share/man/man4/tm.4 \
    share/man/man4/tmscp.4 \
    share/man/man4/ts.4 \
    share/man/man4/vv.4 \
    share/man/man4/xp.4 \
    share/man/man5/dtab.5 \
    share/man/man5/stack.5 \
    share/man/man8/autoconfig.8 \
    share/man/man8/boot.8 \
    share/man/man8/crash.8 \
    share/man/man8/format.8 \
    share/man/man8/makedev.8
do
	add_file legacy-non-arm "$source_path" \
	    "$pdp11_vax_prefix/$source_path"
done

vax_vms_prefix=legacy/non-arm/vax-vms
for source_path in \
    usr.bin/uucp/vms.tar.Z \
    usr.bin/zmodem/vmodem.h \
    usr.bin/zmodem/vrzsz.c \
    usr.bin/zmodem/vupl.t \
    usr.bin/zmodem/vuplfile.t \
    usr.bin/zmodem/vvmodem.c
do
	add_file legacy-non-arm "$source_path" "$vax_vms_prefix/$source_path"
done

add_file legacy-non-arm usr.bin/zmodem/makefile.generic \
    legacy/non-arm/toolchains/usr.bin/zmodem/makefile.generic

pdp11_prefix=legacy/pdp11-v6
add_tree legacy-pdp11-v6 "$pdp11_prefix" usr.bin/pdp11
add_tree legacy-pdp11-v6 "$pdp11_prefix" tests/pdp11_reference
add_tree legacy-pdp11-v6 "$pdp11_prefix" \
    docs/research/v6-emulator-on-discobsd.md

LC_ALL=C sort -t "$(printf '\t')" -k2,2 "$rows_path" > "$sorted_rows_path"

duplicate_source=$(cut -f2 "$sorted_rows_path" | uniq -d | sed -n '1p')
if [ -n "$duplicate_source" ]; then
	echo "duplicate source path: $duplicate_source" >&2
	exit 1
fi
duplicate_destination=$(cut -f3 "$sorted_rows_path" | LC_ALL=C sort | uniq -d | sed -n '1p')
if [ -n "$duplicate_destination" ]; then
	echo "duplicate destination path: $duplicate_destination" >&2
	exit 1
fi

non_arm_count=$(awk -F '\t' '$1 == "legacy-non-arm" { count++ } END { print count + 0 }' "$sorted_rows_path")
pdp11_count=$(awk -F '\t' '$1 == "legacy-pdp11-v6" { count++ } END { print count + 0 }' "$sorted_rows_path")
unsupported_platform_count=$(awk -F '\t' '$1 == "legacy-unsupported-platform" { count++ } END { print count + 0 }' "$sorted_rows_path")
if [ "$non_arm_count" -ne 1008 ] || [ "$pdp11_count" -ne 22 ] || \
    [ "$unsupported_platform_count" -ne 2 ]; then
	echo "legacy map count is non-arm=$non_arm_count pdp11-v6=$pdp11_count unsupported-platform=$unsupported_platform_count; expected 1008, 22, and 2" >&2
	exit 1
fi

printf '# source-revision\t%s\n' "$(git -C "$source_root" rev-parse "$source_revision^{commit}")"
printf 'class\tsource\tdestination\torigin_git_blob\torigin_sha256\tarchive_policy\n'
cat "$sorted_rows_path"
