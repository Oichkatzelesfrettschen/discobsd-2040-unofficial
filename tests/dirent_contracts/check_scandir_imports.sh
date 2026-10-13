#!/bin/sh
# Inspect imports only after the inspector returns a successful result.
set -eu

check_imports()
{
    inspector=$1
    object=$2
    imports=$("$inspector" -u "$object") || {
        echo "scandir: $inspector inspection failed" >&2
        return 2
    }
    inspection_status=0
    printf '%s\n' "$imports" |
        grep -E '[[:space:]](qsort|__aeabi_(idiv|idivmod|uidiv|uidivmod)|__divsi3|__udivsi3)$' >/dev/null || inspection_status=$?
    case $inspection_status in
    0)
        echo "scandir: target object retains qsort or integer division" >&2
        return 1
        ;;
    1) return 0 ;;
    *)
        echo "scandir: import pattern inspection failed" >&2
        return 2
        ;;
    esac
}

allowed_inspector()
{
    printf '         U malloc\n         U strcmp\n'
}

forbidden_inspector()
{
    printf '         U %s\n' "$forbidden_symbol"
}

failed_inspector()
{
    return 2
}

if [ "$#" -eq 1 ] && [ "$1" = --self-test ]; then
    check_imports allowed_inspector calibration
    for forbidden_symbol in qsort __aeabi_idiv __aeabi_idivmod \
        __aeabi_uidiv __aeabi_uidivmod __divsi3 __udivsi3; do
        status=0
        check_imports forbidden_inspector calibration 2>/dev/null || status=$?
        if [ "$status" -ne 1 ]; then
            echo "scandir: import check misses $forbidden_symbol" >&2
            exit 1
        fi
    done
    status=0
    check_imports failed_inspector calibration 2>/dev/null || status=$?
    if [ "$status" -ne 2 ]; then
        echo "scandir: import check accepts a failed inspector" >&2
        exit 1
    fi
    echo "PASS scandir import inspection calibration"
elif [ "$#" -eq 2 ]; then
    check_imports "$1" "$2"
else
    echo "usage: check_scandir_imports.sh --self-test | NM OBJECT" >&2
    exit 2
fi
