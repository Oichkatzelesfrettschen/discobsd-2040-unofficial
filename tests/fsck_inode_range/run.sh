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

[ -n "${PYTHON:-}" ] || error "PYTHON names no interpreter for bounded.py"
here=$(cd "$(dirname "$0")" && pwd) || error "cannot resolve the directory of $0"
mkdir -p "$work" || error "cannot create $work"
out=$work/fsck.out
failed=0
overall=0

# run_bounded SECONDS COMMAND...: run the command under bounded.py, which
# starts it as its own process group through tools/test_execution.py and stops
# the whole group on a timeout or on a SIGINT or SIGTERM of the run. The status
# is the command's own, 128 plus the signal of a command that died of one, 124
# for a timeout, as timeout(1) gives, 125 when the supervisor could not account
# for the process group, and 126 or 127 for a command that could not be
# started.
run_bounded() {
	"$PYTHON" "$here/bounded.py" "$@"
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
	# fsck exits 0, 2, 4, 8 or 12. Every status from 124 up is a timeout, a
	# timeout(1) or bounded.py failure, a missing or unexecutable program or
	# a death by signal, and says nothing about the scenario.
	if [ "$status" -ge 124 ]; then
		error "$scenario: fsck did not run to an exit (status $status)"
	fi
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
# lost_name KIND sets ino to the first inode fsutil reported unreferenced as
# KIND (FILE or DIR), name to the lost+found path fsutil gives it ("#" and the
# number in five digits) and found to the inode that path resolves to, empty
# when no entry has the name. A lookup that does not run to its own exit 0 or
# 1 is an execution error.
lost_name() {
	ino=$(sed -n "s/^UNREF $1  *I=\\([0-9][0-9]*\\) .*/\\1/p" "$out" | sed 1q)
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
	lost_name FILE
	[ -z "$found" ] || fail "$name resolves to $found in a damaged lost+found"
}

# run_fsutil KIND CHILD...: fsutil's repair reconnects the unreferenced KIND
# (FILE or DIR) under lost+found. A reconnected directory names lost+found as
# its parent, and every CHILD (a path below the reconnected name, such as
# /inner) resolves through it to the inode the layout recorded for it below
# /d00 before /d00 lost its name, so a repair that hands the name to another
# inode fails.
run_fsutil() {
	kind=$1
	shift
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has "UNREF $kind"
	lacks 'SORRY'
	# fsutil names the reconnected file "#" and its inode number in five
	# digits; fsck accepts an entry whose name is empty, so the name itself
	# is checked through fsutil's lookup.
	lost_name "$kind"
	[ "$found" = "$ino" ] ||
	    fail "$name resolves to \"$found\", want $ino"
	# A reconnected directory brings its subtree with it: the phase that
	# reconnects it walks the entries phase 2 never reached, which is what
	# gives them their references.
	reconnected=$name
	if [ "$kind" = DIR ]; then
		lost_found=$("$gen" --lookup "$img" /lost+found) ||
		    error "$scenario: no /lost+found in the repaired image"
		status=0
		parent=$("$gen" --lookup "$img" "$reconnected/.." 2>/dev/null) ||
		    status=$?
		[ "$status" -le 1 ] ||
		    error "$scenario: $gen --lookup did not complete, status $status"
		[ "$parent" = "$lost_found" ] ||
		    fail "$reconnected/.. resolves to \"$parent\", want $lost_found"
	fi
	for child in "$@"; do
		name=$reconnected$child
		status=0
		found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || status=$?
		[ "$status" -le 1 ] ||
		    error "$scenario: $gen --lookup did not complete, status $status"
		want=$(sed -n "s|^/d00$child \([0-9][0-9]*\)\$|\1|p" "$map")
		[ -n "$want" ] || error "$scenario: the layout recorded no /d00$child"
		[ "$found" = "$want" ] ||
		    fail "$name resolves to \"$found\", want $want"
	done
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING|WRONG'
	set_expected_files
	second_run "$files"
}

# inode_free: the superblock's free-inode count, which statfs() reports.
inode_free() {
	counts=$("$gen" --inodes "$img") || error "$scenario: $gen --inodes failed"
	free=$(printf '%s\n' "$counts" | sed -n 's/^total [0-9]* used [0-9]* free \([0-9][0-9]*\)$/\1/p')
	[ -n "$free" ] || error "$scenario: no free-inode count in \"$counts\""
	printf '%s\n' "$free"
}

# run_fsutil_count MESSAGE: the superblock's count disagrees with what fsutil
# traverses, and only the count; fsutil must report MESSAGE, store the
# traversed count, and accept the image on the next check.
run_fsutil_count() {
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has "$1"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING'
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING|WRONG'
	set_expected_files
	second_run "$files"
}

# run_fsutil_one_slot: two unreferenced files and room in lost+found for one.
# fsutil reconnects one, reports no space for the other and clears it: the
# reconnected file resolves under its "#" name, the cleared one does not, the
# superblock gains the one free inode, and the next reports are clean.
run_fsutil_one_slot() {
	free_before=$(inode_free)
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has 'SORRY. NO SPACE IN lost+found DIRECTORY'
	listed=$(sed -n 's/^UNREF FILE  *I=\([0-9][0-9]*\) .*/\1/p' "$out")
	[ "$(printf '%s\n' "$listed" | wc -l)" -eq 2 ] ||
	    fail "fsutil reported unreferenced files \"$listed\", want two"
	resolved=0
	for ino in $listed; do
		name=$(printf '/lost+found/#%05u' "$ino")
		status=0
		found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || status=$?
		case $status in
		0)
			[ "$found" = "$ino" ] ||
			    fail "$name resolves to \"$found\", want $ino"
			resolved=$((resolved + 1))
			;;
		1) ;;
		*) error "$scenario: $gen --lookup did not complete, status $status" ;;
		esac
	done
	[ "$resolved" -eq 1 ] ||
	    fail "$resolved of the unreferenced files resolve under lost+found, want one"
	free_after=$(inode_free)
	[ "$free_after" -eq $((free_before + 1)) ] ||
	    fail "free inodes $free_before before the repair and $free_after after, want one more"
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING|WRONG'
	set_expected_files
	second_run "$files"
}

