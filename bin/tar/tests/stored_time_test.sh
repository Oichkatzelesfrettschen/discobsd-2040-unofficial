#!/bin/sh
#
# stored_time_test.sh
#
# tar reads and writes the mtime field of a header, and orders file times for
# the u modifier, as the unsigned 32-bit count the kernel and the disk store:
# valid through 4294967295 (February 2106). The exact tar source is built as
# strict C17 for three views of time and one suite runs against each:
#
#   native         64-bit time_t and long
#   i386-time32    four-byte signed time_t and long, the target default; the
#                  stat family comes from ../../../tests/stored_times/
#                  stat_shim.c, which returns a time after 2038 as the
#                  negative number the kernel returns
#   i386-time64    four-byte long, 64-bit time_t, the target with
#                  -D_TIME_BITS=64
#
# A view the host compiler cannot build and run (no 32-bit libc) reports
# SKIP. The cases cover listing and extraction of members dated 2415919104,
# 3000000000 (a directory), 4294967294 and 4294967295, rejection of a header
# time above 4294967295, creation from files dated past 2038 and refusal of
# a file time outside the stored range, and the u modifier's ordering of a
# file against an archived entry. Seven source mutants, each restoring a signed
# or narrow reading, must each fail a case; a mutant that passes every view
# fails the gate. TAR_TEST_SOURCE names another tar.c (an earlier revision)
# to run the suite against, which skips the mutants.
#
# Output: one PASS line, or FAIL lines on standard error and exit status 1.
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

here=$(cd "$(dirname "$0")" && pwd)
shim=$here/../../../tests/stored_times/stat_shim.c
cc=${CC:-cc}
tar_source=${TAR_TEST_SOURCE:-$here/../tar.c}

