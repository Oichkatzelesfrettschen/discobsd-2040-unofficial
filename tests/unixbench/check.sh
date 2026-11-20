#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Host check of usr.bin/unixbench/Run's setup path as a non-root user over a
# staged copy of the install layout (usr.bin/unixbench/Makefile "install"):
# the scripts and test inputs under a read-only HOMEDIR, a stand-in for the
# target's looper, and the suite narrowed to the shell and C tests so that the
# helper lookup (looper, multi.sh), the working directories and the test
# programs' writes into their directory all run. The check fails on "not
# found", on a permission error or on a missing result log. Three mutants of
# Run that restore the unfixed behavior (HOMEDIR off PATH, results in the
# install tree, tests run in the install tree) must each fail it. A second
# case plants a link at the default work directory's name and requires Run to
# stop with the link's target untouched; a mutant that makes that directory
# with mkdir -p must fail it.

set -eu

topsrc=$(cd "$(dirname "$0")/../.." && pwd)
ub=$topsrc/usr.bin/unixbench
work=$(mktemp -d "${TMPDIR:-/tmp}/unixbench-run.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT
	chmod -R u+w "$work" 2>/dev/null || :
	find "$work" -depth -type f -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

if [ "$(id -u)" = 0 ]; then
	echo "unixbench_run: SKIP: a root caller reads any mode; run as a non-root user" >&2
	exit 0
fi

# stage RUNFILE DIR: the install layout with RUNFILE as Run.
stage()
{
	mkdir -p "$2/home/testdir"
	for f in cleanup.sh fs.awk index.awk index.base index.sh loopm.awk \
	    loops.awk lps.awk multi.sh mwips.awk report.awk report.sh tst.sh \
	    unixbench.logo dhry.awk time.awk mem.awk; do
		if [ -f "$ub/pgms/$f" ]; then
			cp "$ub/pgms/$f" "$2/home/$f"
		fi
	done
	cp "$ub"/testdir/* "$2/home/testdir"
	cp "$1" "$2/home/Run"
	# looper TIME PROGRAM [ARG...] runs PROGRAM once on the target.
	cat > "$2/home/looper" <<'EOF'
#!/bin/sh
echo "$*" >> "$TMPDIR/looper.ran"
shift
exec "$@"
EOF
	chmod 755 "$2/home/looper"
	chmod -R a-w "$2/home"
}

# attempt NAME RUNFILE: stage, run as this user, return 0 when clean.
attempt()
{
	name=$1
	mkdir "$work/$name"
	stage "$2" "$work/$name"
	if ! (cd "$work/$name" && env -i PATH=/usr/bin:/bin HOME="$work/$name" \
	    UB_HOMEDIR="$work/$name/home" UB_WORKDIR="$work/$name/work" \
	    UB_SUITE="shell C" iterations=1 background=1 looper=1 CC=true \
	    TIME="%e" sh "$work/$name/home/Run" > "$work/$name/out" 2>&1); then
		:
	fi
	if grep -E "not found|ermission denied|annot create|annot run|No such file" "$work/$name/out" \
	    > /dev/null; then
		return 1
	fi
	test -s "$work/$name/work/results/log" || return 1
	# The shell and C tests each reached the stand-in looper by bare name;
	# Run sends a "not found" to a file, so the marker is the evidence.
	test -f "$work/$name/work/tmp/looper.ran" || return 1
	test "$(wc -l < "$work/$name/work/tmp/looper.ran")" -ge 2 || return 1
	return 0
}

attempt good "$ub/Run" || {
	echo "unixbench_run: FAIL: the staged install did not run clean" >&2
	grep -E "not found|ermission denied|annot create|annot run|No such file" "$work/good/out" >&2 || :
	exit 1
}
echo "unixbench_run: PASS (shell and C tests, read-only install, non-root)"

mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$ub/Run" > "$work/$name.Run"
	if cmp -s "$ub/Run" "$work/$name.Run"; then
		echo "unixbench_run: FAIL: $name mutation changed nothing" >&2
		exit 1
	fi
	if attempt "$name" "$work/$name.Run"; then
		echo "unixbench_run: FAIL: $name mutation was accepted" >&2
		exit 1
	fi
	echo "unixbench_run: PASS ($name mutation rejected)"
}
mutant path-dropped 's|^PATH=${HOMEDIR}:${PATH}$|PATH=${PATH}|'
mutant results-in-home 's|^RESULTDIR=${WORKDIR}/results|RESULTDIR=${HOMEDIR}/results|'
mutant tests-in-home 's|^TESTDIR=${WORKDIR}/testdir|TESTDIR=${HOMEDIR}/testdir|'

# planted NAME RUNFILE: with no UB_WORKDIR, Run makes /tmp/unixbench.UID.PID.
# Another user's link is planted at that name first (the shell that plants it
# becomes Run by exec, so the process ID is the one Run uses). Run must stop
# and write nothing through the link; return 0 when it does.
planted()
{
	name=$1
	mkdir "$work/$name" "$work/$name/victim"
	stage "$2" "$work/$name"
	(cd "$work/$name" && env -i PATH=/usr/bin:/bin HOME="$work/$name" \
	    UB_HOMEDIR="$work/$name/home" UB_SUITE="shell" iterations=1 \
	    background=1 looper=1 CC=true TIME="%e" VICTIM="$work/$name/victim" \
	    sh -c 'p=/tmp/unixbench.$(id -u).$$; ln -s "$VICTIM" "$p" || exit 9
	    echo "$p" > link.path; exec sh home/Run' > "$work/$name/out" 2>&1)
	rc=$?
	link=$(cat "$work/$name/link.path" 2> /dev/null)
	left=$(ls -A "$work/$name/victim" | wc -l)
	case $link in /tmp/unixbench.*) unlink "$link" 2> /dev/null || : ;; esac
	test "$rc" -ne 0 && test "$rc" -ne 9 && test "$left" -eq 0
}
planted link "$ub/Run" || {
	echo "unixbench_run: FAIL: Run wrote through a link planted at its work directory" >&2
	exit 1
}
echo "unixbench_run: PASS (a planted work directory is refused)"
sed 's|^	mkdir "${WORKDIR}" \|\| {$|	mkdir -p "${WORKDIR}" \|\| {|' "$ub/Run" > "$work/link-followed.Run"
if cmp -s "$ub/Run" "$work/link-followed.Run"; then
	echo "unixbench_run: FAIL: link-followed mutation changed nothing" >&2
	exit 1
fi
if planted link-followed "$work/link-followed.Run"; then
	echo "unixbench_run: FAIL: link-followed mutation was accepted" >&2
	exit 1
fi
echo "unixbench_run: PASS (link-followed mutation rejected)"
