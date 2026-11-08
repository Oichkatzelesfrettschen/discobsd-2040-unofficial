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
run_fsutil() {
	"$gen" --fix "$img" >"$out" 2>&1 || error "$scenario: $gen --fix failed"
	has 'UNREF FILE'
	lacks 'SORRY'
	# fsutil names the reconnected file "#" and its inode number in five
	# digits; fsck accepts an entry whose name is empty, so the name itself
	# is checked through fsutil's lookup.
	ino=$(sed -n 's/^UNREF FILE  *I=\([0-9][0-9]*\) .*/\1/p' "$out")
	[ -n "$ino" ] || error "$scenario: no inode number in the UNREF report"
	name=$(printf '/lost+found/#%05u' "$ino")
	found=$("$gen" --lookup "$img" "$name" 2>/dev/null) || found=
	[ "$found" = "$ino" ] ||
	    fail "$name resolves to \"$found\", want $ino"
	"$gen" --check "$img" >"$out" 2>&1 || error "$scenario: $gen --check failed"
	lacks 'UNREF|SORRY|BAD|DUP|LINK COUNT|MISSING'
	set_expected_files
	second_run "$files"
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
	    full-table | fsutil-lf-last | fsutil-lf-hole | fsutil-lf-pristine | \
	    tail-evict | df-empty | df-full | \
	    df-tail | df-mkfs | mkfs-ratio) ;;
	*) error "unknown scenario $scenario" ;;
	esac
	case $scenario in
	fsutil-lf-last) layout=lf-last ;;
	fsutil-lf-hole) layout=lf-hole-last ;;
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
	fsutil-lf-last | fsutil-lf-hole | fsutil-lf-pristine) run_fsutil ;;
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