work=$(mktemp -d "${TMPDIR:-/tmp}/tar-stored-time.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

failures=0
cases=0
note_failure() {
    echo "FAIL tar stored time: $*" >&2
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

# The four-byte time_t view reads file status through the shim.
view_defines() {
    if [ "$1" = i386-time32 ]; then
        echo "-Dstat(p,b)=stored_times_stat(p,b)" \
            "-Dlstat(p,b)=stored_times_lstat(p,b)" \
            "-Dfstat(d,b)=stored_times_fstat(d,b)"
    fi
}

# Only the four-byte time_t view calls the shim's statx() half.
shim_defines() {
    if [ "$1" != i386-time32 ]; then
        echo "-DSTORED_TIMES_NO_STATX"
    fi
}

view_works() {
    probe_flags=$(view_flags "$1")
    cat > "$work/probe.c" <<'EOF'
#include <time.h>
int main(void) { return sizeof(time_t) > 0 ? 0 : 1; }
EOF
    # shellcheck disable=SC2086
    $cc $probe_flags -o "$work/probe" "$work/probe.c" 2>/dev/null &&
        "$work/probe" 2>/dev/null
}

# compile_tar VIEW SOURCE OUTPUT
compile_tar() {
    ct_view=$1
    ct_source=$2
    ct_output=$3
    # shellcheck disable=SC2046
    $cc $(view_flags "$ct_view") $(view_defines "$ct_view") \
        -D_DEFAULT_SOURCE -DTAR_PATH_LIMIT=256 -std=c17 -O1 \
        -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
        -Wold-style-definition -Wconversion -Wsign-conversion \
        -include "$here/rooted_link_host.h" \
        -o "$ct_output" "$ct_source" "$here/rooted_link_host.c" "$shim_object"
}

# craft_archive PATH NAME:TYPE:MTIME:BODY...: a ustar archive written
# without tar, so the read cases do not depend on the writer under test. The
# mtime field is eleven octal digits, whatever the value.
craft_archive() {
    "$PYTHON" - "$@" <<'PY'
import sys

blocks = b""
for spec in sys.argv[2:]:
    name, kind, mtime, body = spec.split(":")
    data = body.encode()
    header = bytearray(512)
    header[0:len(name)] = name.encode()
    header[100:108] = b"0000755\0" if kind == "5" else b"0000644\0"
    header[108:116] = b"0000000\0"
    header[116:124] = b"0000000\0"
    header[124:136] = ("%011o\0" % len(data)).encode()
    header[136:148] = ("%011o\0" % int(mtime)).encode()
    header[148:156] = b"        "
    header[156:157] = kind.encode()
    header[257:263] = b"ustar\0"
    header[263:265] = b"00"
    header[148:156] = ("%06o\0 " % sum(header)).encode()
    blocks += bytes(header)
    if data:
        blocks += data + bytes(-len(data) % 512)
blocks += bytes(1024)
with open(sys.argv[1], "wb") as handle:
    handle.write(blocks)
PY
}

# archive_mtimes PATH: one "name mtime" line per member, read from the raw
# headers.
archive_mtimes() {
    "$PYTHON" - "$1" <<'PY'
import sys

with open(sys.argv[1], "rb") as handle:
    data = handle.read()
position = 0
while position + 512 <= len(data) and data[position] != 0:
    header = data[position:position + 512]
    name = header[:100].split(b"\0")[0].decode()
    size = int(header[124:135], 8)
    print(name, int(header[136:147], 8))
    position += 512 + ((size + 511) // 512) * 512
PY
}

set_times() {
    "$PYTHON" -c 'import os, sys; os.utime(sys.argv[1], (int(sys.argv[2]), int(sys.argv[2])))' "$1" "$2"
}

file_mtime() {
    "$PYTHON" -c 'import os, sys; print(os.stat(sys.argv[1]).st_mtime_ns // 1000000000)' "$1"
}

# as_seen N: the second count a four-byte signed time_t passes to utimes()
# and reads back for N on this host.
as_seen() {
    if [ "$view" = i386-time32 ]; then
        echo $(($1 - 4294967296))
    else
        echo "$1"
    fi
}

# ctime_text N: the month, day and time-of-day columns and the year of the
# listing, in UTC.
ctime_text() {
    "$PYTHON" -c 'import sys, time; t = time.asctime(time.gmtime(int(sys.argv[1]))); print(t[4:16], t[20:24])' "$1"
}

run_suite() {
    view=$1
    tar=$2
    suite=$work/suite-$view
    mkdir -p "$suite"
    cd "$suite"
    suite_failures=$failures

    craft_archive list.a "d/:5:3000000000:" "d/f:0:2415919104:abc" \
        "g:0:4294967294:def" "h:0:4294967295:ghi"
    check "tar t lists members past 2038" "d
d/f
g
h" "$("$tar" tf list.a 2>&1 || echo "exit $?")"

    if [ "$view" = i386-time32 ]; then
        echo "SKIP tar stored time: $view: tar tv prints through a" \
            "four-byte time_t, which holds no date after January 2038"
    else
        listing=$(TZ=UTC "$tar" tvf list.a 2>&1 || echo "exit $?")
        for seconds in 2415919104 4294967294 4294967295; do
            case $listing in
            *"$(ctime_text "$seconds")"*) cases=$((cases + 1)) ;;
            *) note_failure "$view: tar tv lacks the date of $seconds:" \
                "$listing" ;;
            esac
        done
    fi

    craft_archive over.a "g:0:4294967296:def"
    status=0
    "$tar" tf over.a >/dev/null 2>&1 || status=$?
    check "tar t refuses a time above 4294967295" 2 "$status"

    # Extraction passes the count to the file-time interface; a directory
    # takes its time when extraction leaves it.
    craft_archive extract.a "d/:5:3000000000:" "d/f:0:2415919104:abc" \
        "g:0:4294967294:def"
    mkdir extract
    status=0
    (cd extract && "$tar" xf ../extract.a) >/dev/null 2>&1 || status=$?
    check "tar x exits" 0 "$status"
    check "tar x sets a file time past 2038" "$(as_seen 2415919104)" \
        "$(file_mtime extract/d/f 2>/dev/null || echo none)"
    check "tar x sets the time of 4294967294" "$(as_seen 4294967294)" \
        "$(file_mtime extract/g 2>/dev/null || echo none)"
    check "tar x sets a directory time past 2038" "$(as_seen 3000000000)" \
        "$(file_mtime extract/d 2>/dev/null || echo none)"

    # A member dated 4294967295 is the kernel's "leave this time alone", so a
    # four-byte view silently keeps the extraction time and the 64-bit view
    # refuses it. tar reports the member and fails the operation in every
    # view without calling the time-setting function; the file keeps a
    # time of this decade.
    craft_archive ones.a "d/:5:4294967295:" "d/f:0:4294967295:abc"
    mkdir ones
    status=0
    errors=$(cd ones && "$tar" xf ../ones.a 2>&1) || status=$?
    check "tar x of members dated 4294967295 fails the operation" 1 "$status"
    case $errors in
    *"cannot set modification time on d/f: the count 4294967295 cannot be stored"*)
        seen=reported ;;
    *) seen="silent [$errors]" ;;
    esac
    check "tar x names a file dated 4294967295" reported "$seen"
    for member in ones/d/f ones/d; do
        mtime=$(file_mtime "$member" 2>/dev/null || echo none)
        if [ "$mtime" != none ] && [ "$mtime" -gt 1700000000 ] &&
            [ "$mtime" -lt 4000000000 ]; then
            seen=current
        else
            seen=$mtime
        fi
        check "tar x leaves $member at the extraction time" current "$seen"
    done

    # Creation writes the stored count.
    mkdir create
    mkdir create/dir
    printf 'one' > create/f1
    printf 'two' > create/f2
    set_times create/f1 2415919104
    set_times create/f2 4294967294
    set_times create/dir 3000000000
    status=0
    (cd create && "$tar" cf ../create.tar dir f1 f2) >/dev/null 2>&1 ||
        status=$?
    check "tar c exits with files dated past 2038" 0 "$status"
    check "tar c writes the stored count" "dir/ 3000000000
