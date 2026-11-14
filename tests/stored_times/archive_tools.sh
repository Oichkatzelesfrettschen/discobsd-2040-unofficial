#!/bin/sh
#
# archive_tools.sh
#
# The archive tools (usr.bin/ar, usr.bin/ranlib, usr.bin/ld) and usr.bin/make
# read, write and order a file time as the kernel and the disk store it: an
# unsigned 32-bit count of seconds since 1970, valid through 4294967295
# (February 2106). This gate builds the three archive programs from their
# target sources, and links make's doname.c into a fixture (make_order.c; the
# rest of make needs the directory stream of the target libc and has no host
# build), for three views of time and runs one suite of cases against each:
#
#   native         64-bit time_t and long, the host tools the build uses
#   i386-time32    four-byte signed time_t and long, the target default; the
#                  stat family and time() come from stat_shim.c, which
#                  returns a time after 2038 as the negative number the
#                  kernel returns
#   i386-time64    four-byte long, 64-bit time_t, the target with
#                  -D_TIME_BITS=64
#
# A view the host compiler cannot build and run (no 32-bit libc) reports
# SKIP with the reason. The cases cover the header date read through
# 4294967295, its rejection above that and for a minus sign, the listing and
# extraction dates, the ordering of file times against member dates for ar's
# u modifier, the date ar writes, the SOURCE_DATE_EPOCH bound, the symbol
# table date ranlib writes (clamped at 4294967295), ld's test of an archive
# against its symbol table, and make's choice to rebuild a target whose time
# and whose dependency's time lie on either side of January 2038. Fourteen
# source mutants, each restoring one signed or narrow reading, must each fail
# at least one case under the view that exposes it; a mutant that passes
# every view fails the gate.
#
# The two a.out objects the ranlib and ld cases use (one defines foo, one
# calls it) are written from their bytes, so the gate needs a C compiler and
# $PYTHON only. ARCHIVE_TOOLS_ROOT names another source tree (a checkout of
# an earlier revision) to run the suite against; the mutants apply to this
# tree only and are skipped then.
# Output: one PASS line, or FAIL lines on standard error and exit status 1.
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

if [ "$#" -ne 0 ]; then
    echo "usage: archive_tools.sh" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
# A build exports SOURCE_DATE_EPOCH to every recipe; each case that depends
# on it sets it, and the clock cases set STORED_TIMES_NOW.
unset SOURCE_DATE_EPOCH STORED_TIMES_NOW
# CC may carry options, as in the other host gates.
cc=${CC:-cc}
tools_root=${ARCHIVE_TOOLS_ROOT:-$top}

work=$(mktemp -d "${TMPDIR:-/tmp}/archive-stored-times.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

failures=0
cases=0
note_failure() {
    echo "FAIL stored-times archive: $*" >&2
    failures=$((failures + 1))
}

# check LABEL EXPECTED ACTUAL
check() {
    cases=$((cases + 1))
    if [ "$2" != "$3" ]; then
        note_failure "$view: $1: expected [$2], got [$3]"
    fi
}

view_flags() {
    case $1 in
    native) echo "" ;;
    i386-time32) echo "-m32" ;;
    i386-time64) echo "-m32 -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64" ;;
    esac
}

# Only the four-byte time_t view calls the shim's statx() half.
shim_defines() {
    if [ "$1" != i386-time32 ]; then
        echo "-DSTORED_TIMES_NO_STATX"
    fi
}

# time() the program calls by name becomes the shim's in every view (the
# shim, built with the same definition, defines it under the symbol the
# view binds time() to, so no linker option is needed and the mechanism is
# the same on every host). The stat family becomes the shim's under the
# four-byte time_t view; the other views read times through their own
# 64-bit time_t.
view_defines() {
    echo "-Dtime(t)=stored_times_time(t)"
    if [ "$1" = i386-time32 ]; then
        echo "-Dstat(p,b)=stored_times_stat(p,b)" \
            "-Dlstat(p,b)=stored_times_lstat(p,b)" \
            "-Dfstat(d,b)=stored_times_fstat(d,b)"
    fi
}

