#!/bin/sh
# Run the host fsck over images whose highest-numbered inode is in use.
#
#   run.sh FSCK IMAGE_GEN WORKDIR SCENARIO...
#
# FSCK is the host-built sbin/fsck, IMAGE_GEN the generator of image_gen.c.
# A scenario passes when fsck accepts the highest inode as it accepts every
# other (the verdict text names the observable); a failing scenario prints
# "FAIL <scenario>: <reason>" and the script exits 1. The Makefile runs it
# once over the program (every scenario must pass) and once per mutant (the
# scenarios named for that mutant must fail).
set -eu

if [ "$#" -lt 4 ]; then
	echo "usage: run.sh FSCK IMAGE_GEN WORKDIR SCENARIO..." >&2
	exit 2
fi
fsck=$1
gen=$2
work=$3
shift 3

mkdir -p "$work"
out=$work/fsck.out
failed=0
overall=0

# fsck on a damaged image can loop; a bound turns that into a verdict.
run_fsck() {
	if command -v timeout >/dev/null 2>&1; then
		timeout 60 "$fsck" "$@"
	else
		"$fsck" "$@"
	fi
}

# lacks PATTERN: the report must not match the extended regular expression.
lacks() {
	if grep -E -q -- "$1" "$out"; then
		fail "report matches /$1/"
	fi
}

# has PATTERN: the report must match the fixed string.
has() {
	if ! grep -F -q -- "$1" "$out"; then
		fail "report lacks \"$1\""
	fi
}

fail() {
	printf 'FAIL %s: %s\n' "$scenario" "$1"
	failed=1
}

# check_run OPTION... runs the program with the options on the image and
# leaves its report in $out and its status in $status.
check_run() {
	status=0
	run_fsck "$@" "$img" >"$out" 2>&1 || status=$?
}

# The number of files fsck reports after a clean pass equals fsutil's count
# less the legacy inode 1, which pass 1 starts above.
fsutil_files() {
	"$gen" --check "$img" | sed -n 's/^\([0-9][0-9]*\) files .*/\1/p'
}

run_clean() {
	files=$(($(fsutil_files) - 1))
	check_run -p
	[ "$status" -eq 0 ] || fail "preen exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode'
	has " $files files, "
}

# A repair pass answers yes, and the image must then pass a preen run.
run_repair() {
	files=$(($(fsutil_files) - 1))
	check_run -y
	[ "$status" -eq 0 ] || fail "repair exit status $status, want 0"
	has 'UNREF FILE'
	lacks 'SORRY|CANNOT|bad inode'
	"$1"
}

after_reconnect() {
	has 'RECONNECT?  yes'
	lacks 'NO lost\+found'
	check_run -p
	[ "$status" -eq 0 ] || fail "preen after repair exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode'
	has " $files files, "
}

after_create() {
	has 'NO lost+found DIRECTORY'
	has 'CREATE?  yes'
}

for scenario in "$@"; do
	img=$work/$scenario.img
	rm -f "$img"
	"$gen" "$scenario" "$img"
	case $scenario in
	file-last | dir-last) run_clean ;;
	orphan-last | lf-last) run_repair after_reconnect ;;
	free-last) run_repair after_create ;;
	*)
		echo "run.sh: unknown scenario $scenario" >&2
		exit 2
		;;
	esac
	if [ "$failed" -eq 0 ]; then
		printf 'ok %s\n' "$scenario"
	else
		sed 's/^/    | /' "$out"
		overall=1
		failed=0
	fi
done
exit "$overall"
