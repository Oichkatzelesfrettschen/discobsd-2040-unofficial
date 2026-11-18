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
# A source export carries no repository, so git exits 128 there. This gate
# copies the build fragments and newvers.sh from SOURCE_ROOT into a private
# directory that no repository contains, then requires:
#   - make to expand VERSION to RELEASE-0 with nothing on standard error
#     (an unguarded count leaves "RELEASE-" and one warning per Makefile);
#   - newvers.sh to write a version string whose revision field is 0, again
#     with nothing on standard error.
# Both forms run the copied files, so the verdict covers the working tree.
# The control rewrites the two count commands to their unguarded form in a
# second copy and requires the same checks to reject it.
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

WORK=$(mktemp -d "${TMPDIR:-/tmp}/version-outside-git.XXXXXX")
cleanup()
{
	rm -rf -- "$WORK"
}
trap cleanup EXIT
trap 'cleanup; trap - EXIT; exit 129' HUP
trap 'cleanup; trap - EXIT; exit 130' INT
trap 'cleanup; trap - EXIT; exit 143' TERM

# No repository may answer from inside the private directory.
GIT_CEILING_DIRECTORIES=$WORK
export GIT_CEILING_DIRECTORIES
unset GIT_DIR GIT_WORK_TREE

# stage NAME: copy the fragments the two readers need into $WORK/NAME.
stage()
{
	tree=$WORK/$1
	mkdir -p "$tree/share" "$tree/lib" "$tree/sys/conf" "$tree/kernel"
	cp -R "$root/share/mk" "$tree/share/mk"
	cp "$root/lib/Makefile" "$tree/lib/Makefile"
	cp "$root/sys/conf/newvers.sh" "$tree/sys/conf/newvers.sh"
	if (cd "$tree" && git rev-parse --git-dir >/dev/null 2>&1); then
		die "$tree: a git repository still answers here"
	fi
}

# verdict NAME: print each defect of the staged tree; exit 1 if any.
verdict()
{
	tree=$WORK/$1
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

stage tree
verdict tree || die "the build number is not 0 outside a git checkout"
echo "PASS version-outside-git: VERSION $(cat "$WORK/tree/version.out"), kernel revision 0"

# The control: the same files with both count commands unguarded.
stage control
sed 's|^\(BUILD!=[[:space:]]*\).*|\1git rev-list --count HEAD --|' \
    "$root/share/mk/sys.mk" > "$WORK/control/share/mk/sys.mk"
sed 's|^GITREV=.*git rev-list.*|GITREV=`git rev-list --count HEAD --`|' \
    "$root/sys/conf/newvers.sh" > "$WORK/control/sys/conf/newvers.sh"
if verdict control > "$WORK/control.out"; then
	die "the unguarded control was accepted"
fi
count=$(wc -l < "$WORK/control.out" | tr -d ' ')
[ "$count" -ge 3 ] || die "the unguarded control raised $count defects, expected at least 3"
echo "PASS version-outside-git control: the unguarded count is rejected ($count defects)"