# view_works VIEW: the compiler builds and the kernel runs the view.
view_works() {
    probe_flags=$(view_flags "$1")
    cat > "$work/probe.c" <<'EOF'
#include <stdio.h>
#include <time.h>
int main(void) { return sizeof(time_t) > 0 && sizeof(long) > 0 ? 0 : 1; }
EOF
    # shellcheck disable=SC2086
    $cc $probe_flags -o "$work/probe" "$work/probe.c" 2>/dev/null &&
        "$work/probe" 2>/dev/null
}

# compile_tools VIEW SOURCEROOT OUTDIR: ar, ranlib and ld from the sources
# under SOURCEROOT. Returns nonzero with the compiler output on failure.
compile_tools() {
    ct_view=$1
    ct_root=$2
    ct_out=$3
    ct_flags=$(view_flags "$ct_view")
    mkdir -p "$ct_out"
    # Every definition is a separate word; none holds a space.
    # STORED_TIMES_CFLAGS follows the gate's own flags, for a compiler whose
    # default warnings go beyond the ones these sources are kept clean of.
    # shellcheck disable=SC2046
    set -- $ct_flags $(view_defines "$ct_view") \
        -Os -Wall -Wextra -Wundef -Wformat-security -Werror -std=gnu89 \
        -fno-builtin -DCROSS \
        -I"$ct_root/usr.bin/ar" -I"$ct_root/usr.bin/ranlib" \
        -I"$top/tools/aoututils/include" -idirafter "$top/include" \
        ${STORED_TIMES_CFLAGS:-}
    $cc $(view_flags "$ct_view") "-Dtime(t)=stored_times_time(t)" \
        $(shim_defines "$ct_view") \
        -c -o "$ct_out/stat_shim.o" "$here/stat_shim.c" || return 1
    for ct_source in append ar archive contents delete extract misc move \
        print replace strmode; do
        $cc "$@" -c -o "$ct_out/ar-$ct_source.o" \
            "$ct_root/usr.bin/ar/$ct_source.c" || return 1
    done
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -o "$ct_out/ar" "$ct_out"/ar-*.o \
        "$ct_out/stat_shim.o" || return 1
    $cc "$@" -c -o "$ct_out/ranlib-archive.o" \
        "$ct_root/usr.bin/ar/archive.c" || return 1
    $cc "$@" -c -o "$ct_out/ranlib-main.o" \
        "$ct_root/usr.bin/ranlib/ranlib.c" || return 1
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -o "$ct_out/ranlib" \
        "$ct_out/ranlib-archive.o" "$ct_out/ranlib-main.o" \
        "$ct_out/stat_shim.o" || return 1
    $cc "$@" -c -o "$ct_out/ld.o" "$ct_root/usr.bin/ld/ld.c" || return 1
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -o "$ct_out/ld" "$ct_out/ld.o" \
        "$ct_out/stat_shim.o" || return 1
    # make's source is legacy K&R C: doname.c compiles without warnings as
    # errors, the fixture with them. Both compile without CROSS, under which
    # usr.bin/make/defs.h names the host's <stdio.h> and <ctype.h> by the
    # absolute path /usr/include, a directory a macOS host lacks; the search
    # path finds the host's headers first because the tree's include
    # directory follows it (-idirafter).
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -O1 -w -std=gnu89 \
        -I"$ct_root/usr.bin/make" -I"$ct_root/usr.bin/ar" \
        -I"$top/tools/aoututils/include" -idirafter "$top/include" \
        -c -o "$ct_out/make-doname.o" \
        "$ct_root/usr.bin/make/doname.c" || return 1
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -O1 -Wall -Wextra -Werror -std=gnu89 \
        -I"$ct_root/usr.bin/make" -I"$ct_root/usr.bin/ar" \
        -I"$top/tools/aoututils/include" -idirafter "$top/include" \
        -c -o "$ct_out/make-order.o" "$here/make_order.c" || return 1
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") -o "$ct_out/make_order" \
        "$ct_out/make-doname.o" "$ct_out/make-order.o" || return 1
}

# craft_archive PATH NAME:DATE:BODY...: an archive written without ar, so
# the read cases do not depend on the writer under test.
craft_archive() {
    "$PYTHON" - "$@" <<'PY'
import sys

buffer = b"!<arch>\n"
for spec in sys.argv[2:]:
    name, date, body = spec.split(":")
    data = body.encode()
    buffer += (
        "%-16s%-12s%-6d%-6d%-8o%-10d`\n" % (name, date, 0, 0, 0o100644, len(data))
    ).encode() + data
    if len(data) & 1:
        buffer += b"\n"
with open(sys.argv[1], "wb") as handle:
    handle.write(buffer)
PY
}

