#!/bin/sh
# Decide the fsck inode range gate: the program passes every scenario and
# each mutant is rejected by the scenarios named for it. A mutant named
# fsutil-* rewrites tools/fsutil and is run through its own image generator;
# df-* rewrites bin/df and mkfs-* rewrites sbin/mkfs, each run as its own
# program.
#
#   check.sh RUN_SH FSCK IMAGE_GEN DF MKFS WORKROOT MUTANT_DIR MUTANT=SCENARIO[,SCENARIO]...
#
# A mutant counts as rejected for a scenario only when run.sh exits 1 and
# prints that scenario's own "FAIL <scenario>:" verdict. Exit 0 from run.sh
# means the mutant passes the scenario, and exit 2 means the run itself broke
# (image generation, a mutant that does not execute, a timeout), which says
# nothing about the mutant; both stop the gate. This script exits 0 when
# every decision holds, 1 when the program fails a scenario or a mutant
# escapes, and 2 when a run broke (the status test_execution.py reads as an
# error rather than a failure).
set -eu

if [ "$#" -lt 8 ]; then
	echo "ERROR check.sh: usage: check.sh RUN_SH FSCK IMAGE_GEN DF MKFS WORKROOT MUTANT_DIR MUTANT=SCENARIOS..." >&2
	exit 2
fi
run_sh=$1
fsck=$2
gen=$3
df=$4
mkfs=$5
workroot=$6
mutant_dir=$7
shift 7

all_scenarios='file-last dir-last orphan-last free-last lf-last orphan-dir full-table fsutil-lf-last fsutil-lf-hole fsutil-lf-pristine fsutil-lf-overrun tail-evict df-empty df-full df-tail df-mkfs mkfs-ratio'
log=$workroot/check.log
mkdir -p "$workroot" || exit 2

# shellcheck disable=SC2086  # the scenario list is a fixed word list
if sh "$run_sh" "$fsck" "$gen" "$df" "$mkfs" "$workroot/program" \
    $all_scenarios; then
	:
else
	status=$?
	echo "FAIL program: run.sh exited $status over the unmutated fsck" >&2
	[ "$status" -eq 1 ] || exit 2
	exit 1
fi

escaped=0
for spec in "$@"; do
	mutant=${spec%%=*}
	scenarios=$(echo "${spec#*=}" | tr ',' ' ')
	# A mutant of tools/fsutil is run with the unmutated fsck and its own
	# image generator, and one of bin/df or sbin/mkfs with its own program;
	# a mutant of sbin/fsck is run with the shared generator.
	mutant_fsck=$fsck
	mutant_gen=$gen
	mutant_df=$df
	mutant_mkfs=$mkfs
	case $mutant in
	fsutil-*) mutant_gen=$mutant_dir/image_gen_mutant_$mutant ;;
	df-*) mutant_df=$mutant_dir/df_mutant_$mutant ;;
	mkfs-*) mutant_mkfs=$mutant_dir/mkfs_mutant_$mutant ;;
	*) mutant_fsck=$mutant_dir/fsck_mutant_$mutant ;;
	esac
	for scenario in $scenarios; do
		status=0
		sh "$run_sh" "$mutant_fsck" "$mutant_gen" "$mutant_df" \
		    "$mutant_mkfs" "$workroot/$mutant-$scenario" "$scenario" \
		    >"$log" 2>&1 ||
		    status=$?
		case $status in
		1)
			if grep -q "^FAIL $scenario:" "$log"; then
				echo "mutant $mutant fails $scenario"
			else
				echo "mutant $mutant: run.sh exited 1 without a verdict for $scenario" >&2
				escaped=1
			fi
			;;
		0)
			echo "mutant $mutant passes $scenario" >&2
			escaped=1
			;;
		*)
			echo "ERROR mutant $mutant, scenario $scenario: run.sh exited $status" >&2
			sed 's/^/    | /' "$log" >&2
			exit 2
			;;
		esac
	done
done
exit "$escaped"
