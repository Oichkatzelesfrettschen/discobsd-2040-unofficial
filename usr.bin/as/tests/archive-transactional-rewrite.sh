#!/bin/sh
#
# Verify that archive rewrite failures preserve the original pathname bytes.
#
set -eu

AR=${AR:?set AR to the tree's archive tool}
HOST_CC=${HOST_CC:?set HOST_CC to the host C compiler}
RANLIB=${RANLIB:?set RANLIB to the tree's archive index tool}
SOURCE_ROOT=${SOURCE_ROOT:?set SOURCE_ROOT to this test directory}
work_root=$(cd "${WORK:-.}" && pwd)
test_root=$work_root/archive-transactional-rewrite
fault_library=$test_root/archive-transaction-failure.so

rm -rf "$test_root"
mkdir -p "$test_root"
trap 'rm -rf "$test_root"' 0 1 2 3 15

"$HOST_CC" -shared -fPIC -Wall -Wextra -Werror \
	-o "$fault_library" "$SOURCE_ROOT/archive-transaction-failure.c" -ldl

assert_no_rewrite_temp()
{
	archive_directory=$1
	set -- "$archive_directory"/.ar.*
	if [ -e "$1" ]; then
		echo "archive: uncommitted replacement remains at $1" >&2
		exit 1
	fi
}

check_failed_existing_rewrite()
{
	case_name=$1
	operation=$2
	case_root=$test_root/$case_name
	archive=$case_root/libtransaction.a

	mkdir -p "$case_root"
	printf 'first' > "$case_root/first.bin"
	printf 'second' > "$case_root/second.bin"
	printf 'third' > "$case_root/third.bin"
	(
		cd "$case_root"
		"$AR" rc "$archive" first.bin second.bin
		if [ "$operation" = ranlib-touch ]; then
			"$RANLIB" "$archive"
		fi
		cp "$archive" archive.before
		case "$operation" in
		ar-delete) set -- "$AR" d "$archive" first.bin ;;
		ar-move) set -- "$AR" m "$archive" first.bin ;;
		ar-quick-append) set -- "$AR" q "$archive" third.bin ;;
		ar-replace)
			printf 'replacement' > first.bin
			set -- "$AR" r "$archive" first.bin
			;;
		ranlib-build) set -- "$RANLIB" "$archive" ;;
		ranlib-touch) set -- "$RANLIB" -t "$archive" ;;
		*) echo "archive: unknown operation $operation" >&2; exit 1 ;;
		esac
		if ARCHIVE_FAIL_OPERATION=rename LD_PRELOAD="$fault_library" \
		    "$@" > command.out 2> command.err
		then
			echo "archive: $case_name accepted a failed rename" >&2
			exit 1
		fi
		cmp -s archive.before "$archive"
		grep -q 'Input/output error' command.err
	)
	assert_no_rewrite_temp "$case_root"
}

for operation in ar-delete ar-move ar-quick-append ar-replace \
    ranlib-build ranlib-touch
do
	check_failed_existing_rewrite "$operation" "$operation"
done

check_precommit_failure()
{
	case_name=$1
	failure=$2
	case_root=$test_root/$case_name
	archive=$case_root/libtransaction.a

	mkdir -p "$case_root"
	printf 'first' > "$case_root/first.bin"
	printf 'second' > "$case_root/second.bin"
	printf 'third' > "$case_root/third.bin"
	(
		cd "$case_root"
		"$AR" rc "$archive" first.bin second.bin
		cp "$archive" archive.before
		if ARCHIVE_FAIL_OPERATION="$failure" LD_PRELOAD="$fault_library" \
		    "$AR" q "$archive" third.bin > command.out 2> command.err
		then
			echo "archive: $case_name accepted a failed $failure" >&2
			exit 1
		fi
		cmp -s archive.before "$archive"
		grep -q 'Input/output error' command.err
	)
	assert_no_rewrite_temp "$case_root"
}

check_precommit_failure file-fsync file-fsync
check_precommit_failure directory-fsync directory-fsync

unowned_candidate_root=$test_root/unowned-candidate
mkdir -p "$unowned_candidate_root"
printf 'first' > "$unowned_candidate_root/first.bin"
(
	cd "$unowned_candidate_root"
	"$AR" rc libtransaction.a first.bin
	printf 'unrelated sentinel' > .ar.XXXXXX
	cp .ar.XXXXXX sentinel.before
	if ARCHIVE_FAIL_OPERATION=directory-fsync LD_PRELOAD="$fault_library" \
	    "$AR" q libtransaction.a first.bin > command.out 2> command.err
	then
		echo 'archive: preflight directory sync failure returned success' >&2
		exit 1
	fi
	cmp -s sentinel.before .ar.XXXXXX
	grep -q 'Input/output error' command.err
)

new_failure_root=$test_root/new-link-failure
mkdir -p "$new_failure_root"
printf 'first' > "$new_failure_root/first.bin"
(
	cd "$new_failure_root"
	if ARCHIVE_FAIL_OPERATION=link LD_PRELOAD="$fault_library" \
	    "$AR" rc libtransaction.a first.bin > command.out 2> command.err
	then
		echo 'archive: new archive accepted a failed link' >&2
		exit 1
	fi
	if [ -e libtransaction.a ]; then
		echo 'archive: failed creation exposed an archive pathname' >&2
		exit 1
	fi
	grep -q 'Input/output error' command.err
)
assert_no_rewrite_temp "$new_failure_root"

