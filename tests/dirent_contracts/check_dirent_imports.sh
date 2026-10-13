#!/bin/sh
# Cross imports bind admission calls and allocation-free directory cookies.
set -eu

check_object()
{
    inspector=$1
    object=$2
    kind=$3
    imports=$("$inspector" -u "$object") || {
        echo "dirent: $inspector inspection failed" >&2
        return 2
    }
    case $kind in
    opendir)
        for symbol in fstat fcntl; do
            status=0
            printf '%s\n' "$imports" |
                grep -E "[[:space:]]$symbol\$" >/dev/null || status=$?
            if [ "$status" -eq 1 ]; then
                echo "dirent: opendir misses $symbol" >&2
                return 1
            elif [ "$status" -ne 0 ]; then
                echo "dirent: import pattern inspection failed" >&2
                return 2
            fi
        done
        ;;
    telldir)
        status=0
        printf '%s\n' "$imports" |
            grep -E '[[:space:]]lseek$' >/dev/null || status=$?
        if [ "$status" -eq 0 ]; then
            echo "dirent: telldir imports lseek" >&2
            return 1
        elif [ "$status" -ne 1 ]; then
            echo "dirent: import pattern inspection failed" >&2
            return 2
        fi
        ;;
    *) return 2 ;;
    esac
}

good_opendir() { printf '         U fstat\n         U fcntl\n'; }
good_telldir() { printf '         U errno\n'; }
missing_fstat() { printf '         U fcntl\n'; }
missing_fcntl() { printf '         U fstat\n'; }
bad_telldir() { printf '         U lseek\n'; }
failed_inspector() { return 2; }

expect_rejection()
{
    expected=$1
    inspector=$2
    kind=$3
    status=0
    check_object "$inspector" calibration "$kind" 2>/dev/null || status=$?
    if [ "$status" -ne "$expected" ]; then
        echo "dirent: $inspector returns $status, expected $expected" >&2
        exit 1
    fi
}

if [ "$#" -eq 1 ] && [ "$1" = --self-test ]; then
    check_object good_opendir calibration opendir
    check_object good_telldir calibration telldir
    expect_rejection 1 missing_fstat opendir
    expect_rejection 1 missing_fcntl opendir
    expect_rejection 1 bad_telldir telldir
    expect_rejection 2 failed_inspector opendir
    expect_rejection 2 failed_inspector telldir
    echo "PASS dirent import inspection calibration"
elif [ "$#" -eq 3 ]; then
    check_object "$1" "$2" opendir
    check_object "$1" "$3" telldir
else
    echo "usage: check_dirent_imports.sh --self-test | NM OPENDIR TELLDIR" >&2
    exit 2
fi
