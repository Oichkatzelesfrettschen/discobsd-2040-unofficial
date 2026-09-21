#!/bin/sh
# Verify the maintained ARM registry, legacy reachability, and cleanup guard.

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 SOURCE_ROOT MAKE" >&2
	exit 2
fi

source_root=$1
make_command=$2

# A parent bmake exports its command-line overrides through MAKEFLAGS. Nested
# probes select both maintained machines, so each probe must start from the
# explicit tuple on its own command line rather than inherit the caller tuple.
unset MAKEFLAGS MFLAGS

if [ ! -d "$source_root" ]; then
	echo "$source_root: source root is not a directory" >&2
	exit 2
fi
source_root=$(CDPATH= cd "$source_root" && pwd -P)
if [ ! -x "$make_command" ] && ! command -v "$make_command" >/dev/null 2>&1; then
	echo "$make_command: make command is not executable" >&2
	exit 2
fi

temporary_parent=${TMPDIR:-/tmp}
if [ ! -d "$temporary_parent" ]; then
	echo "$temporary_parent: temporary parent is not a directory" >&2
	exit 2
fi
temporary_parent=$(CDPATH= cd "$temporary_parent" && pwd -P)
temporary_directory=$(mktemp -d "$temporary_parent/discobsd-architecture-isolation.XXXXXX")

cleanup_temporary_directory()
{
	case $temporary_directory in
	"$temporary_parent"/discobsd-architecture-isolation.*)
		find "$temporary_directory" -depth \
		    \( -type f -o -type l \) -exec unlink {} \; 2>/dev/null || :
		find "$temporary_directory" -depth -type d \
		    -exec rmdir {} \; 2>/dev/null || :
		;;
	*)
		echo "architecture isolation: refusing temporary cleanup outside $temporary_parent" >&2
		;;
	esac
}

trap cleanup_temporary_directory EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

map_path=$source_root/tools/architecture-isolation/legacy-path-map.tsv
map_verifier=$source_root/tools/architecture-isolation/verify-legacy-path-map.sh
selector_allowlist=$source_root/tools/architecture-isolation/main-tree-portability-selector-allowlist.txt
retired_base_manifest_paths=$source_root/tools/architecture-isolation/retired-base-manifest-paths.txt
stamp_helper=$source_root/tools/check-build-machine.sh
cleanup_helper=$source_root/tools/clean-build-machines.sh
selector_expression='^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*(^|[^[:alnum:]_])(Alpha|Amiga|BORLAND_C|FLYINGFOX_HOST|M68K|MSC|MIPS|M_I86[A-Za-z0-9_]*|M_XENIX|PARIX|PDP11|POSIX|POSIX1|PowerPC|SPARC|SURVEYOR_HOST|SuperH|TR3200|UMON_HOST|VAX|VMS|VMSDTR|WIN32|XENIX_16|_MSC_VER|_WIN32|__alpha__|__hppa__|__i386__|__m68k__|__mips__|__mips64|__pdp11__|__powerpc__|__riscv|__sh__|__sparc__|__vax__|interdata|msdos|ns32000|pcxt|sel|z8000|OPT_MCU_PIC32MX|OPT_MCU_PIC32MM|OPT_MCU_PIC32MK|OPT_MCU_PIC24|OPT_MCU_DSPIC33|OPT_MCU_PIC32MZ|_mips|__hpux|hpux|mips|pdp11|sun|__sun|vax|vax11c|vms|x86)([^[:alnum:]_]|$)'
pass_count=0

fail()
{
	echo "architecture isolation: $*" >&2
	exit 1
}

record_pass()
{
	pass_count=$((pass_count + 1))
}

expect_equal()
{
	comparison_label=$1
	expected_value=$2
	actual_value=$3
	if [ "$actual_value" != "$expected_value" ]; then
		fail "$comparison_label is '$actual_value'; expected '$expected_value'"
	fi
	record_pass
}

normalize_words()
{
	printf '%s\n' "$1" | awk '{$1=$1; print}'
}

scan_portability_selectors()
{
	selector_root=$1
	selector_output=$2
	git -C "$selector_root" grep -n -E "$selector_expression" \
	    -- ':!legacy/**' | \
	    sed -E 's/^([^:]+):[0-9]+:[[:space:]]*/\1:/' | \
	    LC_ALL=C sort -u >"$selector_output"
}

expect_make_rejection()
{
	rejection_label=$1
	expected_message=$2
	shift 2
	rejection_log=$temporary_directory/make-rejection-$pass_count.log
	if "$make_command" -C "$source_root" "$@" -V MACHINE \
	    >"$rejection_log" 2>&1; then
		fail "$rejection_label was accepted"
	fi
	if ! grep -Fq "$expected_message" "$rejection_log"; then
		cat "$rejection_log" >&2
		fail "$rejection_label missed rejection message '$expected_message'"
	fi
	record_pass
}

capture_dry_run()
{
	dry_run_label=$1
	dry_run_output=$2
	shift 2
	if ! "$make_command" -C "$source_root" -n "$@" \
	    >"$dry_run_output" 2>"$dry_run_output.stderr"; then
		cat "$dry_run_output.stderr" >&2
		fail "$dry_run_label dry run failed"
	fi
}

verify_default_traversal()
{
	traversal_path=$1
	if grep -E 'legacy/(non-arm|pdp11-v6|unsupported-platforms)' \
	    "$traversal_path" >/dev/null || \
	    grep -F 'usr.bin/pdp11' "$traversal_path" >/dev/null || \
	    grep -F 'tests/pdp11_reference' "$traversal_path" >/dev/null || \
	    grep -E '(^|[[:space:]/])simh([[:space:]/]|$)' \
	    "$traversal_path" >/dev/null || \
	    grep -F '/usr/v6' "$traversal_path" >/dev/null
	then
		return 1
	fi
	return 0
}