# archive_dates PATH: one "name date" line per member, from the raw headers.
archive_dates() {
    "$PYTHON" - "$1" <<'PY'
import sys

with open(sys.argv[1], "rb") as handle:
    data = handle.read()
position = 8
while position + 60 <= len(data):
    header = data[position:position + 60]
    size = int(header[48:58])
    print(header[:16].decode().rstrip(), header[16:28].decode().strip())
    position += 60 + size + (size & 1)
PY
}

# set_times PATH SECONDS: both times of a file at a count since 1970.
set_times() {
    "$PYTHON" -c 'import os, sys; os.utime(sys.argv[1], (int(sys.argv[2]), int(sys.argv[2])))' "$1" "$2"
}

# file_mtime PATH
file_mtime() {
    "$PYTHON" -c 'import os, sys; print(os.stat(sys.argv[1]).st_mtime_ns // 1000000000)' "$1"
}

# set_table_date PATH SECONDS: the date of the first member (the symbol
# table), field offset 8 + 16.
set_table_date() {
    "$PYTHON" - "$1" "$2" <<'PY'
import sys

with open(sys.argv[1], "r+b") as handle:
    handle.seek(8 + 16)
    handle.write(("%-12s" % sys.argv[2]).encode())
PY
}

# as_seen N: the second count a utimes() through a four-byte signed time_t
# stores for N, and so the one stat() reads back on the same host.
as_seen() {
    if [ "$view" = i386-time32 ]; then
        echo $(($1 - 4294967296))
    else
        echo "$1"
    fi
}

# wallclock N: the listing text ar writes for a date, in UTC.
wallclock() {
    "$PYTHON" -c 'import sys, time; print(time.strftime("%b %d %H:%M %Y", time.gmtime(int(sys.argv[1]))))' "$1"
}

# write_objects: l.o defines foo, m.o calls it. The bytes are what the host
# assembler (tools/aoututils/as) writes for
#   .text; .globl foo; foo: bx lr
#   .text; .globl _start; _start: bl foo
write_objects() {
    "$PYTHON" - <<'PY'
objects = {
    "l.o": "06018f0004000000000000000000000000000000000000000c000000000000"
    "007047c046032200000000666f6f000000",
    "m.o": "06018f000400000000000000000000000800000000000000180000000000000000"
    "f000f878000000000100000622000000005f7374617274032000000000666f6f000000",
}
for name, text in objects.items():
    with open(name, "wb") as handle:
        handle.write(bytes.fromhex(text))
PY
}

