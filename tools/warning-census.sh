#!/bin/sh
# Count the compiler warnings the tree would raise under a wider warning set,
# say which file and which category each one belongs to, and record enough
# of the run that the count can be checked rather than believed.
#
# The census exists because the obvious ways of measuring this both lie.
# Building with -j interleaves the output of parallel compiles, so a warning
# cannot be attributed to a source file by reading back to the nearest
# compile line: the nearest line belongs to whichever job printed last. And
# raising the warning set without demoting errors stops every directory that
# already carries -Werror, which ends the build after a few dozen files and
# reports a count far below the truth.
#
# Three more ways an earlier version of this script lied, each now closed:
#
# Order. It cleaned every directory and then built them in the order it
# was given, bin before lib, so bin linked against a libc.a the clean had
# removed and ten directories stopped. The directories now come from the
# root Makefile's SUBDIR in the root's own order, and each is cleaned,
# built and installed just before the next, the way the root's build
# target runs them: lib's clean removes lib/crt0.o and lib/libc.a, and
# it is lib's install that puts them back for everything after it. The
# install step compiles on its own account, lib/libc_aout rebuilding its
# members under the tree's flags, so it is logged apart and its compiles
# are counted but never measured: the census is the build step's.
#
# Reach. It passed the warning flags through COPTS, which share/mk/sys.mk
# folds into CFLAGS, and a Makefile that assigns CFLAGS outright, as
# usr.bin/smlrc and games/hunt do, compiled without them and counted as
# clean. The flags now ride WARNERR, which sys.mk puts on the CC command
# itself for exactly this reason: it is the lever -Werror uses, so every
# compile that is fatal under the policy is also measured by the census.
# The script then reads every cross compile line back and reports any
# that lacks a flag, so a leaf that finds a new way around the lever is a
# named gap rather than a silent one.
#
# Record. It printed a summary and deleted its log. The run now leaves a
# directory holding the command run for each directory, its exit status,
# the compile and warning counts it contributed, the compile lines the
# flags did not reach, and the full log, so the summary's numbers can be
# traced to the lines they came from.
#
# A warning is counted twice over: as an instance, once per emission, and
# as a site, once per file, line and category, because a header diagnostic
# is emitted by every translation unit that includes the header and the
# two counts answer different questions, how noisy a build is and how many
# places a repair has to touch.
#
# A warning's own file:line, which the compiler prints, is what the census
# reports by file, resolved against the directory the compile ran in: the
# cross compiler is reached through a wrapper in the record directory that
# prints its working directory and then runs the real one with the same
# arguments, since GCC prints the path a file was named by and a leaf
# names its sources relative to itself, so main.c:35 in one program is
# otherwise indistinguishable from main.c:35 in another. Which compiler raised it is read from the compile line
# that precedes it, which the serial build makes sound, so warnings from
# the host compiles a few Makefiles run for their generators are counted
# apart from the cross compiles the census is about.
#
# Usage: sh tools/warning-census.sh [-w "WARNING FLAGS"] [-o RECORD_DIR]
#                                    [directory ...]
# Default flags are -Wall -Wextra, the default directories are the root
# Makefile's SUBDIR, and the record goes to RECORD_DIR or a fresh
# directory under ${TMPDIR:-/tmp} whose name is printed. The kernel is a
# separate target, because its Makefile carries its own CWARNFLAGS:
#   bmake MACHINE=rp2040 kernel CWARNFLAGS='-Wall -Wextra -Wno-error'
#
# Exit status is 0 when every directory built and every cross compile
# carried every flag; 1 names what did not, and the counts printed are then
# a floor.

set -eu

TOPSRC=$(cd "$(dirname "$0")/.." && pwd)
WARNFLAGS='-Wall -Wextra'
MACHINE=${MACHINE:-rp2040}
RECORD=

while [ $# -gt 0 ]; do
	case $1 in
	-w) WARNFLAGS=$2; shift 2 ;;
	-o) RECORD=$2; shift 2 ;;
	-*) echo "usage: warning-census.sh [-w FLAGS] [-o DIR] [directory ...]" >&2
	    exit 2 ;;
	*) break ;;
	esac