expect_default_traversal()
{
	traversal_label=$1
	traversal_path=$2
	if ! verify_default_traversal "$traversal_path"; then
		grep -n -E 'legacy/(non-arm|pdp11-v6|unsupported-platforms)' \
		    "$traversal_path" >&2 || :
		grep -n -F 'usr.bin/pdp11' "$traversal_path" >&2 || :
		grep -n -F 'tests/pdp11_reference' "$traversal_path" >&2 || :
		grep -n -E '(^|[[:space:]/])simh([[:space:]/]|$)' \
		    "$traversal_path" >&2 || :
		grep -n -F '/usr/v6' "$traversal_path" >&2 || :
		fail "$traversal_label reaches a legacy source root"
	fi
	record_pass
}

expect_path_present()
{
	presence_label=$1
	presence_path=$2
	presence_text=$3
	if ! grep -Fq -- "$presence_text" "$presence_path"; then
		fail "$presence_label does not contain '$presence_text'"
	fi
	record_pass
}

verify_registry_include()
{
	grep -Eq '^[[:space:]]*include[[:space:]]+\$\{TOPSRC\}/share/mk/architecture\.mk([[:space:]]|$)' "$1"
}

verify_kernel_prerequisite_barrier()
{
	grep -Fq 'SYSTEM_DEP=	Makefile ioconf.c .WAIT machine sys .deps .WAIT ${SYSTEM_OBJ}' "$1"
}

unlink_test_path()
{
	test_path=$1
	if [ -e "$test_path" ] || [ -L "$test_path" ]; then
		unlink "$test_path"
	fi
}

verify_maintained_product()
{
	profile_path=$1
	manifest_path=$2
	! grep -E '(^|[[:space:]])(pdp11|v6disk|retro-lab)([[:space:]]|$)|/usr/bin/pdp11|/usr/v6' \
	    "$profile_path" "$manifest_path" >/dev/null
}

expect_map_rejection()
{
	map_label=$1
	mutated_map=$2
	expected_message=$3
	map_log=$temporary_directory/map-rejection-$pass_count.log
	if sh "$map_verifier" "$source_root" "$mutated_map" source \
	    >"$map_log" 2>&1; then
		fail "$map_label was accepted"
	fi
	if ! grep -Fq "$expected_message" "$map_log"; then
		cat "$map_log" >&2
		fail "$map_label missed rejection message '$expected_message'"
	fi
	record_pass
}

# The root build and both explicit maintained selections resolve to canonical
# ARM tuples. The architecture flags and tool directory derive from that tuple.
expect_equal "supported machines" "rp2040 stm32" \
    "$(normalize_words "$("$make_command" -C "$source_root" -V SUPPORTED_MACHINES)")"
expect_equal "default machine" "rp2040" \
    "$("$make_command" -C "$source_root" -V MACHINE)"
expect_equal "default architecture" "arm" \
    "$("$make_command" -C "$source_root" -V MACHINE_ARCH)"
expect_equal "default CPU" "cortex-m0plus" \
    "$("$make_command" -C "$source_root" -V MACHINE_CPU)"
expect_equal "default legacy non-ARM option" "no" \
    "$("$make_command" -C "$source_root" -V BUILD_LEGACY_NON_ARM)"
expect_equal "default PDP-11/V6 option" "no" \
    "$("$make_command" -C "$source_root" -V BUILD_PDP11_V6)"
expect_equal "default legacy traversal" "" \
    "$(normalize_words "$("$make_command" -C "$source_root" -V LEGACY_SUBDIRS)")"
expect_equal "RP2040 tool directory" "$source_root/tools/bin/rp2040" \
    "$("$make_command" -C "$source_root" MACHINE=rp2040 -V TOOLBINDIR)"
expect_equal "STM32 architecture" "arm" \
    "$("$make_command" -C "$source_root" MACHINE=stm32 -V MACHINE_ARCH)"
expect_equal "STM32 CPU" "cortex-m4" \
    "$("$make_command" -C "$source_root" MACHINE=stm32 -V MACHINE_CPU)"
expect_equal "STM32 tool directory" "$source_root/tools/bin/stm32" \
    "$("$make_command" -C "$source_root" MACHINE=stm32 -V TOOLBINDIR)"
expect_equal "RP2040 architecture flags" \
    "-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 -V MACHINE_ARCH_FLAGS)")"
expect_equal "STM32 architecture flags" \
    "-mcpu=cortex-m4 -mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=stm32 -V MACHINE_ARCH_FLAGS)")"
expect_equal "RP2040 kernel configurations" "PICO PICO_UART" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 -V MACHINE_KERNEL_CONFIGS)")"
expect_equal "STM32 kernel configurations" \
    "F405WEACTCORE F411RENUCLEO F412GDISCO F412WEACTCORE F413HDISCO F446RENUCLEO F446WEACTCORE F469IDISCO F4DISCOVERY F4VEDEVEBOX" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=stm32 -V MACHINE_KERNEL_CONFIGS)")"

# A representative generated Makefile for each machine must evaluate its own
# default tuple and machine-qualified tool directory, not merely contain an
# include-shaped line.
pico_compile=$source_root/sys/arch/rp2040/compile/PICO
f405_compile=$source_root/sys/arch/stm32/compile/F405WEACTCORE
expect_equal "PICO generated default machine" "rp2040" \
    "$("$make_command" -C "$pico_compile" -V MACHINE)"