run_suite() {
    view=$1
    bin=$2
    # Each run gets its own directory: a later run that met the files an
    # earlier one left would fail on them and count as a rejected mutant.
    suite=$(mktemp -d "$work/suite-$view.XXXXXX")
    cd "$suite"
    : > .keep
    suite_failures=$failures

    # Reading a header date through 4294967295.
    craft_archive read.a "a.o:2415919104:abcd" "b.o:4294967294:wxyz" \
        "c.o:4294967295:mnop"
    check "ar t lists members past 2038" "a.o
b.o
c.o" "$("$bin/ar" t read.a 2>&1 || echo "exit $?")"
    check "ar p prints a member dated 4294967294" "wxyz" \
        "$("$bin/ar" p read.a b.o 2>&1 || echo "exit $?")"
    check "ar reads the dates" "a.o 2415919104
b.o 4294967294
c.o 4294967295" "$(archive_dates read.a)"

    # Dates outside the stored range are format errors, not wrapped values.
    craft_archive over.a "a.o:4294967296:abcd"
    status=0
    "$bin/ar" t over.a >/dev/null 2>&1 || status=$?
    check "ar t refuses 4294967296" 1 "$status"
    # strtoul skips every C white-space byte before a sign, so each of
    # these is a negative count that would wrap to a stored time.
    for sign in "-1" "$(printf '\t-1')" "$(printf '\r-1')" "$(printf '\v-1')"; do
        craft_archive minus.a "a.o:$sign:abcd"
        status=0
        "$bin/ar" t minus.a >/dev/null 2>&1 || status=$?
        check "ar t refuses the date [$sign]" 1 "$status"
    done

    # A member dated 4294967295 is the kernel's "leave this time alone", so
    # a four-byte view silently keeps the extraction time and the 64-bit view
    # refuses it. ar reports the member and does not set the time, in every
    # view; the file keeps a time of this decade.
    craft_archive ones.a "a.o:4294967295:abcd"
    mkdir ones
    status=0
    errors=$(cd ones && "$bin/ar" xo ../ones.a a.o 2>&1) || status=$?
    check "ar xo of a member dated 4294967295 exits" 0 "$status"
    case $errors in
    *"a.o: utimes: time 4294967295 cannot be stored"*) seen=reported ;;
    *) seen="silent [$errors]" ;;
    esac
    check "ar xo reports a member dated 4294967295" reported "$seen"
    mtime=$(file_mtime ones/a.o)
    if [ "$mtime" -gt 1700000000 ] && [ "$mtime" -lt 4000000000 ]; then
        seen=current
    else
        seen=$mtime
    fi
    check "ar xo leaves a member dated 4294967295 at the extraction time" \
        current "$seen"

    # The listing prints the date through a time_t that holds it.
    if [ "$view" = i386-time32 ]; then
        echo "SKIP stored-times archive: $view: ar tv prints through a" \
            "four-byte time_t, which holds no date after January 2038"
    else
        listing=$(TZ=UTC "$bin/ar" tv read.a 2>&1 || echo "exit $?")
        for member in "2415919104 a.o" "4294967294 b.o"; do
            seconds=${member% *}
            expected="$(wallclock "$seconds") ${member#* }"
            case $listing in
            *"$expected"*) cases=$((cases + 1)) ;;
            *) note_failure "$view: ar tv lacks [$expected]: $listing" ;;
            esac
        done
    fi

    # x o restores the member date; the bits reach the signed interface.
    mkdir extract
    (cd extract && "$bin/ar" xo ../read.a a.o b.o) >/dev/null 2>&1 || true
    check "ar xo restores a.o time" "$(as_seen 2415919104)" \
        "$(file_mtime extract/a.o)"
    check "ar xo restores b.o time" "$(as_seen 4294967294)" \
        "$(file_mtime extract/b.o)"

    # u keeps a member newer than the file and replaces one older.
    craft_archive keep.a "a.o:2415919104:abcd"
    printf 'newer' > a.o
    set_times a.o 2000000000
    "$bin/ar" ru keep.a a.o >/dev/null 2>&1 || true
    check "ar ru keeps a member newer than the file" "a.o 2415919104" \
        "$(archive_dates keep.a)"
    set_times a.o 4000000000
    "$bin/ar" ru keep.a a.o >/dev/null 2>&1 || true
    check "ar ru replaces a member older than the file" "a.o 4000000000" \
        "$(archive_dates keep.a)"

    # x u skips a file newer than the member and replaces an older one.
    craft_archive xu.a "a.o:2415919104:abcd"
    mkdir xu
    printf 'keep' > xu/a.o
    set_times xu/a.o 4000000000
    (cd xu && "$bin/ar" xu ../xu.a a.o) >/dev/null 2>&1 || true
    check "ar xu keeps a file newer than the member" "keep" "$(cat xu/a.o)"
    set_times xu/a.o 2000000000
    (cd xu && "$bin/ar" xu ../xu.a a.o) >/dev/null 2>&1 || true
    check "ar xu replaces a file older than the member" "abcd" \
        "$(cat xu/a.o)"

    # The date ar writes for a file past 2038.
    mkdir write
    printf 'one' > write/m1.o
    printf 'two' > write/m2.o
    set_times write/m1.o 2415919104
    set_times write/m2.o 4294967294
    (cd write && "$bin/ar" rc ../write.a m1.o m2.o) >/dev/null 2>&1 || true
    check "ar r writes the stored count" "m1.o 2415919104