done

command -v bmake >/dev/null 2>&1 || {
	echo "warning-census: bmake absent; not run" >&2
	exit 1
}

# A userland census compiles against the tree's own headers and links
# against its libc, so it wants a tree that `bmake MACHINE=<m> build` has
# already populated. Naming the missing piece here beats a screen of
# "machine/machparam.h: No such file" from every directory in turn.
[ -e "$TOPSRC/include/machine" ] || {
	echo "warning-census: include/machine absent; run bmake MACHINE=$MACHINE symlinks; not run" >&2
	exit 1
}
[ -f "$TOPSRC/lib/crt0.o" ] || {
	echo "warning-census: lib/crt0.o absent; run bmake MACHINE=$MACHINE build; not run" >&2
	exit 1
}

cd "$TOPSRC"

# The root's own order is the dependency order: share before lib, lib
# before everything that links it, usr.bin before the sbin multicall
# programs that take its objects.
if [ $# -eq 0 ]; then
	# shellcheck disable=SC2046
	set -- $(bmake MACHINE="$MACHINE" -V SUBDIR)
	[ $# -gt 0 ] || { echo "warning-census: root SUBDIR is empty; not run" >&2; exit 1; }
fi
for d in "$@"; do
	[ -d "$d" ] || { echo "warning-census: no directory $d; not run" >&2; exit 1; }
done

# The compilers, read from share/mk/sys.mk through a directory that
# includes it; the root Makefile carries neither.
REALPREFIX=$(bmake MACHINE="$MACHINE" -C lib -V GCCPREFIX)
[ -n "$REALPREFIX" ] || { echo "warning-census: GCCPREFIX is empty; not run" >&2; exit 1; }
[ -x "$REALPREFIX-gcc" ] || { echo "warning-census: $REALPREFIX-gcc absent; not run" >&2; exit 1; }
# shellcheck disable=SC2016
DESTDIR=$(bmake MACHINE="$MACHINE" -V '${DESTDIR}')
[ -n "$DESTDIR" ] && [ -d "$DESTDIR" ] || {
	echo "warning-census: DESTDIR '$DESTDIR' absent; run bmake MACHINE=$MACHINE build; not run" >&2
	exit 1
}
HOSTCC=$(bmake MACHINE="$MACHINE" -C lib -V HOST_CC)
[ -n "$HOSTCC" ] || { echo "warning-census: HOST_CC is empty; not run" >&2; exit 1; }

if [ -z "$RECORD" ]; then
	RECORD=$(mktemp -d "${TMPDIR:-/tmp}/warning-census.XXXXXX")
else
	mkdir -p "$RECORD"
fi
: > "$RECORD/commands.txt"
: > "$RECORD/uncovered.txt"
: > "$RECORD/log.txt"
# The wrapper prefix: every tool the prefix names is a link to the real
# one, and the compiler is a script that announces its directory first.
PREFIX="$RECORD/cc/$(basename "$REALPREFIX")"
mkdir -p "$RECORD/cc"
for tool in "$REALPREFIX"-*; do
	[ -e "$tool" ] || continue
	ln -sf "$tool" "$PREFIX-${tool##"$REALPREFIX"-}"
done
rm -f "$PREFIX-gcc"
# shellcheck disable=SC2016
printf '#!/bin/sh\nprintf "warning-census-cwd %%s\\n" "$PWD"\nexec %s "$@"\n' \
	"$REALPREFIX-gcc" > "$PREFIX-gcc"
chmod +x "$PREFIX-gcc"
CROSS="$PREFIX-gcc"

printf 'directory\tstatus\tcross_compiles\tcovered\tuncovered\thost_compiles\tcross_warnings\thost_warnings\tinstall_compiles\tsites\n' \
	> "$RECORD/record.tsv"

# A recorded cross warning is "<cwd>|<file>:<line>:<col>: warning: ...".
# located() rewrites each to a path relative to the tree, resolving the
# file against the compile's directory; sites_of() reduces that to one
# line per file, line and category.
located() {
	awk -v top="$TOPSRC/" '
	function norm(p,    n, i, parts, out, k, r) {
		n = split(p, parts, "/"); k = 0
		for (i = 1; i <= n; i++) {
			if (parts[i] == "" || parts[i] == ".") continue
			if (parts[i] == "..") { if (k > 0) k--; continue }
			out[++k] = parts[i]
		}
		r = ""
		for (i = 1; i <= k; i++) r = r "/" out[i]
		return r
	}
	{
		bar = index($0, "|"); cwd = substr($0, 1, bar - 1); rest = substr($0, bar + 1)
		colon = index(rest, ":"); file = substr(rest, 1, colon - 1)
		path = (file ~ /^\//) ? norm(file) : norm(cwd "/" file)
		if (index(path, top) == 1) path = substr(path, length(top) + 1)
		print path substr(rest, colon)
	}' "$1"
}
sites_of() {
	located "$1" |
	sed -n 's|^\([^ ][^:]*:[0-9][0-9]*\):[0-9]*:[[:space:]]*warning:.*\(\[-W[^]]*\]\).*|\1 \2|p' |
	sort -u
}

# Every flag, and the demotion, must appear on a cross compile line for it
# to count as covered.
covered_line() {
	line=$1
	for flag in $WARNFLAGS -Wno-error; do
		case " $line " in
		*" $flag "*) ;;
		*) return 1 ;;
		esac
	done
	return 0
}

failed=0
uncovered_total=0

# Serial on purpose: the compiler class of a warning is read from the
# compile line before it, which only holds when one compile runs at a time.
for d in "$@"; do
	tag=$(printf '%s' "$d" | tr '/' '_')
	log="$RECORD/$tag.log"
	ilog="$RECORD/$tag.install.log"
	cwarn="$RECORD/$tag.cross-warnings.txt"
	: > "$cwarn"
	bmake MACHINE="$MACHINE" -C "$d" clean >/dev/null 2>&1 || true
	cmd="bmake MACHINE=$MACHINE -C $d GCCPREFIX=$PREFIX WARNERR='$WARNFLAGS -Wno-error' && bmake MACHINE=$MACHINE -C $d GCCPREFIX=$PREFIX DESTDIR=$DESTDIR install"
	printf '%s\n' "$cmd" >> "$RECORD/commands.txt"
	: > "$ilog"
	if bmake MACHINE="$MACHINE" -C "$d" GCCPREFIX="$PREFIX" \
	    WARNERR="$WARNFLAGS -Wno-error" > "$log" 2>&1 &&
	   bmake MACHINE="$MACHINE" -C "$d" GCCPREFIX="$PREFIX" DESTDIR="$DESTDIR" install \
	    > "$ilog" 2>&1; then
		status=0
	else
		status=$?
		failed=$((failed + 1))
	fi
	cat "$log" >> "$RECORD/log.txt"
	inst=$(grep -c -E "$CROSS .*(-c |[.][cS]( |\$))" "$ilog" || true)

	cross=0; cov=0; host=0; cw=0; hw=0; class=other; cwd=$TOPSRC/$d
	while IFS= read -r line; do
		case $line in
		"warning-census-cwd "*)
			cwd=${line#warning-census-cwd }
			;;
		*"$CROSS "*" -c "*|*"$CROSS "*.c|*"$CROSS "*.c" "*|*"$CROSS "*.S|*"$CROSS "*.S" "*)
			# A compile is a cross line that carries -c or names a
			# source; usr.bin/retroforth compiles and links in one
			# command, and a test on -c alone misses its warnings.
			class=cross
			cross=$((cross + 1))
			if covered_line "$line"; then
				cov=$((cov + 1))
			else
				printf '%s\t%s\n' "$d" "$line" >> "$RECORD/uncovered.txt"
			fi
			;;
		*"$CROSS "*)
			class=other
			;;
		"$HOSTCC "*|*" $HOSTCC "*)
			class=host
			host=$((host + 1))
			;;
		*warning:*)
			case $class in
			cross) cw=$((cw + 1)); printf '%s|%s\n' "$cwd" "$line" >> "$cwarn" ;;
			host) hw=$((hw + 1)) ;;
			esac
			;;
		esac
	done < "$log"
	unc=$((cross - cov))
	uncovered_total=$((uncovered_total + unc))
	dsites=$(sites_of "$cwarn" | grep -c . || true)
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$d" "$status" "$cross" "$cov" "$unc" "$host" "$cw" "$hw" "$inst" "$dsites" \
		>> "$RECORD/record.tsv"
