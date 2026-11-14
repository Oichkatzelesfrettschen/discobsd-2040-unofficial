#!/bin/sh
# Symbol and mutant calibration for the 64-bit calendar test.
#
# Each member of the good build is read with nm first. A 64-bit member
# (NAME64.o) must define exactly its __NAME64 symbols, import none of the
# compiler's 64-bit multiply, divide or arithmetic-shift helpers, which
# libc does not carry (check-imports), and reference no symbol of the
# 32-bit interface; a 32-bit member (NAME32.o) must reference no __NAME64
# symbol. Two calibration objects prove the reading: ctime.c built without
# the view defines the 32-bit names, and a 64-bit product imports
# __aeabi_lmul; each must be rejected.
#
# Each mutant then changes one rule in a copy of ctime.c, mktime.c,
# difftime.c or offtime.h, and the test built over that copy must fail. A
# mutation that does not change its file is an error, so a mutant cannot
# pass by leaving the source as it was.
#
# usage: check.sh MAKE QEMU_ARM_RUN NM "CC CFLAGS"
set -eu

make=$1
run=$2
nm=$3
compile=$4
here=$(cd "$(dirname "$0")" && pwd)
source=$(cd "$here/../../lib/libc/gen" && pwd)
copy=$here/mutant-src
failures=0

cleanup() {
	rm -f "$copy"/*.c "$copy"/*.h "$here"/mutant_*.o \
	    "$here/mutant_calendar64_test_arm" "$here/mutant.out" \
	    "$here"/calibration_*.o "$here/calibration.c"
	if [ -d "$copy" ]; then rmdir "$copy"; fi
}
trap cleanup EXIT

# The 32-bit interface a 64-bit member must not reach. tzset, tzname and
# asctime read no time_t and serve both widths.
narrow_names='ctime localtime gmtime mktime timegm time difftime wait3 wait4
utime utimes ftime gettimeofday __discobsd_offtime'
helpers='__aeabi_lmul __aeabi_ldivmod __aeabi_uldivmod __aeabi_lasr
__muldi3 __divdi3 __udivdi3 __moddi3 __umoddi3 __ashrdi3'

# wide_verdict OBJECT WANT: 0 when the object defines exactly WANT and
# references nothing of the 32-bit interface and no 64-bit helper.
wide_verdict() {
	object=$1
	want=$2
	defined=$("$nm" -g --defined-only "$object" | awk '{ print $3 }' |
	    sort | tr '\n' ' ')
	if [ "$defined" != "$want" ]; then
		echo "time64 calendar: ${object##*/} defines [$defined], want [$want]"
		return 1
	fi
	undefined=$("$nm" -u "$object" | awk '{ print $2 }')
	for name in $narrow_names $helpers; do
		for symbol in $undefined; do
			if [ "$symbol" = "$name" ]; then
				echo "time64 calendar: ${object##*/} references $name"
				return 1
			fi
		done
	done
	return 0
}

# expect_wide MEMBER SYMBOL...: the good 64-bit member must pass.
expect_wide() {
	member=$1
	shift
	want=$(printf '%s\n' "$@" | sort | tr '\n' ' ')
	if ! wide_verdict "$here/good_${member}64.o" "$want"; then
		failures=$((failures + 1))
	fi
}

expect_wide ctime __ctime64 __discobsd_offtime64 __gmtime64 __localtime64
expect_wide mktime __mktime64 __timegm64
expect_wide difftime __difftime64
expect_wide time __time64
expect_wide wait3 __wait364
expect_wide utime __utime64
expect_wide ftime __ftime64

for object in "$here"/good_*32.o; do
	wide=$("$nm" "$object" | awk '$NF ~ /^__.*64$/ { print $NF }')
	if [ -n "$wide" ]; then
		echo "time64 calendar: ${object##*/} references $wide"
		failures=$((failures + 1))
	fi
done