expect_equal "PICO generated architecture" "arm" \
    "$("$make_command" -C "$pico_compile" -V MACHINE_ARCH)"
expect_equal "PICO generated CPU" "cortex-m0plus" \
    "$("$make_command" -C "$pico_compile" -V MACHINE_CPU)"
expect_equal "PICO generated tool directory" "$source_root/tools/bin/rp2040" \
    "$("$make_command" -C "$pico_compile" -V TOOLBINDIR)"
expect_equal "F405 generated default machine" "stm32" \
    "$("$make_command" -C "$f405_compile" -V MACHINE)"
expect_equal "F405 generated architecture" "arm" \
    "$("$make_command" -C "$f405_compile" -V MACHINE_ARCH)"
expect_equal "F405 generated CPU" "cortex-m4" \
    "$("$make_command" -C "$f405_compile" -V MACHINE_CPU)"
expect_equal "F405 generated tool directory" "$source_root/tools/bin/stm32" \
    "$("$make_command" -C "$f405_compile" -V TOOLBINDIR)"

# Each input below is a calibrated bad registry mutation. Parsing must fail at
# the registry rather than reaching compiler or source-directory selection.
expect_make_rejection "undeclared machine" "unsupported MACHINE=pic32" \
    MACHINE=pic32
expect_make_rejection "unknown machine" "unsupported MACHINE=unknown" \
    MACHINE=unknown
expect_make_rejection "RP2040 MIPS override" \
    "MACHINE=rp2040 requires MACHINE_ARCH=arm" \
    MACHINE=rp2040 MACHINE_ARCH=mips
expect_make_rejection "STM32 CPU override" \
    "MACHINE=stm32 requires MACHINE_CPU=cortex-m4" \
    MACHINE=stm32 MACHINE_CPU=cortex-m0plus
expect_make_rejection "private expected architecture override" \
    "private architecture registry variables cannot be overridden" \
    _EXPECTED_MACHINE_ARCH=mips
expect_make_rejection "private active-target override" \
    "private architecture registry variables cannot be overridden" \
    _ARCHITECTURE_ACTIVE_TARGETS=build
expect_make_rejection "private registry namespace override" \
    "private architecture registry variables cannot be overridden" \
    _DISCOBSD_MUTATION=yes
expect_make_rejection "supported-machine override" \
    "derived architecture registry variables cannot be overridden" \
    "SUPPORTED_MACHINES=rp2040 stm32 pic32"
expect_make_rejection "default-machine override" \
    "derived architecture registry variables cannot be overridden" \
    MACHINE_DEFAULT=stm32
expect_make_rejection "kernel-configuration override" \
    "derived architecture registry variables cannot be overridden" \
    MACHINE_KERNEL_CONFIGS=PICO
expect_make_rejection "architecture-flag override" \
    "derived architecture registry variables cannot be overridden" \
    MACHINE_ARCH_FLAGS=-mcpu=bogus
expect_make_rejection "legacy-subdirectory override" \
    "derived architecture registry variables cannot be overridden" \
    LEGACY_SUBDIRS=legacy/non-arm
expect_make_rejection "architecture-stamp override" \
    "derived architecture registry variables cannot be overridden" \
    ARCHITECTURE_STAMP=/tmp/mutation
expect_make_rejection "tool-directory override" \
    "TOOLBINDIR must be $source_root/tools/bin/rp2040" \
    MACHINE=rp2040 TOOLBINDIR=/tmp/mutation
expect_make_rejection "invalid non-ARM legacy option" \
    "BUILD_LEGACY_NON_ARM must be yes or no" \
    BUILD_LEGACY_NON_ARM=maybe
expect_make_rejection "invalid PDP-11/V6 option" \
    "BUILD_PDP11_V6 must be yes or no" \
    BUILD_PDP11_V6=maybe
expect_make_rejection "STM32 PDP-11/V6 option" \
    "BUILD_PDP11_V6=yes supports MACHINE=rp2040 only" \
    MACHINE=stm32 BUILD_PDP11_V6=yes

# Every generated maintained kernel Makefile includes the same registry. The
# expected configuration lists make additions and removals reviewed changes.
generated_makefile_count=0
for machine_name in rp2040 stm32; do
	machine_configurations=$("$make_command" -C "$source_root" \
	    MACHINE="$machine_name" -V MACHINE_KERNEL_CONFIGS)
	for configuration_name in $machine_configurations; do
		generated_makefile=$source_root/sys/arch/$machine_name/compile/$configuration_name/Makefile
		if [ ! -f "$generated_makefile" ]; then
			fail "$generated_makefile is absent"
		fi
		if ! verify_registry_include "$generated_makefile"; then
			fail "$generated_makefile omits the canonical registry"
		fi
		if ! verify_kernel_prerequisite_barrier "$generated_makefile"; then
			fail "$generated_makefile lets kernel objects race required links and directories"
		fi
		record_pass
		generated_makefile_count=$((generated_makefile_count + 1))
	done
done
expect_equal "generated ARM kernel Makefile count" "12" \
    "$generated_makefile_count"
for registry_consumer in "$source_root/Makefile" \
    "$source_root/share/mk/sys.mk"
do
	if ! verify_registry_include "$registry_consumer"; then
		fail "$registry_consumer omits the active canonical registry include"
	fi
	record_pass
done
comment_only_include=$temporary_directory/generated-Makefile.comment-only-registry
printf '%s\n' '# include ${TOPSRC}/share/mk/architecture.mk' \
    >"$comment_only_include"