done

LOG="$RECORD/log.txt"
sum() { awk -F'\t' -v c="$1" 'NR > 1 { s += $c } END { print s + 0 }' "$RECORD/record.tsv"; }
compiles=$(sum 3)
covered=$(sum 4)
hostc=$(sum 6)
total=$(sum 7)
hostw=$(sum 8)
inst=$(sum 9)
cat "$RECORD"/*.cross-warnings.txt > "$RECORD/cross-warnings.txt"
sites_of "$RECORD/cross-warnings.txt" > "$RECORD/sites.txt"
nsites=$(grep -c . "$RECORD/sites.txt" || true)

{
printf 'warning census: %s\n' "$WARNFLAGS"
printf '  directories %s   build failures %s\n' "$#" "$failed"
printf '  cross compiles %s   carrying every flag %s   without %s\n' \
	"$compiles" "$covered" "$uncovered_total"
printf '  cross warnings %s   host compiles %s   host warnings %s\n' \
	"$total" "$hostc" "$hostw"
printf '  install-step compiles %s, logged apart and not measured\n' "$inst"
printf '  distinct sites (file, line and category) %s; see sites.txt\n\n' "$nsites"

if [ "$failed" -gt 0 ]; then
	printf '  %s directories failed to build: the census is a floor, not a total.\n' "$failed"
	awk -F'\t' 'NR > 1 && $2 != 0 { printf "    %s (exit %s)\n", $1, $2 }' "$RECORD/record.tsv"
	printf '\n'
fi
if [ "$uncovered_total" -gt 0 ]; then
	printf '  %s cross compiles did not carry every flag; see uncovered.txt:\n' "$uncovered_total"
	awk -F'\t' 'NR > 1 && $5 > 0 { printf "    %s: %s of %s\n", $1, $5, $3 }' "$RECORD/record.tsv"
	printf '\n'
fi

printf 'by directory (cross compiles, warning instances, distinct sites)\n'
awk -F'\t' 'NR > 1 { printf "  %-12s %5s compiles %5s warnings %5s sites\n", $1, $3, $7, $10 }' \
	"$RECORD/record.tsv"

printf '\nby category: instances, then distinct sites, cross compiles only\n'
grep -o '\[-W[a-z0-9=+-]*\]' "$RECORD/cross-warnings.txt" | sort | uniq -c | sort -rn |
	while read -r n cat; do
		printf '  %6d  %6d  %s\n' "$n" "$(grep -c -F " $cat" "$RECORD/sites.txt")" "$cat"
	done

printf '\nby file, resolved against the compile directory, cross compiles only\n'
located "$RECORD/cross-warnings.txt" |
	sed -n 's|^\([^ ][^:]*\):[0-9][0-9]*:[0-9]*:[[:space:]]*warning:.*|\1|p' |
	sort | uniq -c | sort -rn | head -25 |
	awk '{printf "  %6d  %s\n", $1, $2}'

printf '\nwarnings raised inside a macro, by macro\n'
grep -o "in expansion of macro '[^']*'" "$LOG" | sort | uniq -c | sort -rn |
	head -10 | awk '{printf "  %6d  %s\n", $1, $6}'

printf '\nrecord: %s\n' "$RECORD"
} | tee "$RECORD/summary.txt"

[ "$failed" -eq 0 ] && [ "$uncovered_total" -eq 0 ]
