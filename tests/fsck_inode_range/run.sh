#!/bin/sh
# Run the host fsck over images whose highest-numbered inode is in use and
# over images whose repair must survive a second run.
#
#   run.sh FSCK IMAGE_GEN WORKDIR SCENARIO...
#
# FSCK is the host-built sbin/fsck, IMAGE_GEN the generator of image_gen.c.
# A scenario passes when fsck accepts the highest inode as it accepts every
# other, or when the repair it makes leaves an image that a second preen run
# accepts (the verdict text names the observable). Exit status:
#   0  every scenario passed;
#   1  at least one scenario failed an assertion; each prints
#      "FAIL <scenario>: <reason>";
#   2  the run itself failed (usage, image generation, fsck did not execute,
#      timed out or died on a signal): no verdict on any scenario.
# check.sh runs it once over the program (every scenario must pass) and once
# per scenario per mutant (status 1 naming that scenario is the rejection).
set -eu

# error MESSAGE: stop with status 2 and no scenario verdict.
error() {
	printf 'ERROR run.sh: %s\n' "$1" >&2
	exit 2
}

if [ "$#" -lt 4 ]; then
	error "usage: run.sh FSCK IMAGE_GEN WORKDIR SCENARIO..."
fi
fsck=$1
gen=$2
work=$3
shift 3

mkdir -p "$work" || error "cannot create $work"
out=$work/fsck.out
failed=0
overall=0

# fsck on a damaged image can loop; the bound ends the run, and check_run
# reports the timeout as an execution error.
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
	# fsck exits 0, 2, 4 or 8; a timeout, a missing or unexecutable program
	# and a death by signal say nothing about the scenario.
	case $status in
	124 | 126 | 127 | 1[3-9][0-9] | 2[0-9][0-9])
		error "$scenario: fsck did not run to an exit (status $status)"
		;;
	esac
}

# Set $files to the number of files fsck reports after a clean pass: fsutil's
# count less the legacy inode 1, which pass 1 starts above.
set_expected_files() {
	report=$("$gen" --check "$img") || error "$scenario: $gen --check failed"
	count=$(printf '%s\n' "$report" |
	    sed -n 's/^\([0-9][0-9]*\) files .*/\1/p')
	[ -n "$count" ] || error "$scenario: no file count in the fsutil report"
	files=$((count - 1))
}

run_clean() {
	set_expected_files
	check_run -p
	[ "$status" -eq 0 ] || fail "preen exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode'
	has " $files files, "
}

# run_repair KIND AFTER: a repair pass answers yes to the unreferenced KIND
# (FILE or DIR) it finds, AFTER inspects the result, and a following preen run
# must find nothing left to do: second_run asserts that, expecting $1 files.
run_repair() {
	set_expected_files
	check_run -y
	[ "$status" -eq 0 ] || fail "repair exit status $status, want 0"
	has "UNREF $1"
	lacks 'SORRY|CANNOT|bad inode'
	"$2"
}

# second_run FILES: a preen run over the repaired image exits 0, reports no
# damage, finds nothing unreferenced and counts FILES files.
second_run() {
	check_run -p
	[ "$status" -eq 0 ] || fail "preen after repair exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode'
	lacks 'UNREF|BAD INODE NUMBER'
	has " $1 files, "
}

after_reconnect() {
	has 'RECONNECT?  yes'
	lacks 'NO lost\+found'
	second_run "$files"
}

after_create() {
	has 'NO lost+found DIRECTORY'
	has 'CREATE?  yes'
}

for scenario in "$@"; do
	img=$work/$scenario.img
	rm -f "$img"
	case $scenario in
	file-last | dir-last | orphan-last | free-last | lf-last | orphan-dir) ;;
	*) error "unknown scenario $scenario" ;;
	esac
	"$gen" "$scenario" "$img" || error "$scenario: image generation failed"
	case $scenario in
	file-last | dir-last) run_clean ;;
	orphan-last | lf-last) run_repair FILE after_reconnect ;;
	orphan-dir) run_repair DIR after_reconnect ;;
	free-last) run_repair FILE after_create ;;
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
