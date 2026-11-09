#!/bin/sh
# Reject a shipped RP2040 program whose object files define a global symbol
# that libc.a also defines. Under a shared libc image imported with
# `ld --just-symbols`, such a program global binds to the libc address without
# a diagnostic; under the static archive the program definition shadows the
# archive member only until another symbol of that member is pulled, which
# then fails the link with a duplicate definition. A program global therefore
# either stays out of the libc namespace (`static`, or a program prefix) or
# appears in the allow-list with its reason.
#
# usage: verify_libc_export_namespace.sh [-a ALLOWLIST] [-m MANIFEST] ROOT
#        verify_libc_export_namespace.sh --selftest
#
# ROOT holds a completed RP2040 build: lib/libc.a and the program objects.
# MANIFEST (default ROOT/distrib/rp2040/mi.rp2040) names the shipped programs:
# each `pack` entry must have a source directory with objects, and a `file`
# entry that has objects is checked as well. A source directory is the first
# of bin, sbin, usr.bin, usr.sbin, libexec and games holding NAME/Makefile,
# which resolves the multicall boxes that live under sbin. Directory
# usr.bin/find links only find.o; every other program links all of its *.o.
#
# ALLOWLIST lines read `PROGRAM SYMBOL # reason`. An entry without a reason,
# or one that no longer matches a collision, fails the gate.
#
# Environment: NM (default arm-none-eabi-nm), CC (default arm-none-eabi-gcc,
# used only by --selftest).
set -eu

NM=${NM:-arm-none-eabi-nm}
CC=${CC:-arm-none-eabi-gcc}
LC_ALL=C
export LC_ALL

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

die() {
    echo "verify_libc_export_namespace: $*" >&2
    exit 2
}

# Print the defined global symbol names of the given object or archive files,
# one per line, sorted and unique. COMMON symbols count as defined.
defined_globals() {
    "$NM" -g --defined-only "$@" | awk 'NF == 3 {print $3}' | sort -u
}

# check_programs LIBC LIST ALLOW
# LIST holds one `NAME DIR` pair per line and DIR holds the objects. Prints
# the collisions as `NAME SYMBOL` lines to stdout, then applies ALLOW.
# Returns 0 when no collision survives the allow-list and every allow-list
# entry matched one.
check_programs() {
    libc=$1 list=$2 allow=$3
    defined_globals "$libc" > "$WORK/libc.exports"
    [ -s "$WORK/libc.exports" ] || die "no defined globals in $libc"
    : > "$WORK/collisions"
    while read -r name dir; do
        # A directory may also hold host-format objects from native test
        # builds; arm-none-eabi-nm rejects those and a target object it
        # cannot read does not exist, so the readable objects are the
        # program's.
        : > "$WORK/objects"
        skipped=0
        for obj in "$dir"/*.o; do
            [ "$name" = find ] && [ "${obj##*/}" != find.o ] && continue
            [ -f "$obj" ] || continue
            if "$NM" "$obj" > /dev/null 2>&1; then
                echo "$obj" >> "$WORK/objects"
            else
                skipped=$((skipped + 1))
            fi
        done
        [ "$skipped" -eq 0 ] ||
            echo "note: $name: $skipped objects in $dir are not target objects and were skipped" >&2
        [ -s "$WORK/objects" ] || die "no target objects for $name in $dir (build first)"
        # shellcheck disable=SC2046
        defined_globals $(cat "$WORK/objects") | comm -12 - "$WORK/libc.exports" |
            sed "s|^|$name |" >> "$WORK/collisions"
    done < "$list"
    sort -u -o "$WORK/collisions" "$WORK/collisions"

    : > "$WORK/allowed"
    if [ -n "$allow" ]; then
        grep -vE '^[[:space:]]*(#|$)' "$allow" > "$WORK/allow.lines" || true
        : > "$WORK/allowed.raw"
        while read -r line; do
            case $line in
                *'#'*[![:space:]]*) ;;
                *) echo "allow-list entry without a reason: $line" >&2
                   return 1 ;;
            esac
            entry=${line%%#*}
            # shellcheck disable=SC2086
            set -- $entry
            [ $# -eq 2 ] || { echo "allow-list entry is not PROGRAM SYMBOL # reason: $line" >&2
                              return 1; }
            echo "$1 $2" >> "$WORK/allowed.raw"
        done < "$WORK/allow.lines"
        sort -u "$WORK/allowed.raw" > "$WORK/allowed"
    fi
    comm -13 "$WORK/collisions" "$WORK/allowed" > "$WORK/stale"
    if [ -s "$WORK/stale" ]; then
        sed 's/^/stale allow-list entry: /' "$WORK/stale" >&2
        return 1
    fi
    comm -23 "$WORK/collisions" "$WORK/allowed" > "$WORK/failing"
    cat "$WORK/failing"
    [ ! -s "$WORK/failing" ]
}

