#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Hold the build number to its value outside a git checkout.
#
# share/mk/sys.mk counts the commits of HEAD into BUILD, and
# sys/conf/newvers.sh stamps the same count into the kernel version string.
# Both take the count only from a repository rooted at the source tree. A
# source export carries none, so the number is 0, wherever the export is
# unpacked. This gate copies the build fragments and newvers.sh from
# SOURCE_ROOT into two private placements:
#   plain   a directory where no repository answers;
#   nested  a directory beneath an unrelated repository of one commit.
# In each it requires:
#   - make to expand VERSION to RELEASE-0 with nothing on standard error;
#   - newvers.sh to write a version string whose revision field is 0, again
#     with nothing on standard error.
# Both run the copied files, so the verdict covers the working tree.
#
# Only the absence of a repository selects 0. In a third placement, broken,
# the tree's .git is an empty file, a repository that is present and cannot
# be counted: there make has to leave the number empty and pass git's
# diagnostic on, and newvers.sh has to fail with it, so a damaged checkout
# never reads as an export.
#
# Two controls rewrite the count commands in further copies and require the
# same checks to reject them: the unguarded command in the plain placement
# (an empty number and the git diagnostic from each reader), and a command
# that silences git and falls back to 0 without binding the repository to
# the source tree, in the nested placement (the outer repository's count,
# 1, in each reader).
#
# usage: check_version_outside_git.sh SOURCE_ROOT [MAKE]

set -eu

die()
{
	echo "check-version-outside-git: $*" >&2
	exit 1
}

[ $# -ge 1 ] || die "usage: check_version_outside_git.sh SOURCE_ROOT [MAKE]"
root=$1
make=${2:-bmake}
[ -f "$root/share/mk/sys.mk" ] || die "$root: no share/mk/sys.mk"
[ -f "$root/sys/conf/newvers.sh" ] || die "$root: no sys/conf/newvers.sh"
command -v git >/dev/null 2>&1 || die "not run: git is required for the nested placement"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/version-outside-git.XXXXXX")
cleanup()
{
	rm -rf -- "$WORK"
}
trap cleanup EXIT
trap 'cleanup; trap - EXIT; exit 129' HUP
trap 'cleanup; trap - EXIT; exit 130' INT
trap 'cleanup; trap - EXIT; exit 143' TERM

# No repository above the private directory may answer; the nested
# placement's own outer repository lies below this ceiling and does.
GIT_CEILING_DIRECTORIES=$WORK
export GIT_CEILING_DIRECTORIES
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE

# stage PLACEMENT NAME: copy the fragments the two readers need into a tree
# named NAME and print its path.
stage()
{
	case $1 in
	plain)
		tree=$WORK/$2
		;;
	nested)
		outer=$WORK/$2.outer
		mkdir -p "$outer"
		git -C "$outer" init -q
		git -C "$outer" -c user.name=gate -c user.email=gate@invalid \
		    -c commit.gpgsign=false commit -q --allow-empty -m outer
		[ "$(git -C "$outer" rev-list --count HEAD --)" = 1 ] ||
		    die "$outer: the outer repository does not hold one commit"
		tree=$outer/export
		;;
	*)	die "unknown placement $1" ;;
	esac
	mkdir -p "$tree/share" "$tree/lib" "$tree/sys/conf" "$tree/kernel"
	cp -R "$root/share/mk" "$tree/share/mk"
	cp "$root/lib/Makefile" "$tree/lib/Makefile"
	cp "$root/sys/conf/newvers.sh" "$tree/sys/conf/newvers.sh"
	if [ "$1" = plain ] &&
	    (cd "$tree" && git rev-parse --git-dir >/dev/null 2>&1); then
		die "$tree: a git repository still answers here"
	fi
	printf '%s\n' "$tree"
}

# verdict TREE: print each defect of the staged tree; return 1 if any.
verdict()
{
	tree=$1
	defects=0

	if ! (cd "$tree/lib" && "$make" MACHINE=rp2040 TOPSRC="$tree" \
	    -V '${VERSION}') > "$tree/version.out" 2> "$tree/version.err"; then
		echo "make failed while expanding VERSION"
		defects=1
	fi
	version=$(cat "$tree/version.out")
	case $version in
	*[0-9].[0-9]*-0) ;;
	*)	echo "VERSION is '$version', expected RELEASE-0"
		defects=1 ;;
	esac
	if [ -s "$tree/version.err" ]; then
		echo "make wrote to standard error: $(head -n 1 "$tree/version.err")"
		defects=1
	fi

	if ! (cd "$tree/kernel" && SOURCE_DATE_EPOCH=0 \
	    sh "$tree/sys/conf/newvers.sh") > "$tree/vers.c" 2> "$tree/vers.err"; then
		echo "newvers.sh failed"
		defects=1
	fi
	if ! grep -q '^const char version\[\] = ".* #1 0: ' "$tree/vers.c"; then
		echo "newvers.sh revision field is not 0: $(head -n 1 "$tree/vers.c")"
		defects=1
	fi
	if [ -s "$tree/vers.err" ]; then
		echo "newvers.sh wrote to standard error: $(head -n 1 "$tree/vers.err")"
		defects=1
	fi
	return $defects
}