m2.o 4294967294" "$(archive_dates write.a 2>&1 || echo failed)"

    # The SOURCE_DATE_EPOCH bound runs to the end of the stored range.
    printf 'late' > write/late.o
    set_times write/late.o 4260211200
    printf 'last' > write/last.o
    set_times write/last.o 4294967294
    for row in "4200000000 late.o" "4294967290 last.o"; do
        epoch=${row% *}
        member=${row#* }
        rm -f bound.a
        (cd write && SOURCE_DATE_EPOCH=$epoch "$bin/ar" rc ../bound.a \
            "$member") >/dev/null 2>&1 || true
        check "SOURCE_DATE_EPOCH $epoch bounds $member" \
            "$member $epoch" "$(archive_dates bound.a 2>&1 || echo failed)"
    done
    rm -f bound.a
    (cd write && SOURCE_DATE_EPOCH=4294967296 "$bin/ar" rc ../bound.a \
        late.o) >/dev/null 2>&1 || true
    check "SOURCE_DATE_EPOCH above the stored range leaves the date" \
        "late.o 4260211200" "$(archive_dates bound.a 2>&1 || echo failed)"

    # ranlib reads members past 2038 and dates the table without overflow.
    write_objects
    cp l.o write/lib.o
    set_times write/lib.o 2415919104
    for pair in "3000000000 3000000003" "4294967290 4294967293" \
        "4294967294 4294967295" "4294967295 4294967295"; do
        now=${pair% *}
        expected=${pair#* }
        rm -f table.a
        (cd write && "$bin/ar" rc ../table.a lib.o) >/dev/null 2>&1 || true
        status=0
        STORED_TIMES_NOW=$now "$bin/ranlib" table.a >/dev/null 2>&1 ||
            status=$?
        check "ranlib at $now exits" 0 "$status"
        check "ranlib at $now dates the table" "__.SYMDEF $expected
lib.o 2415919104" "$(archive_dates table.a 2>&1 || echo failed)"
    done
    rm -f table.a
    (cd write && "$bin/ar" rc ../table.a lib.o) >/dev/null 2>&1 || true
    STORED_TIMES_NOW=4294967290 SOURCE_DATE_EPOCH=4294967000 \
        "$bin/ranlib" table.a >/dev/null 2>&1 || true
    check "ranlib bounded by SOURCE_DATE_EPOCH dates the table" \
        "__.SYMDEF 4294967003
lib.o 2415919104" "$(archive_dates table.a 2>&1 || echo failed)"
    set_times table.a 4294967294
    STORED_TIMES_NOW=4294967290 SOURCE_DATE_EPOCH=4294967000 \
        "$bin/ranlib" -t table.a >/dev/null 2>&1 || true
    check "ranlib -t sets the archive time to the bound" \
        "$(as_seen 4294967000)" "$(file_mtime table.a)"

    # ld treats an archive as sorted while its file time is at most two
    # seconds after the table date, whatever the date. The two rows that
    # straddle 2147483648 separate an unsigned compare from a signed one:
    # with both dates on one side of it the two orders agree.
    for row in "1700000000 3 warn" "2415919104 2 quiet" \
        "2147483646 3 warn" "2147483649 -3 quiet" \
        "2415919104 3 warn" "4294967292 3 warn" "4294967292 2 quiet" \
        "4294967294 1 quiet"; do
        set -- $row
        table=$1
        lead=$2
        verdict=$3
        rm -f ld.a
        (cd write && "$bin/ar" rc ../ld.a lib.o) >/dev/null 2>&1 || true
        "$bin/ranlib" ld.a >/dev/null 2>&1 || true
        set_table_date ld.a "$table"
        set_times ld.a $((table + lead))
        errors=$("$bin/ld" -o ld.out m.o ld.a 2>&1 || true)
        case $errors in
        *"out of date"*) seen=warn ;;
        *) seen=quiet ;;
        esac
        check "ld at table $table, file $lead seconds later" "$verdict" \
            "$seen"
    done

    # A table date ld cannot read as a stored count (a minus sign behind any
    # white space, a value beyond 4294967295, trailing text) makes the file
    # no archive, so the link fails; 64-bit and 32-bit longs agree.
    for bad in "-1" "$(printf '\t-1')" "$(printf '\r-1')" "4294967296" \
        "12abc" "99999999999"; do
        rm -f ld.a
        (cd write && "$bin/ar" rc ../ld.a lib.o) >/dev/null 2>&1 || true
        "$bin/ranlib" ld.a >/dev/null 2>&1 || true
        set_table_date ld.a "$bad"
        set_times ld.a 1700000000
        status=0
        "$bin/ld" -o ld.out m.o ld.a >/dev/null 2>&1 || status=$?
        if [ "$status" -ne 0 ]; then seen=refused; else seen=linked; fi
        check "ld refuses the table date [$bad]" refused "$seen"
    done

    # make rebuilds a target older than its dependency, by stored count.
    for row in "2000000000 2415919104 rebuilt" \
        "4294967290 2415919104 current" "2415919104 2000000000 current" \
        "2000000000 4294967290 rebuilt" "0 2415919104 rebuilt" \
        "4294967290 4294967294 rebuilt" "4294967294 4294967290 current"; do
        set -- $row
        check "make with target $1 and dependency $2" "$3" \
            "$("$bin/make_order" "$1" "$2" 2>&1 || echo "exit $?")"
    done

    cd "$work"
    [ "$failures" -eq "$suite_failures" ]
}