# With no lost+found to reconnect into, fsutil clears the unreferenced file.
# The blocks it frees leave the block map, and the count phase 5 compares
# with the free list must lose them too: an image that keeps the count makes
# fsutil miss the shortfall, and the next fsck -p reports BLK(S) MISSING and
# salvages the free list (a repair marker, and a changed image).
run_fsutil_clear() {
	free_before=$(inode_free)
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has 'UNREF FILE'
	has 'SORRY. NO lost+found DIRECTORY'
	free_after=$(inode_free)
	[ "$free_after" -eq $((free_before + 1)) ] ||
	    fail "free inodes $free_before before the repair and $free_after after, want one more"
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|BAD|DUP|LINK COUNT|MISSING|WRONG'
	set_expected_files
	second_run "$files"
}

# run_fsutil_cycle COUNT: COUNT unreached directories, two or more of them
# with ".." entries that name each other, lead the climb along ".." around a
# loop with no topmost directory, so fsutil's repair must end the climb and
# still reconnect every one of them. A repair that does not finish in the time
# allowed fails the scenario; the limit is generous for a 128-kilobyte image,
# which repairs in milliseconds. Every directory must then resolve under
# lost+found.
run_fsutil_cycle() {
	status=0
	run_bounded 10 "$gen" --fix "$img" >"$out" 2>&1 || status=$?
	case $status in
	0) ;;
	124)
		fail "$gen --fix did not finish in 10 seconds"
		return
		;;
	*) error "$scenario: $gen --fix failed, status $status" ;;
	esac
	lacks 'SORRY'
	listed=$(sed -n 's/^UNREF DIR  *I=\([0-9][0-9]*\) .*/\1/p' "$out")
	[ "$(printf '%s\n' "$listed" | wc -l)" -eq "$1" ] ||
	    fail "fsutil reported unreferenced directories \"$listed\", want $1"
	for ino in $listed; do
		name=$(printf '/lost+found/#%05u' "$ino")
		status=0
		found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || status=$?
		[ "$status" -le 1 ] ||
		    error "$scenario: $gen --lookup did not complete, status $status"
		[ "$found" = "$ino" ] ||
		    fail "$name resolves to \"$found\", want $ino"
	done
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING|WRONG'
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
	map=$work/$scenario.map
	rm -f "$img" "$map"
	case $scenario in
	file-last | dir-last | orphan-last | free-last | lf-last | orphan-dir | \
	    full-table | fsck-lf-cycle | fsck-lf-cycle-gap | fsck-lf-chain | \
	    fsck-dotdot-range | \
	    fsutil-lf-last | fsutil-lf-hole | fsutil-lf-pristine | \
	    fsutil-lf-overrun | fsutil-lf-dir | fsutil-lf-deep | fsutil-lf-cycle | fsutil-lf-gap | fsutil-lf-chain | \
	    fsutil-clear-blocks | fsutil-count-tfree | fsutil-count-tinode | \
	    fsutil-lf-second | fsutil-lf-one-slot | \
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
	fsutil-lf-dir) layout=orphan-dir ;;
	fsutil-lf-deep) layout=orphan-deep ;;
	fsutil-lf-cycle) layout=orphan-cycle ;;
	fsutil-lf-gap) layout=orphan-cycle-gap ;;
	fsutil-lf-chain) layout=orphan-chain-cycle ;;
	fsutil-clear-blocks) layout=orphan-nolf ;;
	fsutil-count-tfree) layout=count-tfree ;;
	fsutil-lf-second) layout=lf-second-block ;;
	fsutil-lf-one-slot) layout=lf-one-slot ;;
	fsutil-count-tinode) layout=count-tinode ;;
	df-empty) layout=empty ;;
	df-full) layout=file-last ;;
	df-tail) layout=tail-evict ;;
	df-mkfs | mkfs-ratio) layout= ;;
	*) layout=$scenario ;;
	esac
	if [ -n "$layout" ]; then
		"$gen" "$layout" "$img" >"$map" ||
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
	fsutil-lf-last | fsutil-lf-hole | fsutil-lf-pristine) run_fsutil FILE ;;
	fsutil-lf-dir) run_fsutil DIR /inner ;;
	fsutil-lf-deep) run_fsutil DIR /sub /sub/inner ;;
	fsutil-lf-cycle | fsutil-lf-gap) run_fsutil_cycle 2 ;;
	fsutil-lf-chain) run_fsutil_cycle 3 ;;
	fsutil-clear-blocks) run_fsutil_clear ;;
	fsutil-lf-second) run_fsutil FILE ;;
	fsutil-lf-one-slot) run_fsutil_one_slot ;;
	fsutil-count-tfree) run_fsutil_count 'FREE BLK COUNT WRONG IN SUPERBLOCK' ;;
	fsutil-count-tinode) run_fsutil_count 'FREE INODE COUNT WRONG IN SUPERBLOCK' ;;
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
