#!/bin/sh
# Report every file under SOURCE_ROOT that git does not track, ignored files
# included, unless a line of ALLOW_FILE accounts for it.  Run after cleanall:
# a build or test product that its clean target misses is listed here, where
# a later build would otherwise reuse it unseen.
#
# Each ALLOW_FILE line is "PATTERN # reason".  PATTERN is a sh case pattern
# matched against the path relative to SOURCE_ROOT, where * also matches /.
# A line without a reason is refused.
#
# usage: check-clean-residue.sh SOURCE_ROOT ALLOW_FILE
#        check-clean-residue.sh --selftest

set -eu

die()
{
	echo "check-clean-residue: $*" >&2
	exit 1
}

# check_residue ROOT ALLOW: print each unaccounted path; exit 1 if any.
check_residue()
{
	root=$1 allow=$2

	[ -f "$allow" ] || die "$allow: allow list is not a file"
	patterns=$WORK/patterns
	: > "$patterns"
	while IFS= read -r line; do
		case $line in
		'' | '#'*) continue ;;
		*' # '?*) printf '%s\n' "${line%% # *}" >> "$patterns" ;;
		*) die "allow-list entry without a reason: $line" ;;
		esac
	done < "$allow"

	git -C "$root" ls-files --others > "$WORK/others" ||
	    die "$root: git ls-files failed"
	: > "$WORK/residue"
	while IFS= read -r path; do
		matched=no
		while IFS= read -r pattern; do
			# The pattern is data from the allow list, matched as a
			# glob and never evaluated.
			# shellcheck disable=SC2254
			case $path in
			$pattern) matched=yes; break ;;
			esac
		done < "$patterns"
		[ "$matched" = yes ] || printf '%s\n' "$path" >> "$WORK/residue"
	done < "$WORK/others"

	if [ -s "$WORK/residue" ]; then
		sed 's/^/residue after cleanall: /' "$WORK/residue" >&2
		echo "check-clean-residue: $(wc -l < "$WORK/residue" | tr -d ' ') files survive cleanall" >&2
		return 1
	fi
	echo "check-clean-residue: no untracked file outside the allow list"
}

selftest()
{
	repo=$WORK/selftest
	mkdir -p "$repo/kept" "$repo/stale"
	git -C "$repo" init -q
	echo tracked > "$repo/tracked"
	printf '*.o\n' > "$repo/.gitignore"
	git -C "$repo" add tracked .gitignore
	echo cache > "$repo/kept/receipt"
	printf 'kept/* # selftest cache\n' > "$WORK/ok.allow"
	printf 'kept/*\n' > "$WORK/noreason.allow"

	check_residue "$repo" "$WORK/ok.allow" > /dev/null ||
	    die "selftest: a tree with only allowed files was rejected"
	echo product > "$repo/stale/leftover.o"
	if check_residue "$repo" "$WORK/ok.allow" > /dev/null 2>&1; then
		die "selftest: an ignored leftover was accepted"
	fi
	if (check_residue "$repo" "$WORK/noreason.allow") > /dev/null 2>&1; then
		die "selftest: an allow-list entry without a reason was accepted"
	fi
	echo "check-clean-residue selftest: allowed cache accepted, ignored leftover rejected, reasonless entry refused"
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

case ${1-} in
--selftest)
	[ "$#" -eq 1 ] || die "usage: $0 --selftest"
	selftest
	;;
*)
	[ "$#" -eq 2 ] || die "usage: $0 SOURCE_ROOT ALLOW_FILE"
	check_residue "$1" "$2"
	;;
esac
