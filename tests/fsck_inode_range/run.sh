#!/bin/sh
# Run the host fsck over images whose highest-numbered inode is in use and
# over images whose repair must survive a second run, and read the inode
# counts of the same images through the host-built bin/df.
#
#   run.sh FSCK IMAGE_GEN DF MKFS WORKDIR SCENARIO...
#
# FSCK is the host-built sbin/fsck, IMAGE_GEN the generator of image_gen.c,
# DF the host-built raw-device path of bin/df (df_driver.c) and MKFS the
# host-built sbin/mkfs. A scenario passes when fsck accepts the highest inode
# as it accepts every other, when the repair it makes leaves an image that a
# second preen run accepts, or when df reports the inode counts the kernel's
# statfs() derives from the superblock (the verdict text names the
# observable). Exit status:
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

if [ "$#" -lt 6 ]; then
	error "usage: run.sh FSCK IMAGE_GEN DF MKFS WORKDIR SCENARIO..."
fi
fsck=$1
gen=$2
df=$3
mkfs=$4
work=$5
shift 5

mkdir -p "$work" || error "cannot create $work"
out=$work/fsck.out
failed=0
overall=0

# run_bounded SECONDS COMMAND...: run the command and end it after SECONDS,
# with status 124 as timeout(1) gives. Without timeout(1), a watcher in the
# background sleeps and then ends the command by SIGTERM, leaving a marker
# file so that a command that died of its own SIGTERM is not mistaken for a
# timeout. The watcher publishes the process number of its sleep, which the
# caller ends as soon as the command has returned, so that no process outlives
# the call and the call does not wait out the limit.
run_bounded() {
	limit=$1
	shift
	if command -v timeout >/dev/null 2>&1; then
		timeout "$limit" "$@"
		return
	fi
	marker=$work/watchdog.$$.fired
	sleeper=$work/watchdog.$$.sleep
	rm -f "$marker" "$sleeper"
	"$@" &
	command_pid=$!
	(
		sleep "$limit" &
		sleep_pid=$!
		echo "$sleep_pid" >"$sleeper"
		wait "$sleep_pid" || exit 0
		: >"$marker"
		kill "$command_pid" 2>/dev/null
	) >/dev/null 2>&1 </dev/null &
	watcher_pid=$!
	bounded_status=0
	wait "$command_pid" || bounded_status=$?
	until [ -s "$sleeper" ]; do :; done
	read -r sleep_pid <"$sleeper"
	kill "$sleep_pid" 2>/dev/null || :
	wait "$watcher_pid" 2>/dev/null || :
	rm -f "$sleeper"
	if [ -e "$marker" ]; then
		rm -f "$marker"
		bounded_status=124
	fi
	return "$bounded_status"
}

# fsck on a damaged image can loop; the bound ends the run, and check_run
# reports the timeout as an execution error.
run_fsck() {
	run_bounded 60 "$fsck" "$@"
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
# damage, finds nothing unreferenced, repairs nothing (preen exits 0 after an
# (ADJUSTED), (SALVAGED), (FIXED) or similar repair, so the report is checked
# for those and the image for any changed byte) and counts FILES files.
second_run() {
	cp "$img" "$img.repaired" || error "$scenario: cannot copy $img"
	check_run -p
	[ "$status" -eq 0 ] || fail "preen after repair exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode'
	lacks 'UNREF|BAD INODE NUMBER'
	lacks 'ADJUSTED|SALVAGED|FIXED|CLEARED|RECONNECTED|CREATED|EXPANDED|IGNORED|SKIPPING'
	has " $1 files, "
	cmp -s "$img.repaired" "$img" || fail "the second preen run changed the image"
}

after_reconnect() {
	has 'RECONNECT?  yes'
	lacks 'NO lost\+found'
	second_run "$files"
}

# A second inode window holds a dirty inode while lost+found is repaired, and
# a later read of the first window evicts it: the repaired directory block
# must not be overwritten by the evicted window's stale tail.
run_tail_evict() {
	set_expected_files
	check_run -y
	[ "$status" -eq 0 ] || fail "repair exit status $status, want 0"
	has 'NOT MULTIPLE OF 1024'
	has 'UNALLOCATED'
	has 'LINK COUNT FILE'
	lacks 'SORRY|CANNOT|bad inode'
	second_run "$files"
}

# With every inode in use fsck cannot create lost+found: it reports the
# refusal, clears the orphan it could not reconnect and leaves an image the
# next preen run accepts with one file fewer.
run_decline() {
	set_expected_files
	check_run -y
	[ "$status" -eq 0 ] || fail "repair exit status $status, want 0"
	has 'UNREF FILE'
	has 'NO lost+found DIRECTORY'
	has 'SORRY. CANNOT CREATE lost+found DIRECTORY'
	has 'CLEAR?  yes'
	lacks 'bad inode'
	second_run $((files - 1))
}

# fsutil's own checker, over the image the generator wrote, reconnects the
# unreferenced file into lost+found instead of reporting that it has no
# lost+found or no space in it (the lost+found of fs_create holds "." and
# ".." alone, so the entry is cut from the record of ".."); a second fsutil
# report is then clean, and so is a preen run of the host fsck over the
# repaired image, which leaves every byte as it found it.
# lost_name sets ino to the inode fsutil reported unreferenced, name to the
# lost+found path fsutil gives it ("#" and the number in five digits) and
# found to the inode that path resolves to, empty when no entry has the name.
# A lookup that does not run to its own exit 0 or 1 is an execution error.
lost_name() {
	ino=$(sed -n 's/^UNREF FILE  *I=\([0-9][0-9]*\) .*/\1/p' "$out")
	[ -n "$ino" ] || error "$scenario: no inode number in the UNREF report"
	name=$(printf '/lost+found/#%05u' "$ino")
	status=0
	found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || status=$?
	case $status in
	0) ;;
	1) found= ;;
	*) error "$scenario: $gen --lookup did not complete, status $status" ;;
	esac
}