if verify_registry_include "$comment_only_include"; then
	fail "comment-only registry include mutation was accepted"
fi
record_pass
mutated_generated_makefile=$temporary_directory/generated-Makefile.without-registry
sed '/share\/mk\/architecture\.mk/d' \
    "$source_root/sys/arch/rp2040/compile/PICO/Makefile" \
    >"$mutated_generated_makefile"
if verify_registry_include "$mutated_generated_makefile"; then
	fail "generated-Makefile missing-include mutation was accepted"
fi
record_pass
mutated_generated_makefile=$temporary_directory/generated-Makefile.without-prerequisite-barrier
sed 's/ machine sys \.deps \.WAIT ${SYSTEM_OBJ}/ machine sys .deps ${SYSTEM_OBJ}/' \
    "$source_root/sys/arch/stm32/compile/F405WEACTCORE/Makefile" \
    >"$mutated_generated_makefile"
if verify_kernel_prerequisite_barrier "$mutated_generated_makefile"; then
	fail "generated-Makefile missing-prerequisite-barrier mutation was accepted"
fi
record_pass

# Evaluated traversal proves the default and the archive-verification option
# remain outside every ordinary build. PDP-11/V6 joins RP2040 only on opt-in.
rp2040_build=$temporary_directory/rp2040-build.txt
rp2040_default=$temporary_directory/rp2040-default.txt
stm32_build=$temporary_directory/stm32-build.txt
non_arm_option_build=$temporary_directory/non-arm-option-build.txt
pdp11_option_build=$temporary_directory/pdp11-option-build.txt
default_host=$temporary_directory/default-host.txt
default_clean=$temporary_directory/default-clean.txt
pdp11_option_clean=$temporary_directory/pdp11-option-clean.txt
cleanall_output=$temporary_directory/cleanall.txt
rp2040_usr_bin_subdirs=$temporary_directory/rp2040-usr-bin-subdirs.txt
stm32_usr_bin_subdirs=$temporary_directory/stm32-usr-bin-subdirs.txt
capture_dry_run "RP2040 build" "$rp2040_build" MACHINE=rp2040 build
capture_dry_run "RP2040 default target" "$rp2040_default" MACHINE=rp2040
capture_dry_run "STM32 build" "$stm32_build" MACHINE=stm32 build
capture_dry_run "non-ARM archive option" "$non_arm_option_build" \
    MACHINE=rp2040 BUILD_LEGACY_NON_ARM=yes build
capture_dry_run "PDP-11/V6 option" "$pdp11_option_build" \
    MACHINE=rp2040 BUILD_PDP11_V6=yes build
capture_dry_run "default host tier" "$default_host" MACHINE=rp2040 check-host
capture_dry_run "default clean" "$default_clean" MACHINE=rp2040 clean
capture_dry_run "PDP-11/V6 option clean" "$pdp11_option_clean" \
    MACHINE=rp2040 BUILD_PDP11_V6=yes clean
capture_dry_run "unconditional cleanall" "$cleanall_output" \
    MACHINE=rp2040 cleanall

# The root build uses shell loops, so -n prints the loop without evaluating a
# nested Makefile. Query usr.bin directly and project each selected child back
# to its source path before applying the legacy-path oracle.
for machine_name in rp2040 stm32; do
	case $machine_name in
	rp2040)
		usr_bin_subdirs=$rp2040_usr_bin_subdirs
		;;
	stm32)
		usr_bin_subdirs=$stm32_usr_bin_subdirs
		;;
	esac
	usr_bin_words=$temporary_directory/$machine_name-usr-bin-words.txt
	if ! "$make_command" -C "$source_root/usr.bin" \
	    MACHINE="$machine_name" -V SUBDIR >"$usr_bin_words" 2>&1; then
		cat "$usr_bin_words" >&2
		fail "$machine_name usr.bin selector query failed"
	fi
	awk '{ for (field_index = 1; field_index <= NF; field_index++) print "usr.bin/" $field_index }' \
	    "$usr_bin_words" >"$usr_bin_subdirs"
done
expect_default_traversal "RP2040 build" "$rp2040_build"
expect_default_traversal "RP2040 default target" "$rp2040_default"
if ! cmp -s "$rp2040_build" "$rp2040_default"; then
	diff -u "$rp2040_build" "$rp2040_default" >&2 || :
	fail "RP2040 default target differs from the explicit build target"
fi
record_pass
mutated_root_makefile=$temporary_directory/Makefile.without-main
sed '/^[.]MAIN:[[:space:]]*all[[:space:]]*$/d' \
    "$source_root/Makefile" >"$mutated_root_makefile"
mutated_rp2040_default=$temporary_directory/rp2040-default.without-main.txt
capture_dry_run "RP2040 missing-default-main mutation" \
    "$mutated_rp2040_default" -f "$mutated_root_makefile" MACHINE=rp2040
if cmp -s "$rp2040_build" "$mutated_rp2040_default"; then
	fail "root missing-default-main mutation was accepted"
fi
record_pass
expect_default_traversal "STM32 build" "$stm32_build"
expect_default_traversal "non-ARM archive option build" "$non_arm_option_build"
expect_default_traversal "default host tier" "$default_host"
expect_default_traversal "default clean" "$default_clean"
expect_default_traversal "RP2040 usr.bin selection" "$rp2040_usr_bin_subdirs"
expect_default_traversal "STM32 usr.bin selection" "$stm32_usr_bin_subdirs"
expect_path_present "PDP-11/V6 option build" "$pdp11_option_build" \
    "legacy/pdp11-v6"
