#!/bin/sh
# Calibration for the time64 wrapper test: each mutant below changes one
# conversion in a copy of lib/libc/gen/time64*, and the test built over
# that copy must fail. A mutation that does not change its file is an
# error, so a mutant cannot pass by leaving the source as it was.
#
# Before the mutants, each wrapper member of the good build is read with
# nm: time64_NAME.o must define the one symbol __NAME64. A member whose
# source misses the header that binds its name would define NAME itself,
# collide with the 32-bit entry point and leave __NAME64 undefined.
#
# usage: check.sh MAKE QEMU_ARM_RUN NM
set -eu

make=$1
run=$2
nm=$3
here=$(cd "$(dirname "$0")" && pwd)
source=$(cd "$here/../../lib/libc/gen" && pwd)
copy=$here/mutant-src
failures=0

cleanup() {
	rm -f "$copy"/time64* "$here"/mutant_*.o "$here/mutant_time64_test_arm" \
	    "$here/mutant.out"
	if [ -d "$copy" ]; then rmdir "$copy"; fi
}
trap cleanup EXIT

# expect_rejected NAME FILE SED-EXPRESSION
expect_rejected() {
	name=$1
	file=$2
	expression=$3
	cleanup
	mkdir "$copy"
	cp "$source"/time64.h "$source"/time64_*.c "$copy"/
	sed "$expression" "$source/$file" >"$copy/$file"
	if cmp -s "$source/$file" "$copy/$file"; then
		echo "time64 wrappers: FAIL: $name mutation did not apply"
		failures=$((failures + 1))
		return
	fi
	if ! (cd "$here" && $make TIME64_SRC="$copy" PREFIX=mutant \
	    mutant_time64_test_arm) >"$here/mutant.out" 2>&1; then
		cat "$here/mutant.out"
		echo "time64 wrappers: FAIL: $name mutant did not build"
		failures=$((failures + 1))
		return
	fi
	if (cd "$here" && $run ./mutant_time64_test_arm) >"$here/mutant.out" 2>&1; then
		echo "time64 wrappers: FAIL: $name mutant passed"
		failures=$((failures + 1))
	else
		echo "time64 wrappers: $name mutant rejected"
	fi
}

for object in "$here"/good_time64_*.o; do
	name=${object##*/good_time64_}
	name=${name%.o}
	case $name in
	widen_*) want=__time64_$name ;;
	*) want=__${name}64 ;;
	esac
	defined=$("$nm" -g --defined-only "$object" | awk '{ print $3 }' | tr '\n' ' ')
	if [ "$defined" != "$want " ]; then
		echo "time64 wrappers: FAIL: time64_$name.o defines [$defined], want [$want]"
		failures=$((failures + 1))
	fi
done
if [ "$failures" -eq 0 ]; then
	echo "time64 wrappers: every member defines its one symbol"
fi

expect_rejected epoch-sign-extended time64.h \
    's/return (time_t)(utime32_t)seconds;/return (time_t)seconds;/'
expect_rejected epoch-range-widened time64.h \
    's/return seconds >= 0 \&\& seconds <= 0xffffffffLL;/return seconds >= -1 \&\& seconds <= 0x1ffffffffLL;/'
expect_rejected interval-range-widened time64.h \
    's/return seconds >= -0x7fffffffLL - 1 \&\& seconds <= 0x7fffffffLL;/return seconds >= -0xffffffffLL \&\& seconds <= 0xffffffffLL;/'
expect_rejected interval-zero-extended time64.h \
    's/wide->tv_sec = narrow->tv_sec;/wide->tv_sec = (utime32_t)narrow->tv_sec;/'
expect_rejected stat-time-sign-extended time64_widen_stat.c \
    's/wide->st_mtime = (time_t)narrow->st_mtime;/wide->st_mtime = (time_t)(long)narrow->st_mtime;/'
expect_rejected rusage-counters-dropped time64_widen_rusage.c \
    's/sizeof(struct __rusage32) - offsetof(struct __rusage32, ru_maxrss));/sizeof(long));/'
expect_rejected pselect-negative-timeout time64_pselect.c \
    's/ || timeout->tv_sec < 0) {/) {/'
expect_rejected wait4-fills-without-a-child time64_wait4.c \
    's/if (result > 0 \&\& usage)/if (usage)/'
expect_rejected failed-stat-overwrites time64_stat.c \
    's/if (result == 0)/if (1)/'

if [ "$failures" -ne 0 ]; then
	echo "time64 wrappers: $failures calibration failures"
	exit 1
fi
echo "time64 wrappers: every mutant rejected"