# A lost+found whose record runs past its block is damaged: fsutil must
# leave it alone and report that it has no room for the orphan.
run_fsutil_overrun() {
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has 'UNREF FILE'
	has 'SORRY. NO SPACE IN lost+found DIRECTORY'
	lost_name
	[ -z "$found" ] || fail "$name resolves to $found in a damaged lost+found"
}

run_fsutil() {
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has 'UNREF FILE'
	lacks 'SORRY'
	# fsutil names the reconnected file "#" and its inode number in five
	# digits; fsck accepts an entry whose name is empty, so the name itself
	# is checked through fsutil's lookup.
	lost_name
	[ "$found" = "$ino" ] ||
	    fail "$name resolves to \"$found\", want $ino"
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING'
	set_expected_files
	second_run "$files"
}

# Unreached directories whose ".." entries lead round a cycle: the climb along
# ".." in pass 3 never reaches a topmost directory, so fsck must end it. A
# repair that does not finish in ten seconds fails the scenario (check_run
# would report the timeout as an execution error, which says nothing about
# the program); the bound is generous for a 128-kilobyte image, which repairs
# in milliseconds. Every directory must then resolve under lost+found by the
# name lftempname() in sbin/fsck/dir.c gives it, a "#" and the inode number
# in as many digits as the inode count has (two for the 32-inode image), and
# the repaired image must pass a preen run.
# run_cycle COUNT closed|reached: COUNT directories are reconnected. For
# "closed" (a cycle of two, the lowest inode among them), the climb from the
# lowest stops where it closes the cycle, so the first directory reconnected
# is not the lowest. For "reached" (a chain into a cycle that does not hold
# the lowest inode, which only the imax bound ends), the lowest is reconnected
# by its own later climb: the ascending loop has passed it by then, and the
# directory the bound stopped at leads to it by no entry.
run_cycle() {
	status=0
	run_bounded 10 "$fsck" -y "$img" >"$out" 2>&1 || status=$?
	case $status in
	0) ;;
	124)
		fail "fsck -y did not finish in 10 seconds"
		return
		;;
	*) error "$scenario: fsck -y exited with status $status" ;;
	esac
	lacks 'SORRY|CANNOT|bad inode'
	listed=$(sed -n 's/^UNREF DIR  *I=\([0-9][0-9]*\) .*/\1/p' "$out")
	[ "$(printf '%s\n' "$listed" | wc -l)" -eq "$1" ] ||
	    fail "fsck reported unreferenced directories \"$listed\", want $1"
	if [ "$2" = closed ]; then
		lowest=$(printf '%s\n' "$listed" | sort -n | sed -n 1p)
		first=$(printf '%s\n' "$listed" | sed -n 1p)
		[ "$first" != "$lowest" ] ||
		    fail "fsck reconnected inode $first first, the start of the climb, not the directory that closes the cycle"
	fi
	for ino in $listed; do
		name=$(printf '/lost+found/#%02u' "$ino")
		status=0
		found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || status=$?
		[ "$status" -le 1 ] ||
		    error "$scenario: $gen --lookup did not complete, status $status"
		[ "$found" = "$ino" ] ||
		    fail "$name resolves to \"$found\", want $ino"
	done
	set_expected_files
	second_run "$files"
}

# One unreached directory whose ".." holds a number above imax. The directory
# scan takes the entry for damage and merges its slot into the record of ".",
# so pass 3 sees no parent and reconnects the directory, and pass 2 then finds
# ".." missing and must lay it out in the remainder of the merged record: the
# repaired block holds "." at its own size and ".." for the rest of the block,
# so that a following read-only run and a preen run find nothing to repair.
run_dotdot_range() {
	run_cycle 1 plain
	check_run -n
	[ "$status" -eq 0 ] || fail "check after repair exit status $status, want 0"
	lacks 'CORRUPTED|OUT OF RANGE|UNALLOCATED|INCONSISTENCY|bad inode|MISSING'
}

# Creating lost+found adds one directory to the count, and the directory
# fsck writes must pass the next directory check.
after_create() {
	has 'NO lost+found DIRECTORY'
	has 'CREATE?  yes'
	second_run $((files + 1))
}