if grep -E 'legacy/(non-arm|unsupported-platforms)' \
    "$pdp11_option_build" >/dev/null; then
	fail "PDP-11/V6 option reaches another legacy class"
fi
record_pass
expect_path_present "PDP-11/V6 option clean" "$pdp11_option_clean" \
    "legacy/pdp11-v6"
expect_path_present "cleanall helper" "$cleanall_output" \
    "tools/clean-build-machines.sh"
expect_path_present "cleanall machine set" "$cleanall_output" \
    "rp2040 stm32"
mutated_traversal=$temporary_directory/default-build.with-legacy-edge
cp "$rp2040_build" "$mutated_traversal"
printf '%s\n' 'legacy/pdp11-v6' >>"$mutated_traversal"
if verify_default_traversal "$mutated_traversal"; then
	fail "default-traversal legacy-edge mutation was accepted"
fi
record_pass
mutated_usr_bin_makefile=$temporary_directory/usr.bin.Makefile.with-pdp11
cp "$source_root/usr.bin/Makefile" "$mutated_usr_bin_makefile"
printf '%s\n' 'SUBDIR+= pdp11' >>"$mutated_usr_bin_makefile"
mutated_usr_bin_words=$temporary_directory/usr-bin-words.with-pdp11
mutated_usr_bin_projection=$temporary_directory/usr-bin-projection.with-pdp11
if ! "$make_command" -C "$source_root/usr.bin" \
    -f "$mutated_usr_bin_makefile" MACHINE=rp2040 -V SUBDIR \
    >"$mutated_usr_bin_words" 2>&1; then
	cat "$mutated_usr_bin_words" >&2
	fail "usr.bin restored-PDP selector mutation did not evaluate"
fi
awk '{ for (field_index = 1; field_index <= NF; field_index++) print "usr.bin/" $field_index }' \
    "$mutated_usr_bin_words" >"$mutated_usr_bin_projection"
if verify_default_traversal "$mutated_usr_bin_projection"; then
	fail "usr.bin restored-PDP selector mutation was accepted"
fi
record_pass

# Both relocation modes decide the versioned 1,032-row denominator. Duplicate
# keys and changed origin hashes calibrate the rejecting side of the verifier.
map_row_count=$(awk -F '\t' \
    '$1 !~ /^#/ && $1 != "class" { row_count++ } END { print row_count + 0 }' \
    "$map_path")
expect_equal "legacy relocation row count" "1032" "$map_row_count"
for verification_mode in source archive; do
	verification_log=$temporary_directory/map-$verification_mode.log
	if ! sh "$map_verifier" "$source_root" "$map_path" "$verification_mode" \
	    >"$verification_log" 2>&1; then
		cat "$verification_log" >&2
		fail "legacy path map failed in $verification_mode mode"
	fi
	record_pass
done
duplicate_map=$temporary_directory/legacy-path-map.duplicate.tsv
awk -F '\t' '
    BEGIN { OFS="\t" }
    /^#/ || $1 == "class" { print; next }
    !saved { saved=$0; print; next }
    !mutated { print saved; mutated=1; next }
    { print }
' "$map_path" >"$duplicate_map"
expect_map_rejection "duplicate relocation key mutation" "$duplicate_map" \
    "duplicate source path"
hash_map=$temporary_directory/legacy-path-map.changed-hash.tsv
awk -F '\t' '
    BEGIN { OFS="\t"; zero="0000000000000000000000000000000000000000000000000000000000000000" }
    /^#/ || $1 == "class" { print; next }
    !mutated { $5=zero; mutated=1 }
    { print }
' "$map_path" >"$hash_map"
expect_map_rejection "changed relocation hash mutation" "$hash_map" \
    "origin SHA-256"

# Portability selectors that remain in maintained or imported generic code are
# a finite reviewed set. A repository-wide scan discovers additions anywhere
# outside legacy/, and the injected unrelated-file row calibrates that edge.
selector_scan=$temporary_directory/main-tree-portability-selectors.txt
scan_portability_selectors "$source_root" "$selector_scan"
if ! cmp -s "$selector_allowlist" "$selector_scan"; then
	diff -u "$selector_allowlist" "$selector_scan" >&2 || :
	fail "main-tree portability selector set differs from the allowlist"
fi
record_pass
selector_fixture=$temporary_directory/unrelated-maintained.c
for selector_mutation in WIN32 MIPS __mips__ VAX vax PDP11 pdp11 M_I86SM \
    ns32000 __powerpc__ __sparc__ __hppa__ TR3200 msdos
do
	printf '#ifdef %s\n#endif\n' "$selector_mutation" >"$selector_fixture"
	if ! grep -Eq "$selector_expression" "$selector_fixture"; then
		fail "portability selector scanner missed $selector_mutation"
	fi
	record_pass
done
mutated_selector_scan=$temporary_directory/main-tree-portability-selectors.mutated.txt
selector_repository=$temporary_directory/selector-repository
mkdir "$selector_repository"
git -C "$selector_repository" init -q
mkdir "$selector_repository/tests"
printf '#ifdef MIPS\n#endif\n' \
    >"$selector_repository/tests/unrelated-maintained.c"
git -C "$selector_repository" add tests/unrelated-maintained.c
scan_portability_selectors "$selector_repository" "$mutated_selector_scan"
expect_equal "unrelated-file selector fixture" \
    'tests/unrelated-maintained.c:#ifdef MIPS' \
    "$(cat "$mutated_selector_scan")"
if cmp -s "$selector_allowlist" "$mutated_selector_scan"; then
	fail "unrelated-file portability selector mutation was accepted"