# rewrite TREE SYSMK_COMMAND NEWVERS_COMMAND: replace both count commands.
# The sys.mk assignment spans two lines; the continuation is dropped.
rewrite()
{
	awk -v command="$2" '
	    /^BUILD!=/ { print "BUILD!=\t\t" command; skip = /\\$/; next }
	    skip { skip = /\\$/; next }
	    { print }' "$root/share/mk/sys.mk" > "$1/share/mk/sys.mk"
	awk -v command="$3" '
	    /^TOP=/ { print "GITREV=" command; next }
	    /^if \[ -e "\$TOP\/\.git" \]$/ { drop = 1; next }
	    drop { if ($0 == "fi") drop = 0; next }
	    { print }' "$root/sys/conf/newvers.sh" > "$1/sys/conf/newvers.sh"
	grep -q "^BUILD!=.*$4" "$1/share/mk/sys.mk" ||
	    die "the control did not rewrite sys.mk"
	grep -q "^GITREV=.*$4" "$1/sys/conf/newvers.sh" ||
	    die "the control did not rewrite newvers.sh"
	if grep -q '[$]TOP' "$1/sys/conf/newvers.sh"; then
		die "the control left the bounded lookup in newvers.sh"
	fi
}

for placement in plain nested; do
	tree=$(stage "$placement" tree."$placement")
	verdict "$tree" ||
	    die "$placement placement: the build number is not 0 outside a git checkout"
	echo "PASS version-outside-git $placement: VERSION $(cat "$tree/version.out"), kernel revision 0"
done

# A repository that is present and cannot be counted is not an export.
tree=$(stage plain tree.broken)
: > "$tree/.git"
(cd "$tree/lib" && "$make" MACHINE=rp2040 TOPSRC="$tree" -V '${VERSION}') \
    > "$tree/version.out" 2> "$tree/version.err" || :
case $(cat "$tree/version.out") in
*-0)	die "broken placement: make reported build number 0 for an uncountable repository" ;;
esac
[ -s "$tree/version.err" ] ||
    die "broken placement: make passed no diagnostic on"
if (cd "$tree/kernel" && SOURCE_DATE_EPOCH=0 sh "$tree/sys/conf/newvers.sh") \
    > "$tree/vers.c" 2> "$tree/vers.err"; then
	die "broken placement: newvers.sh succeeded on an uncountable repository"
fi
[ -s "$tree/vers.err" ] ||
    die "broken placement: newvers.sh failed without a diagnostic"
echo "PASS version-outside-git broken: an uncountable repository is reported, not counted as 0"

# Control: the unguarded command where no repository answers.
tree=$(stage plain control.unguarded)
rewrite "$tree" 'git rev-list --count HEAD --' \
    '`git rev-list --count HEAD --`' 'git rev-list --count HEAD --'
if verdict "$tree" > "$WORK/unguarded.out"; then
	die "the unguarded control was accepted"
fi
count=$(wc -l < "$WORK/unguarded.out" | tr -d ' ')
[ "$count" -ge 3 ] ||
    die "the unguarded control raised $count defects, expected at least 3"
echo "PASS version-outside-git control: the unguarded count is rejected ($count defects)"

# Control: a silenced command with a fallback that still searches parents.
tree=$(stage nested control.unbounded)
rewrite "$tree" 'git rev-list --count HEAD -- 2>/dev/null || echo 0' \
    '`git rev-list --count HEAD -- 2>/dev/null` || GITREV="0"' '2>/dev/null'
if verdict "$tree" > "$WORK/unbounded.out"; then
	die "the unbounded control was accepted"
fi
count=$(wc -l < "$WORK/unbounded.out" | tr -d ' ')
[ "$count" -eq 2 ] ||
    die "the unbounded control raised $count defects, expected 2"
grep -q "VERSION is '.*-1'" "$WORK/unbounded.out" ||
    die "the unbounded control did not report the outer repository's count"
echo "PASS version-outside-git control: a parent repository's count is rejected ($count defects)"