cleanup
# shellcheck disable=SC2086 # $compile is a compiler and its flags
$compile -D_TIME_BITS=32 -c -o "$here/calibration_narrow.o" "$source/ctime.c"
if wide_verdict "$here/calibration_narrow.o" \
    "__ctime64 __discobsd_offtime64 __gmtime64 __localtime64 " >/dev/null; then
	echo "time64 calendar: FAIL: a 32-bit ctime.o passed the symbol check"
	failures=$((failures + 1))
fi
printf 'long long product(long long a, long long b) { return a * b; }\n' \
    >"$here/calibration.c"
# shellcheck disable=SC2086
$compile -D_TIME_BITS=64 -c -o "$here/calibration_product.o" \
    "$here/calibration.c"
if wide_verdict "$here/calibration_product.o" "product " >/dev/null; then
	echo "time64 calendar: FAIL: a 64-bit product passed the import check"
	failures=$((failures + 1))
fi
if [ "$failures" -eq 0 ]; then
	echo "time64 calendar: every member defines its symbols and imports no 64-bit helper; both calibrations rejected"
fi

# expect_rejected NAME FILE SED-EXPRESSION
expect_rejected() {
	name=$1
	file=$2
	expression=$3
	cleanup
	mkdir "$copy"
	cp "$source"/ctime.c "$source"/mktime.c "$source"/difftime.c \
	    "$source"/tzset.c "$source"/offtime.h "$source"/tzstate.h "$copy"/
	sed "$expression" "$source/$file" >"$copy/$file"
	if cmp -s "$source/$file" "$copy/$file"; then
		echo "time64 calendar: FAIL: $name mutation did not apply"
		failures=$((failures + 1))
		return
	fi
	if ! (cd "$here" && $make CAL_SRC="$copy" PREFIX=mutant \
	    mutant_calendar64_test_arm) >"$here/mutant.out" 2>&1; then
		cat "$here/mutant.out"
		echo "time64 calendar: FAIL: $name mutant did not build"
		failures=$((failures + 1))
		return
	fi
	if (cd "$here" && $run ./mutant_calendar64_test_arm) >"$here/mutant.out" 2>&1; then
		echo "time64 calendar: FAIL: $name mutant passed"
		failures=$((failures + 1))
	else
		echo "time64 calendar: $name mutant rejected"
	fi
}

expect_rejected day-split-divisor ctime.c \
    's|divide_small(&count, SECS_PER_DAY / 128) << 7;|divide_small(\&count, SECS_PER_DAY / 128 + 1) << 7;|'
expect_rejected day-split-low-bits ctime.c \
    's/second = (uint32_t)count & 127U;/second = (uint32_t)count \& 63U;/'
expect_rejected era-factor offtime.h \
    's/^#define CAL64_ERA_FACTOR_HIGH\t773UL$/#define CAL64_ERA_FACTOR_HIGH\t772UL/'
expect_rejected era-bias-dropped ctime.c \
    's/count = (uint64_t)(\*timestamp + offset) + CAL64_BIAS_DAYS \* 86400ULL;/count = (uint64_t)(*timestamp + offset);/'
expect_rejected leap-era-start ctime.c \
    's/(year_of_era % 100 != 0 || year_of_era == 0);/(year_of_era % 100 != 0);/'
expect_rejected negative-remainder-sign mktime.c \
    's/if (\*rest < 0) {/if (*rest < 0 \&\& 0) {/'
expect_rejected range-max-off-by-one offtime.h \
    's/^#define CAL64_MAX\t\t67768036191676799LL$/#define CAL64_MAX\t\t67768036191676800LL/'
expect_rejected range-min-off-by-one offtime.h \
    's/^#define CAL64_MIN\t\t(-67768040609740800LL)$/#define CAL64_MIN\t\t(-67768040609740799LL)/'
expect_rejected difftime-rounds-each-end difftime.c \
    's/^\t\treturn (double)(end - beginning);$/\t\treturn (double)end - (double)beginning;/'

if [ "$failures" -ne 0 ]; then
	echo "time64 calendar: $failures calibration failures"
	exit 1
fi
echo "time64 calendar: every mutant rejected"
