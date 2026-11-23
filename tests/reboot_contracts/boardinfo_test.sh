#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Host test of usr.bin/boardinfo/boardinfo over stub sysctl and mount
# commands. The script must print every sysctl value it asks for, report
# "unavailable" for a name the kernel lacks (and still exit 0), tell a
# PicoCalc kernel from a PICO one by the battery register, and print the
# mounts. A mutant that asks for a misspelled name must fail the check.
# usage: boardinfo_test.sh BOARDINFO_SCRIPT WORKDIR
set -eu

script=$1
work=$2
stubs=$work/boardinfo-stubs
mkdir -p "$stubs"

cat > "$stubs/sysctl" <<'EOF'
#!/bin/sh
# stub: -n NAME prints the value; names in $STUB_MISSING fail like ENOENT
[ "$1" = -n ] && shift
for m in $STUB_MISSING; do
	[ "$m" = "$1" ] && exit 1
done
case $1 in
kern.version) echo "GizmoBSD 0.0 (PICOCALC) #1" ;;
kern.hostname) echo testboard ;;
machdep.cpu_khz) echo 125000 ;;
# both halves have the top bit set, so the kernel's int reads negative
machdep.flash_unique_hi) echo -559038737 ;;	# 0xdeadbeef
machdep.flash_unique_lo) echo -2147483647 ;;	# 0x80000001
*) echo "val-$1" ;;
esac
EOF
cat > "$stubs/mount" <<'EOF'
#!/bin/sh
echo "/dev/rd0 on / (rw)"
EOF
# The target's printf reads an operand with strtoul on a 32-bit long. The
# stub is a function sourced ahead of the script (a shell's builtin printf
# would shadow a stub on PATH) that wraps a negative operand the same way, so
# the host's 64-bit long does not widen it.
cat > "$stubs/prelude.sh" <<'EOF'
printf() {
	_fmt=$1
	shift
	_args=
	for _a in "$@"; do
		case $_a in
		-*) _a=$((_a + 4294967296)) ;;
		esac
		_args="$_args $_a"
	done
	# shellcheck disable=SC2086
	/usr/bin/printf "$_fmt" $_args
}
EOF
chmod +x "$stubs/sysctl" "$stubs/mount"

fail=0
check() {
	if ! grep -Fq -- "$2" "$1"; then
		echo "boardinfo_test: missing '$2'" >&2
		fail=1
	fi
}

run() {	# run SCRIPT OUT MISSING
	# id is exported so a script that trusts an inherited value prints it
	id=inherited BOARDINFO_PATH=$stubs STUB_MISSING="$3" sh -c '. "$1"; . "$2"' sh "$stubs/prelude.sh" "$1" > "$2"
}

run "$script" "$work/bi-full.out" ""
check "$work/bi-full.out" "kern.version: GizmoBSD 0.0 (PICOCALC) #1"
check "$work/bi-full.out" "kern.hostname: testboard"
check "$work/bi-full.out" "machdep.cpu_khz: 125000"
check "$work/bi-full.out" "machdep.image_crc: val-machdep.image_crc"
check "$work/bi-full.out" "board: PicoCalc registers answer"
check "$work/bi-full.out" "machdep.flash_unique: deadbeef80000001"
check "$work/bi-full.out" "/dev/rd0 on / (rw)"

run "$script" "$work/bi-pico.out" "machdep.picocalc.battery machdep.image_crc machdep.flash_unique_lo"
check "$work/bi-pico.out" "board: no PicoCalc registers"
check "$work/bi-pico.out" "machdep.image_crc: unavailable"
check "$work/bi-pico.out" "machdep.flash_unique: unavailable"
check "$work/bi-pico.out" "kern.hostname: testboard"

# Calibration: a misspelled name must be caught by the same checks.
sed 's/kern.version/kern.versio/' "$script" > "$work/bi-mutant.sh"
if cmp -s "$script" "$work/bi-mutant.sh"; then
	echo "boardinfo_test: mutant did not change the script" >&2
	exit 1
fi
run "$work/bi-mutant.sh" "$work/bi-mutant.out" "kern.versio"
if grep -Fq "kern.version: GizmoBSD" "$work/bi-mutant.out"; then
	echo "boardinfo_test: mutant output passed the check" >&2
	fail=1
fi
# Unique-ID calibration: each broken script must lose the full-run check.
# The halves are signed and have the top bit set, the case a decimal format
# or a swapped pair gets wrong.
n=0
idmutant() {	# idmutant NAME SED-EXPR
	n=$((n + 1))
	sed "$2" "$script" > "$work/bi-id$n.sh"
	if cmp -s "$script" "$work/bi-id$n.sh"; then
		echo "boardinfo_test: $1 did not change the script" >&2
		exit 1
	fi
	run "$work/bi-id$n.sh" "$work/bi-id$n.out" ""
	if grep -Fq "machdep.flash_unique: deadbeef80000001" "$work/bi-id$n.out"; then
		echo "boardinfo_test: $1 output passed the check" >&2
		fail=1
	fi
}
idmutant signed-format 's/%08x%08x/%08d%08d/'
idmutant swapped-halves 's/\$hi \$lo/$lo $hi/'
idmutant hi-name-misspelled 's/flash_unique_hi/flash_unique_h/'
[ "$fail" -eq 0 ] || exit 1
echo "boardinfo_test: all checks passed"