fi
record_pass

# Active maintained manifests may not name a path whose source moved into a
# legacy class. Fixed-string matching is sufficient because manifest entries
# retain the source-relative suffix below /usr.
map_sources=$temporary_directory/legacy-map-sources
awk -F '\t' '$1 !~ /^#/ && $1 != "class" { print $2 }' \
    "$map_path" >"$map_sources"
for active_manifest in "$source_root/distrib/base/mi" \
    "$source_root/distrib/rp2040/mi.rp2040"
do
	if grep -F -f "$map_sources" "$active_manifest" >/dev/null; then
		grep -n -F -f "$map_sources" "$active_manifest" >&2 || :
		fail "$active_manifest names a relocated legacy source"
	fi
	record_pass
done
mutated_active_manifest=$temporary_directory/base-mi.with-legacy-source
cp "$source_root/distrib/base/mi" "$mutated_active_manifest"
printf '%s\n' 'file /usr/include/smallc/curses.h' \
    >>"$mutated_active_manifest"
if ! grep -F -f "$map_sources" "$mutated_active_manifest" >/dev/null; then
	fail "active-manifest relocated-source mutation was accepted"
fi
record_pass

# The generic base manifest cannot retain product paths whose producers left
# the maintained build. The exact set turns each removal or restoration into
# a reviewed boundary change instead of an image-builder surprise.
retired_base_manifest_count=$(wc -l <"$retired_base_manifest_paths" | tr -d ' ')
expect_equal "retired base-manifest path count" "29" \
    "$retired_base_manifest_count"
retired_base_manifest_sorted=$temporary_directory/retired-base-manifest-paths.sorted
LC_ALL=C sort -u "$retired_base_manifest_paths" >"$retired_base_manifest_sorted"
if ! cmp -s "$retired_base_manifest_paths" "$retired_base_manifest_sorted"; then
	fail "$retired_base_manifest_paths is not sorted and unique"
fi
record_pass
if grep -Fqx -f "$retired_base_manifest_paths" \
    "$source_root/distrib/base/mi"; then
	grep -Fnx -f "$retired_base_manifest_paths" \
	    "$source_root/distrib/base/mi" >&2 || :
	fail "base manifest retains a retired product path"
fi
record_pass
mutated_retired_manifest=$temporary_directory/base-mi.with-retired-product
cp "$source_root/distrib/base/mi" "$mutated_retired_manifest"
sed -n '1p' "$retired_base_manifest_paths" >>"$mutated_retired_manifest"
if ! grep -Fqx -f "$retired_base_manifest_paths" \
    "$mutated_retired_manifest"; then
	fail "retired base-manifest mutation was accepted"
fi
record_pass

# Maintained profiles and manifests contain ARM product entries only. The
# optional fragments remain complete and can enter only through the flag.
maintained_profiles=$source_root/distrib/rp2040/profiles
maintained_manifest=$source_root/distrib/rp2040/mi.rp2040
legacy_profiles=$source_root/legacy/pdp11-v6/distrib/rp2040/profiles
legacy_manifest=$source_root/legacy/pdp11-v6/distrib/rp2040/mi.rp2040
if ! verify_maintained_product "$maintained_profiles" "$maintained_manifest"; then
	grep -n -E '(^|[[:space:]])(pdp11|v6disk|retro-lab)([[:space:]]|$)|/usr/bin/pdp11|/usr/v6' \
	    "$maintained_profiles" "$maintained_manifest" >&2 || :
	fail "maintained RP2040 product files contain PDP-11/V6"
fi
record_pass
expect_equal "default manifest variant" "distrib/rp2040/_manifest.full" \
    "$("$make_command" -C "$source_root" MACHINE=rp2040 -V MI_MANIFEST)"
expect_equal "default closure inputs" "distrib/rp2040/mi.rp2040" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 -V FS_CLOSURES)")"
expect_equal "default profile inputs" "distrib/rp2040/profiles" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 -V FS_PROFILES)")"
expect_equal "default explicit closure selection" "" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 -V FS_SELECT_ARGS)")"
expect_equal "opt-in manifest variant" \
    "distrib/rp2040/_manifest.full.pdp11-v6" \
    "$("$make_command" -C "$source_root" MACHINE=rp2040 BUILD_PDP11_V6=yes -V MI_MANIFEST)"
expect_equal "opt-in closure inputs" \
    "distrib/rp2040/mi.rp2040 legacy/pdp11-v6/distrib/rp2040/mi.rp2040" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 BUILD_PDP11_V6=yes -V FS_CLOSURES)")"
expect_equal "opt-in profile inputs" \
    "distrib/rp2040/profiles legacy/pdp11-v6/distrib/rp2040/profiles" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 BUILD_PDP11_V6=yes -V FS_PROFILES)")"
expect_equal "opt-in explicit closure selection" \
    "--select-closure=pdp11 --select-closure=v6disk" \
    "$(normalize_words "$("$make_command" -C "$source_root" MACHINE=rp2040 BUILD_PDP11_V6=yes -V FS_SELECT_ARGS)")"
for required_declaration in \
    "closure pdp11 needs closure v6disk" \
    "closure pdp11 needs path /usr/bin/pdp11" \
    "closure v6disk needs path /usr/v6/root.rk"
do
	if ! grep -Fqx "$required_declaration" "$legacy_profiles"; then
		fail "$legacy_profiles misses '$required_declaration'"
	fi
	record_pass
done
for required_entry in "dir /usr/v6" "pack /usr/bin/pdp11" \
    "file /usr/v6/root.rk"