post_sync_root=$test_root/post-commit-directory-fsync
mkdir -p "$post_sync_root"
printf 'first' > "$post_sync_root/first.bin"
printf 'second' > "$post_sync_root/second.bin"
(
	cd "$post_sync_root"
	"$AR" rc libtransaction.a first.bin
	if ARCHIVE_FAIL_OPERATION=post-commit-directory-fsync \
	    LD_PRELOAD="$fault_library" "$AR" q libtransaction.a second.bin \
	    > command.out 2> command.err
	then
		echo 'archive: post-commit directory sync failure returned success' >&2
		exit 1
	fi
	printf '%s\n%s\n' first.bin second.bin > members.expected
	"$AR" t libtransaction.a > members.actual
	diff -u members.expected members.actual
	grep -q 'Input/output error' command.err
)
assert_no_rewrite_temp "$post_sync_root"

check_semantic_failure()
{
	case_name=$1
	operation=$2
	case_root=$test_root/$case_name
	archive=$case_root/libtransaction.a

	mkdir -p "$case_root"
	printf 'first' > "$case_root/first.bin"
	printf 'second' > "$case_root/second.bin"
	printf 'third' > "$case_root/third.bin"
	(
		cd "$case_root"
		"$AR" rc "$archive" first.bin second.bin
		cp "$archive" archive.before
		case "$operation" in
		delete) set -- "$AR" d "$archive" first.bin missing.bin ;;
		move) set -- "$AR" m "$archive" first.bin missing.bin ;;
		quick-append) set -- "$AR" q "$archive" third.bin missing.bin ;;
		replace)
			printf 'replacement' > first.bin
			set -- "$AR" r "$archive" first.bin missing.bin
			;;
		*) echo "archive: unknown operation $operation" >&2; exit 1 ;;
		esac
		if "$@" > command.out 2> command.err; then
			echo "archive: $case_name accepted a missing input or member" >&2
			exit 1
		fi
		cmp -s archive.before "$archive"
	)
	assert_no_rewrite_temp "$case_root"
}

for operation in delete move quick-append replace
do
	check_semantic_failure "semantic-$operation" "$operation"
done

missing_new_root=$test_root/missing-new-input
mkdir -p "$missing_new_root"
(
	cd "$missing_new_root"
	if "$AR" rc libtransaction.a missing.bin > command.out 2> command.err; then
		echo 'archive: missing input created an archive' >&2
		exit 1
	fi
	if [ -e libtransaction.a ]; then
		echo 'archive: missing input exposed a new archive pathname' >&2
		exit 1
	fi
)
assert_no_rewrite_temp "$missing_new_root"

hardlink_root=$test_root/hardlink
mkdir -p "$hardlink_root"
printf 'first' > "$hardlink_root/first.bin"
printf 'second' > "$hardlink_root/second.bin"
(
	cd "$hardlink_root"
	"$AR" rc canonical.a first.bin
	ln canonical.a alias.a
	cp canonical.a archive.before
	if "$AR" q alias.a second.bin > command.out 2> command.err; then
		echo 'archive: hard-linked archive rewrite returned success' >&2
		exit 1
	fi
	cmp -s archive.before canonical.a
	cmp -s archive.before alias.a
	grep -q 'Too many links' command.err
)
assert_no_rewrite_temp "$hardlink_root"

symlink_root=$test_root/symlink
mkdir -p "$symlink_root"
printf 'first' > "$symlink_root/first.bin"
printf 'second' > "$symlink_root/second.bin"
(
	cd "$symlink_root"
	"$AR" rc canonical.a first.bin
	ln -s canonical.a alias.a
	cp canonical.a archive.before
	if "$AR" q alias.a second.bin > command.out 2> command.err; then
		echo 'archive: symbolic-link archive rewrite returned success' >&2
		exit 1
	fi
	test -L alias.a
	cmp -s archive.before canonical.a
	grep -q 'Too many levels of symbolic links' command.err
)
assert_no_rewrite_temp "$symlink_root"

success_root=$test_root/success
success_archive=$success_root/libtransaction.a
mkdir -p "$success_root"
printf 'first' > "$success_root/first.bin"
printf 'second' > "$success_root/second.bin"
printf 'third' > "$success_root/third.bin"
(
	cd "$success_root"
	"$AR" rc "$success_archive" first.bin second.bin third.bin
	printf 'fourth' > fourth.bin
	"$AR" q "$success_archive" fourth.bin
	"$AR" m "$success_archive" first.bin
	"$AR" d "$success_archive" second.bin
	printf 'replacement' > first.bin
	"$AR" r "$success_archive" first.bin
	"$RANLIB" "$success_archive"
	"$RANLIB" -t "$success_archive"
	"$AR" t "$success_archive" > members.actual
	printf '%s\n%s\n%s\n%s\n' __.SYMDEF third.bin fourth.bin first.bin \
	    > members.expected
	diff -u members.expected members.actual
	"$AR" p "$success_archive" first.bin > payload.actual
	cmp -s first.bin payload.actual
)
assert_no_rewrite_temp "$success_root"

echo "archive: transactions preserve pre-commit state and publish complete results"