f1 2415919104
f2 4294967294" "$(archive_mtimes create.tar 2>&1 || echo failed)"

    # A time outside the stored range has no header form: tar refuses it.
    if [ "$view" = i386-time32 ]; then
        echo "SKIP tar stored time: $view: a four-byte time_t holds no" \
            "time beyond the stored range"
    else
        mkdir range
        printf 'x' > range/far
        set_times range/far 5000000000
        status=0
        (cd range && "$tar" cf ../range.tar far) >/dev/null 2>&1 ||
            status=$?
        check "tar c refuses a time beyond 4294967295" 2 "$status"
    fi

    # u appends a file newer than its archived entry and skips an older one.
    mkdir update
    printf 'v1' > update/f
    set_times update/f 2000000000
    (cd update && "$tar" cf ../update.tar f) >/dev/null 2>&1 || true
    set_times update/f 2415919104
    (cd update && "$tar" uf ../update.tar f) >/dev/null 2>&1 || true
    check "tar u appends a file dated past 2038" "f 2000000000
f 2415919104" "$(archive_mtimes update.tar 2>&1 || echo failed)"
    set_times update/f 2000000000
    (cd update && "$tar" uf ../update.tar f) >/dev/null 2>&1 || true
    check "tar u skips a file older than a time past 2038" "f 2000000000
f 2415919104" "$(archive_mtimes update.tar 2>&1 || echo failed)"
    set_times update/f 4294967294
    (cd update && "$tar" uf ../update.tar f) >/dev/null 2>&1 || true
    check "tar u appends a file dated 4294967294" "f 2000000000
f 2415919104
f 4294967294" "$(archive_mtimes update.tar 2>&1 || echo failed)"

    cd "$work"
    [ "$failures" -eq "$suite_failures" ]
}

# mutate_source SOURCE OUTPUT OLD NEW: OUTPUT is SOURCE with the one
# occurrence of OLD replaced. \n and \t in OLD and NEW stand for a newline
# and a tab.
mutate_source() {
    "$PYTHON" - "$@" <<'PY'
import codecs
import sys

source, output, old, new = sys.argv[1:5]
old = codecs.decode(old, "unicode_escape")
new = codecs.decode(new, "unicode_escape")
with open(source, encoding="utf-8") as handle:
    text = handle.read()
if text.count(old) != 1:
    raise SystemExit("mutation anchor count is %d, not 1" % text.count(old))
with open(output, "w", encoding="utf-8") as handle:
    handle.write(text.replace(old, new))
PY
}