do
	if ! grep -Fqx "$required_entry" "$legacy_manifest"; then
		fail "$legacy_manifest misses '$required_entry'"
	fi
	record_pass
done
mutated_profiles=$temporary_directory/profiles.with-pdp11
mutated_manifest=$temporary_directory/mi.rp2040.with-pdp11
cp "$maintained_profiles" "$mutated_profiles"
cp "$maintained_manifest" "$mutated_manifest"
printf '%s\n' 'profile mutation pdp11 v6disk' >>"$mutated_profiles"
if verify_maintained_product "$mutated_profiles" "$maintained_manifest"; then
	fail "maintained-profile PDP-11 mutation was accepted"
fi
record_pass
printf '%s\n' 'pack /usr/bin/pdp11' >>"$mutated_manifest"
if verify_maintained_product "$maintained_profiles" "$mutated_manifest"; then
	fail "maintained-manifest PDP-11 mutation was accepted"
fi
record_pass

# The stamp publisher admits one tuple atomically, rejects malformed state,
# and accepts repeat requests only for the winning tuple.
stamp_directory=$temporary_directory/stamp-race
stamp_path=$stamp_directory/.build-machine
mkdir -p "$stamp_directory"
sh "$stamp_helper" "$stamp_path" rp2040 arm cortex-m0plus \
    >"$temporary_directory/stamp-first.log" 2>&1 &
first_stamp_pid=$!
sh "$stamp_helper" "$stamp_path" stm32 arm cortex-m4 \
    >"$temporary_directory/stamp-second.log" 2>&1 &
second_stamp_pid=$!
if wait "$first_stamp_pid"; then
	first_stamp_status=0
else
	first_stamp_status=$?
fi
if wait "$second_stamp_pid"; then
	second_stamp_status=0
else
	second_stamp_status=$?
fi
case $first_stamp_status:$second_stamp_status in
0:2)
	loser_log=$temporary_directory/stamp-second.log
	loser_machine=stm32
	loser_arch=arm
	loser_cpu=cortex-m4
	;;
2:0)
	loser_log=$temporary_directory/stamp-first.log
	loser_machine=rp2040
	loser_arch=arm
	loser_cpu=cortex-m0plus
	;;
*)
	cat "$temporary_directory/stamp-first.log" >&2
	cat "$temporary_directory/stamp-second.log" >&2
	fail "conflicting first stamp writers returned $first_stamp_status and $second_stamp_status"
	;;
esac
stamp_content=$(cat "$stamp_path")
case $stamp_content in
"MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus")
	winner_machine=rp2040
	winner_arch=arm
	winner_cpu=cortex-m0plus
	;;
"MACHINE=stm32 MACHINE_ARCH=arm MACHINE_CPU=cortex-m4")
	winner_machine=stm32
	winner_arch=arm
	winner_cpu=cortex-m4
	;;
*)
	fail "stamp race published '$stamp_content'"
	;;
esac
expect_path_present "stamp race owner diagnostic" "$loser_log" \
    "shared artifacts belong to $stamp_content"
expect_path_present "stamp race requested tuple diagnostic" "$loser_log" \
    "requested tuple is MACHINE=$loser_machine MACHINE_ARCH=$loser_arch MACHINE_CPU=$loser_cpu"
expect_path_present "stamp race recovery diagnostic" "$loser_log" \
    "run bmake MACHINE=$winner_machine cleanall before switching machines"
stamp_retry_log=$temporary_directory/stamp-retry.log
if sh "$stamp_helper" "$stamp_path" "$loser_machine" "$loser_arch" \
    "$loser_cpu" >"$stamp_retry_log" 2>&1; then
	fail "repeated losing stamp tuple was accepted"
else
	stamp_retry_status=$?
fi
expect_equal "repeated losing stamp status" "2" "$stamp_retry_status"
expect_path_present "repeated losing stamp owner diagnostic" \
    "$stamp_retry_log" "shared artifacts belong to $stamp_content"
expect_path_present "repeated losing stamp recovery diagnostic" \
    "$stamp_retry_log" \
    "run bmake MACHINE=$winner_machine cleanall before switching machines"
sh "$stamp_helper" "$stamp_path" "$winner_machine" "$winner_arch" \
    "$winner_cpu"
record_pass
unlink_test_path "$stamp_path"
printf '%s\n%s\n' \
    "MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus" \
    "MACHINE=stm32 MACHINE_ARCH=arm MACHINE_CPU=cortex-m4" >"$stamp_path"
if sh "$stamp_helper" "$stamp_path" rp2040 arm cortex-m0plus \
    >"$temporary_directory/stamp-multiline.log" 2>&1; then
	fail "multiline stamp mutation was accepted"
fi
expect_path_present "multiline stamp rejection" \
    "$temporary_directory/stamp-multiline.log" \
    "architecture stamp must contain exactly one line"
unlink_test_path "$stamp_path"
printf '%s\n' "MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus" \
    >"$stamp_directory/stamp-target"
ln -s stamp-target "$stamp_path"
if sh "$stamp_helper" "$stamp_path" rp2040 arm cortex-m0plus \
    >"$temporary_directory/stamp-symlink.log" 2>&1; then
	fail "symbolic-link stamp mutation was accepted"
fi
expect_path_present "symbolic-link stamp rejection" \
    "$temporary_directory/stamp-symlink.log" \
    "architecture stamp is not a regular file"