# run_df TOTAL USED FREE: df -i over the image reports the counts the kernel's
# statfs() derives from the superblock, which "image_gen --inodes" prints:
# (isize - 1) * INOPB inodes, tinode of them free. TOTAL, USED and FREE name
# the figures the layout is built to give, so the superblock reading is
# checked against the layout as well as against df.
run_df() {
	counts=$("$gen" --inodes "$img") ||
	    error "$scenario: $gen --inodes failed"
	[ "$counts" = "total $1 used $2 free $3" ] ||
	    fail "superblock says \"$counts\", want total $1 used $2 free $3"
	"$df" "$img" >"$out" 2>&1 || error "$scenario: $df failed"
	# The data line is the second: name, blocks, used, avail, capacity,
	# iused, ifree, %iused. The name is cut to eleven characters and the
	# mount point is empty.
	row=$(sed -n 2p "$out")
	rest=
	read -r name blocks usedb availb capacity iused ifree percent rest <<EOF
$row
EOF
	{ [ -n "$percent" ] && [ -z "$rest" ]; } ||
	    error "$scenario: unexpected df report: $row"
	# ino_t is four bytes, so a negative count prints as 2^32 less.
	[ "$iused" = "$2" ] || fail "df reports $iused inodes used, want $2"
	[ "$ifree" = "$3" ] || fail "df reports $ifree inodes free, want $3"
	sum=$(awk -v a="$iused" -v b="$ifree" \
	    'BEGIN { printf "%d", (a + b) % 4294967296 }')
	[ "$sum" = "$1" ] || fail "df reports $sum inodes in all, want $1"
}

# mkfs gives up on a volume with no room for data and prints the volume size
# in blocks and the inode blocks it asked for (isize - (SUPERB+1)); a
# two-block volume at one inode per 512 bytes asks for one.
run_mkfs_ratio() {
	status=0
	"$mkfs" -i 512 "$img" 2 >"$out" 2>&1 || status=$?
	# Only a completed run counts as a verdict: 126, 127 and signal
	# statuses mean the program never ran to its own exit.
	case $status in
	0 | 1) ;;
	*) error "$scenario: $mkfs did not complete, status $status" ;;
	esac
	[ "$status" -eq 1 ] || fail "mkfs exit status $status, want 1"
	has '2/1: bad ratio'
}

for scenario in "$@"; do
	img=$work/$scenario.img
	rm -f "$img"
	case $scenario in
	file-last | dir-last | orphan-last | free-last | lf-last | orphan-dir | \
	    full-table | fsck-lf-cycle | fsck-lf-cycle-gap | fsck-lf-chain | \
	    fsck-dotdot-range | \
	    fsutil-lf-last | fsutil-lf-hole | \
	    fsutil-lf-pristine | fsutil-lf-overrun | \
	    tail-evict | df-empty | df-full | \
	    df-tail | df-mkfs | mkfs-ratio) ;;
	*) error "unknown scenario $scenario" ;;
	esac
	case $scenario in
	fsck-lf-cycle) layout=orphan-cycle ;;
	fsck-lf-cycle-gap) layout=orphan-cycle-gap ;;
	fsck-lf-chain) layout=orphan-chain-cycle ;;
	fsck-dotdot-range) layout=orphan-dotdot-range ;;
	fsutil-lf-last) layout=lf-last ;;
	fsutil-lf-hole) layout=lf-hole-last ;;
	fsutil-lf-overrun) layout=lf-overrun-last ;;
	fsutil-lf-pristine) layout=orphan-last ;;
	df-empty) layout=empty ;;
	df-full) layout=file-last ;;
	df-tail) layout=tail-evict ;;
	df-mkfs | mkfs-ratio) layout= ;;
	*) layout=$scenario ;;
	esac
	if [ -n "$layout" ]; then
		"$gen" "$layout" "$img" ||
		    error "$scenario: image generation failed"
	fi
	case $scenario in
	file-last | dir-last) run_clean ;;
	orphan-last | lf-last) run_repair FILE after_reconnect ;;
	orphan-dir) run_repair DIR after_reconnect ;;
	free-last) run_repair FILE after_create ;;
	full-table) run_decline ;;
	fsck-lf-cycle | fsck-lf-cycle-gap) run_cycle 2 closed ;;
	fsck-lf-chain) run_cycle 3 reached ;;
	fsck-dotdot-range) run_dotdot_range ;;
	fsutil-lf-last | fsutil-lf-hole | fsutil-lf-pristine) run_fsutil ;;
	fsutil-lf-overrun) run_fsutil_overrun ;;
	tail-evict) run_tail_evict ;;
	df-empty) run_df 32 3 29 ;;
	df-full) run_df 32 32 0 ;;
	df-tail) run_df 96 37 59 ;;
	df-mkfs)
		"$mkfs" -i 4096 "$img" 128 >"$out" 2>&1 ||
		    error "$scenario: $mkfs failed"
		run_df 32 3 29
		;;
	mkfs-ratio) run_mkfs_ratio ;;
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
