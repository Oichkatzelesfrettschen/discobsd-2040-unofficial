#!/bin/sh
# Each mutant of win.c, written by win_mutate.py, must be rejected by win_test
# with exit status 1 and a FAIL diagnostic; the baseline copy, which carries
# no fault, must pass. A mutant that passes, crashes, hangs past the time
# bound or fails to build means the gate cannot see that fault.
# usage: check_mutants.sh LIBWIN_DIRECTORY FONT_DIRECTORY
# The environment supplies HOSTCC and BASE_FLAGS.
set -eu

: "${HOSTCC:?set HOSTCC to the host C compiler}"
: "${BASE_FLAGS:?set BASE_FLAGS to the host compile flags}"
libdir=$1
fontdir=$2
count=0

for source in win_*.mutant.c; do
    if [ ! -e "$source" ]; then
        echo "win mutants: no mutant sources found" >&2
        exit 1
    fi
    name=${source%.mutant.c}
    # BASE_FLAGS is a word list by construction.
    # shellcheck disable=SC2086
    "$HOSTCC" $BASE_FLAGS -I"$fontdir" -I"$libdir" -o "$name.mutant.test" \
        win_test.c "$source" "$fontdir/lcdfont.c"
    status=0
    timeout 120 "./$name.mutant.test" > "$name.mutant.log" 2>&1 || status=$?
    if [ "$status" -eq 124 ]; then
        echo "win mutants: $name did not finish within 120 seconds" >&2
        exit 1
    fi
    if [ "$name" = win_baseline ]; then
        if [ "$status" -ne 0 ]; then
            echo "win mutants: the unfaulted baseline exited $status" >&2
            tail -n 3 "$name.mutant.log" >&2
            exit 1
        fi
        echo "PASS libwin baseline accepted"
        continue
    fi
    if [ "$status" -ne 1 ]; then
        echo "win mutants: $name exited $status, expected 1" >&2
        exit 1
    fi
    if ! grep -q '^FAIL ' "$name.mutant.log"; then
        echo "win mutants: $name printed no FAIL diagnostic" >&2
        exit 1
    fi
    echo "PASS libwin mutation $name rejected: $(grep -m1 '^FAIL ' "$name.mutant.log" | cut -c1-72)"
    count=$((count + 1))
done
echo "win mutants: $count rejected, baseline accepted"