# Cleanup derives the only unlink target from the canonical source root and
# removes it only after every sub-clean succeeds.
cleanup_root=$temporary_directory/cleanup-root
cleanup_stamp=$cleanup_root/distrib/obj/.build-machine
arbitrary_file=$temporary_directory/arbitrary-file
mkdir -p "$cleanup_root/distrib/obj"
printf '%s\n' protected >"$arbitrary_file"
chmod 600 "$arbitrary_file"
if sh "$cleanup_helper" "$cleanup_root" "$arbitrary_file" /bin/true rp2040 \
    >"$temporary_directory/cleanup-path.log" 2>&1; then
	fail "obsolete caller-supplied cleanup stamp argument was accepted"
fi
expect_path_present "cleanup path rejection" \
    "$temporary_directory/cleanup-path.log" "make command is not executable"
expect_equal "arbitrary cleanup file preservation" "protected" \
    "$(cat "$arbitrary_file")"
printf '%s\n' "MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus" \
    >"$cleanup_stamp"
if sh "$cleanup_helper" "$cleanup_root" /bin/false rp2040 \
    >"$temporary_directory/cleanup-failure.log" 2>&1; then
	fail "failing cleanup mutation returned success"
fi
if [ ! -f "$cleanup_stamp" ]; then
	fail "failing cleanup removed the architecture stamp"
fi
record_pass
if ! sh "$cleanup_helper" "$cleanup_root" /bin/echo \
    rp2040 stm32 >"$temporary_directory/cleanup-success.log" 2>&1; then
	cat "$temporary_directory/cleanup-success.log" >&2
	fail "successful cleanup fixture failed"
fi
if [ -e "$cleanup_stamp" ] || [ -L "$cleanup_stamp" ]; then
	fail "successful cleanup retained the architecture stamp"
fi
record_pass
expect_equal "cleanall first legacy cleanup invocation" \
    "-C $cleanup_root/legacy/pdp11-v6 MACHINE=rp2040 BUILD_PDP11_V6=yes clean" \
    "$(sed -n '1p' "$temporary_directory/cleanup-success.log")"
expect_path_present "cleanall RP2040 source cleanup" \
    "$temporary_directory/cleanup-success.log" \
    "-C $cleanup_root MACHINE=rp2040 BUILD_LEGACY_NON_ARM=no BUILD_PDP11_V6=no"
expect_path_present "cleanall STM32 source cleanup" \
    "$temporary_directory/cleanup-success.log" \
    "-C $cleanup_root MACHINE=stm32 BUILD_LEGACY_NON_ARM=no BUILD_PDP11_V6=no"
cleanup_environment_make=$temporary_directory/cleanup-environment-make
cleanup_environment_log=$temporary_directory/cleanup-environment.log
printf '%s\n' '#!/bin/sh' \
    'printf "MAKEFLAGS=%s\n" "${MAKEFLAGS-}" >>"$CLEANUP_ENV_LOG"' \
    'printf "MFLAGS=%s\n" "${MFLAGS-}" >>"$CLEANUP_ENV_LOG"' \
    'printf "MACHINE=%s\n" "${MACHINE-}" >>"$CLEANUP_ENV_LOG"' \
    'printf "MACHINE_ARCH=%s\n" "${MACHINE_ARCH-}" >>"$CLEANUP_ENV_LOG"' \
    'printf "MACHINE_CPU=%s\n" "${MACHINE_CPU-}" >>"$CLEANUP_ENV_LOG"' \
    >"$cleanup_environment_make"
chmod 755 "$cleanup_environment_make"
printf '%s\n' "MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus" \
    >"$cleanup_stamp"
if ! CLEANUP_ENV_LOG="$cleanup_environment_log" \
    MAKEFLAGS=leaked-makeflags MFLAGS=leaked-mflags \
    MACHINE=leaked-machine MACHINE_ARCH=leaked-architecture \
    MACHINE_CPU=leaked-cpu \
    sh "$cleanup_helper" "$cleanup_root" "$cleanup_environment_make" \
    rp2040 stm32 >"$temporary_directory/cleanup-environment.stdout" 2>&1
then
	cat "$temporary_directory/cleanup-environment.stdout" >&2
	fail "cleanup environment fixture failed"
fi
if grep -Ev '^(MAKEFLAGS|MFLAGS|MACHINE|MACHINE_ARCH|MACHINE_CPU)=$' \
    "$cleanup_environment_log" >/dev/null
then
	cat "$cleanup_environment_log" >&2
	fail "cleanup inherited a caller machine or make override"
fi
expect_equal "cleanup environment observation count" "25" \
    "$(wc -l <"$cleanup_environment_log" | awk '{$1=$1; print}')"
if [ -e "$cleanup_stamp" ] || [ -L "$cleanup_stamp" ]; then
	fail "cleanup environment fixture retained the architecture stamp"
fi
record_pass
printf '%s\n' "MACHINE=rp2040 MACHINE_ARCH=arm MACHINE_CPU=cortex-m0plus" \
    >"$cleanup_stamp"
cleanup_link=$temporary_directory/cleanup-root-link
ln -s "$cleanup_root" "$cleanup_link"
if ! sh "$cleanup_helper" "$cleanup_link" /bin/true rp2040 \
    >"$temporary_directory/cleanup-symlink.log" 2>&1; then
	cat "$temporary_directory/cleanup-symlink.log" >&2
	fail "cleanup through a symbolic-link checkout failed"
fi
if [ -e "$cleanup_stamp" ] || [ -L "$cleanup_stamp" ]; then
	fail "cleanup through a symbolic-link checkout retained the stamp"
fi
record_pass

echo "architecture isolation verified: assertions=$pass_count machines=2 generated-kernel-makefiles=$generated_makefile_count relocation-rows=$map_row_count"