views=
# STORED_TIMES_VIEWS restricts the views tried, so a host with a 32-bit libc
# can run the suite as a host without one does (STORED_TIMES_VIEWS=native).
for candidate in ${STORED_TIMES_VIEWS:-native i386-time32 i386-time64}; do
    if view_works "$candidate"; then
        views="$views $candidate"
    else
        echo "SKIP tar stored time: $candidate: $cc cannot build and" \
            "run it (no 32-bit libc)"
    fi
done
if [ -z "$views" ]; then
    echo "FAIL tar stored time: no view builds" >&2
    exit 1
fi

for view in $views; do
    # The shim is compiled for the view, since its types follow it.
    # shellcheck disable=SC2046
    shim_object=$work/shim-$view.o
    # shellcheck disable=SC2046
    $cc $(view_flags "$view") $(shim_defines "$view") -c -o "$shim_object" \
        "$shim" ||
        { note_failure "$view: the stat shim does not build"; continue; }
    tar_binary=$work/tar-$view
    if ! compile_tar "$view" "$tar_source" "$tar_binary"; then
        note_failure "$view: tar.c does not build"
        continue
    fi
    run_suite "$view" "$tar_binary" || true
done

mutants=0
mutant_survivors=
[ "$tar_source" = "$here/../tar.c" ] || : > "$work/no-mutants"
while IFS='#' read -r name needs32 old new; do
    [ -n "$name" ] || continue
    [ ! -e "$work/no-mutants" ] || continue
    case $views in
    *i386*) ;;
    *)
        if [ "$needs32" = yes ]; then
            echo "SKIP tar stored time: mutant $name needs a four-byte" \
                "long (no 32-bit view)"
            continue
        fi
        ;;
    esac
    mutate_source "$tar_source" "$work/mutant-$name.c" "$old" "$new" ||
        { note_failure "mutant $name: its source anchor is missing"; continue; }
    killed=no
    for view in $views; do
        shim_object=$work/shim-$view.o
        if ! compile_tar "$view" "$work/mutant-$name.c" \
            "$work/mutant-$name-$view" >/dev/null 2>&1; then
            note_failure "mutant $name: does not build as $view"
            killed=error
            break
        fi
        if ! (run_suite "$view" "$work/mutant-$name-$view") >/dev/null 2>&1
        then
            killed=yes
            break
        fi
    done
    mutants=$((mutants + 1))
    if [ "$killed" = no ]; then
        mutant_survivors="$mutant_survivors $name"
    fi
done <<'MUTANTS'
create-negative#yes#!file_time_is_stored(sp->st_mtime)#(sp->st_mtime < 0 || file_time_is_stored(sp->st_mtime) == 2)
list-long-max#yes#!stored_time_in_range(time_value) ||#time_value > LONG_MAX ||
header-unbounded#no#!stored_time_in_range(time_value) ||#0 ||
range-unchecked#no#return seconds >= 0 &&\n            stored_time_in_range((unsigned long long)seconds);#return seconds != 0 || seconds == 0;
set-all-ones#no#modification_time == STORED_TIME_MAX#modification_time == STORED_TIME_MAX + 1
update-signed#yes#stored_time(stbuf.st_mtime) > newest#stbuf.st_mtime > (time_t)newest
directory-time-dropped#no#metadata.restore_time = mflag == 0;#metadata.restore_time = 0;
MUTANTS

if [ -n "$mutant_survivors" ]; then
    note_failure "mutants survived every view:$mutant_survivors"
fi
if [ "$failures" -ne 0 ]; then
    echo "FAIL tar stored time: $failures of $cases checks failed" >&2
    exit 1
fi
echo "PASS tar stored time: $cases checks over$views;" \
    "$mutants source mutants each fail a check"