# Compile one good and one bad object, each beside an unreadable junk.o that
# stands for a host-format object, then require the check to pass on the first
# and to fail on the second, with and without an allow-list.
selftest() {
    libc=$1
    [ -f "$libc" ] || die "$libc missing"
    defined_globals "$libc" > "$WORK/libc.names"
    # A function symbol that libc.a defines; its name comes from the archive
    # so the probe tracks the archive contents.
    "$NM" -g --defined-only "$libc" | awk 'NF == 3 && $2 == "T" {print $3}' |
        sort -u | head -n 1 > "$WORK/clash.name"
    clash=$(cat "$WORK/clash.name")
    [ -n "$clash" ] || die "no text symbol in $libc"
    mkdir -p "$WORK/good" "$WORK/bad"
    printf 'void export_probe_unique(void) {}\n' > "$WORK/good.c"
    printf 'void %s(void) {}\nvoid export_probe_unique(void) {}\n' "$clash" \
        > "$WORK/bad.c"
    flags="-mcpu=cortex-m0plus -mthumb -fno-common -ffreestanding -c"
    # shellcheck disable=SC2086
    "$CC" $flags -o "$WORK/good/p.o" "$WORK/good.c"
    # shellcheck disable=SC2086
    "$CC" $flags -o "$WORK/bad/p.o" "$WORK/bad.c"
    printf 'not an object\n' > "$WORK/good/junk.o"
    printf 'not an object\n' > "$WORK/bad/junk.o"
    printf 'good %s\n' "$WORK/good" > "$WORK/good.list"
    printf 'bad %s\n' "$WORK/bad" > "$WORK/bad.list"
    printf 'bad %s # selftest override\n' "$clash" > "$WORK/ok.allow"
    printf 'bad %s\n' "$clash" > "$WORK/noreason.allow"
    printf 'good %s # no such collision\n' "$clash" > "$WORK/stale.allow"

    check_programs "$libc" "$WORK/good.list" "" > /dev/null 2>&1 ||
        die "selftest: clean object rejected"
    if check_programs "$libc" "$WORK/bad.list" "" > /dev/null 2>&1; then
        die "selftest: colliding object accepted"
    fi
    check_programs "$libc" "$WORK/bad.list" "$WORK/ok.allow" > /dev/null 2>&1 ||
        die "selftest: allow-listed collision rejected"
    if check_programs "$libc" "$WORK/bad.list" "$WORK/noreason.allow" \
        > /dev/null 2>&1; then
        die "selftest: allow-list entry without a reason accepted"
    fi
    if check_programs "$libc" "$WORK/good.list" "$WORK/stale.allow" \
        > /dev/null 2>&1; then
        die "selftest: stale allow-list entry accepted"
    fi
    echo "libc-export-namespace selftest: clean accepted, collision on $clash rejected, allow-list rules enforced"
}

ALLOW=
MANIFEST=
SELFTEST=no
while [ $# -gt 0 ]; do
    case $1 in
        -a) [ $# -ge 2 ] || die "-a needs a file"; ALLOW=$2; shift 2 ;;
        -m) [ $# -ge 2 ] || die "-m needs a file"; MANIFEST=$2; shift 2 ;;
        --selftest) SELFTEST=yes; shift ;;
        -*) die "unknown option $1" ;;
        *) break ;;
    esac
done
[ $# -eq 1 ] || die "usage: $0 [-a ALLOWLIST] [-m MANIFEST] ROOT | --selftest ROOT"
ROOT=$1
LIBC=$ROOT/lib/libc.a
[ -f "$LIBC" ] || die "$LIBC missing (build first)"

if [ "$SELFTEST" = yes ]; then
    selftest "$LIBC"
    exit 0
fi

[ -n "$MANIFEST" ] || MANIFEST=$ROOT/distrib/rp2040/mi.rp2040
[ -f "$MANIFEST" ] || die "$MANIFEST missing"
[ -z "$ALLOW" ] || [ -f "$ALLOW" ] || die "$ALLOW missing"

# Resolve each shipped program to its source directory. An entry between
# `#closure NAME` and `#endclosure NAME` ships only in profiles that select
# the closure, so a closure program the tree has not built is reported and
# skipped; an unbuilt base-system program fails the gate.
awk '/^#closure / {open = 1; next}
    /^#endclosure / {open = 0; next}
    $1 == "pack" || $1 == "file" {
        n = split($2, a, "/")
        print (open ? "closure-" : "") $1, a[n]
    }' "$MANIFEST" | sort -u > "$WORK/entries"
: > "$WORK/list.raw"
: > "$WORK/skipped"
while read -r kind name; do
    dir=
    for parent in bin sbin usr.bin usr.sbin libexec games; do
        if [ -f "$ROOT/$parent/$name/Makefile" ]; then
            dir=$ROOT/$parent/$name
            break
        fi
    done
    if [ -z "$dir" ] || ! ls "$dir"/*.o > /dev/null 2>&1; then
        case $kind in
            pack) die "no built objects for packed program $name (build first)" ;;
            closure-pack) echo "$name" >> "$WORK/skipped" ;;
        esac
        continue
    fi
    echo "$name $dir" >> "$WORK/list.raw"
done < "$WORK/entries"
sort -u "$WORK/list.raw" > "$WORK/list"
[ -s "$WORK/list" ] || die "no shipped program resolved from $MANIFEST"

if [ -s "$WORK/skipped" ]; then
    echo "libc-export-namespace: unbuilt closure programs skipped: $(tr '\n' ' ' < "$WORK/skipped")"
fi
if check_programs "$LIBC" "$WORK/list" "$ALLOW" > "$WORK/failing.out"; then
    echo "libc-export-namespace: $(wc -l < "$WORK/list") programs, no program global collides with libc.a"
    exit 0
fi
while read -r name symbol; do
    echo "libc-export-namespace FAIL $name: defines libc export $symbol" >&2
done < "$WORK/failing.out"
exit 1