# mutate ROOT FILE OLD NEW: replace the one occurrence of OLD in ROOT/FILE.
# \n and \t in OLD and NEW stand for a newline and a tab.
mutate() {
    "$PYTHON" - "$@" <<'PY'
import codecs
import sys

root, relative, old, new = sys.argv[1:5]
old = codecs.decode(old, "unicode_escape")
new = codecs.decode(new, "unicode_escape")
path = root + "/" + relative
with open(path, encoding="utf-8") as handle:
    text = handle.read()
if text.count(old) != 1:
    raise SystemExit("mutation anchor count is %d, not 1" % text.count(old))
with open(path, "w", encoding="utf-8") as handle:
    handle.write(text.replace(old, new))
PY
}

# mutant_tree NAME: a writable copy of the three source directories.
mutant_tree() {
    mutant_root=$work/mutant-$1
    mkdir -p "$mutant_root/usr.bin/ar" "$mutant_root/usr.bin/ranlib" \
        "$mutant_root/usr.bin/ld" "$mutant_root/usr.bin/make"
    cp "$top"/usr.bin/ar/*.c "$top"/usr.bin/ar/*.h "$mutant_root/usr.bin/ar/"
    cp "$top"/usr.bin/ranlib/*.c "$mutant_root/usr.bin/ranlib/"
    cp "$top"/usr.bin/ld/*.c "$mutant_root/usr.bin/ld/"
    cp "$top"/usr.bin/make/defs.h "$top"/usr.bin/make/doname.c \
        "$mutant_root/usr.bin/make/"
}

views=
# STORED_TIMES_VIEWS restricts the views tried, so a host with a 32-bit libc
# can run the suite as a host without one does (STORED_TIMES_VIEWS=native).
for candidate in ${STORED_TIMES_VIEWS:-native i386-time32 i386-time64}; do
    if view_works "$candidate"; then
        views="$views $candidate"
    else
        echo "SKIP stored-times archive: $candidate: $cc cannot build and" \
            "run it (no 32-bit libc)"
    fi
done
if [ -z "$views" ]; then
    echo "FAIL stored-times archive: no view builds" >&2
    exit 1
fi

for view in $views; do
    out=$work/tools-$view
    if ! compile_tools "$view" "$tools_root" "$out"; then
        note_failure "$view: the archive tools do not build"
        continue
    fi
    run_suite "$view" "$out" || true
    # The path a mutant takes, with the unchanged build: it must pass, or a
    # mutant's failure would say nothing about the mutation.
    if ! (run_suite "$view" "$out") >/dev/null 2>&1; then
        note_failure "$view: the unchanged build fails a repeated suite run"
    fi
done

# Calibration: each mutant restores one signed or narrow reading and must
# fail a case. needs32 marks a mutant only a four-byte long exposes.
mutants=0
mutant_survivors=
[ "$tools_root" = "$top" ] || : > "$work/no-mutants"
while IFS='@' read -r name needs32 file old new; do
    [ -n "$name" ] || continue
    [ ! -e "$work/no-mutants" ] || continue
    case $views in
    *i386*) ;;
    *)
        if [ "$needs32" = yes ]; then
            echo "SKIP stored-times archive: mutant $name needs a" \
                "four-byte long (no 32-bit view)"
            continue
        fi
        ;;
    esac
    mutant_tree "$name"
    if ! mutate "$mutant_root" "$file" "$old" "$new"; then
        note_failure "mutant $name: its source anchor is missing"
        continue
    fi
    killed=no
    for view in $views; do
        out=$work/mutant-tools-$name-$view
        if ! compile_tools "$view" "$mutant_root" "$out" 2>/dev/null; then
            note_failure "mutant $name: does not build as $view"
            killed=error
            break
        fi
        if ! (run_suite "$view" "$out") >/dev/null 2>&1; then
            killed=yes
            break
        fi
    done
    mutants=$((mutants + 1))
    if [ "$killed" = no ]; then
        mutant_survivors="$mutant_survivors $name"
    fi
done <<'MUTANTS'
ar-parse-signed@yes@usr.bin/ar/archive.c@chdr.date = parse_archive_unsigned(hdr->ar_date, sizeof(hdr->ar_date),\n\t    DECIMAL);@chdr.date = (unsigned long)parse_archive_number(hdr->ar_date,\n\t    sizeof(hdr->ar_date), DECIMAL);
ar-epoch-bound@no@usr.bin/ar/archive.c@epoch <= AR_DATE_MAX &&\n\t\t    date > epoch@epoch <= 0x7fffffffULL &&\n\t\t    date > epoch
ar-minus-whitespace@yes@usr.bin/ar/archive.c@for (digits = number; *digits == ' ' || (*digits >= '\\t' &&\n\t    *digits <= '\\r'); digits++)@for (digits = number; *digits == ' '; digits++)
ar-extract-all-ones@no@usr.bin/ar/extract.c@chdr.date == AR_DATE_MAX)@chdr.date == AR_DATE_MAX + 1)
ld-date-unchecked@no@usr.bin/ld/ld.c@if (! fgetardate (buf, &h->ar_date))\n\t\tgoto failed;@h->ar_date = strtoul (buf, 0, 10); (void) fgetardate;
ar-extract-signed@yes@usr.bin/ar/extract.c@stored_date(sb.st_mtime) > chdr.date@sb.st_mtime > (time_t)chdr.date
ar-replace-signed@yes@usr.bin/ar/replace.c@stored_date(sb.st_mtime) <= chdr.date@sb.st_mtime <= (time_t)chdr.date
ar-write-signed@yes@usr.bin/ar/archive.h@#define\tHDR2\t"%-16.16s%-12lu@#define\tHDR2\t"%-16.16s%-12ld
ranlib-no-clamp@no@usr.bin/ranlib/ranlib.c@if (date > AR_DATE_MAX - RANLIBSKEW)\n\t\tdate = AR_DATE_MAX;\n\telse\n\t\tdate += RANLIBSKEW;@date += RANLIBSKEW;
ranlib-epoch-bound@no@usr.bin/ranlib/ranlib.c@epoch <= AR_DATE_MAX &&\n\t\t    now > epoch@epoch <= 0x7fffffffULL &&\n\t\t    now > epoch
ld-parse-signed@yes@usr.bin/ld/ld.c@value = strtoul (text, &end, 10);@value = (unsigned long) strtol (text, &end, 10);
ld-sum-wraps@yes@usr.bin/ld/ld.c@if (stored_time (x.st_mtime) > archdr.ar_date &&\n\t    stored_time (x.st_mtime) - archdr.ar_date > 2)@if (stored_time (x.st_mtime) > archdr.ar_date + 2)
ld-signed-compare@yes@usr.bin/ld/ld.c@if (stored_time (x.st_mtime) > archdr.ar_date &&\n\t    stored_time (x.st_mtime) - archdr.ar_date > 2)@if ((long) stored_time (x.st_mtime) > (long) archdr.ar_date + 2)
make-signed-time@yes@usr.bin/make/defs.h@typedef unsigned long TIMETYPE;@typedef time_t TIMETYPE;
MUTANTS

if [ -n "$mutant_survivors" ]; then
    note_failure "mutants survived every view:$mutant_survivors"
fi
if [ "$failures" -ne 0 ]; then
    echo "FAIL stored-times archive: $failures of $cases checks failed" >&2
    exit 1
fi
echo "PASS stored-times archive: $cases checks over$views;" \
    "$mutants source mutants each fail a check"
