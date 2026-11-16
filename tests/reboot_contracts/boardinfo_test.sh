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
kern.version) echo "DiscoBSD 0.0 (PICOCALC) #1" ;;
kern.hostname) echo testboard ;;
machdep.cpu_khz) echo 125000 ;;
*) echo "val-$1" ;;
esac
EOF
cat > "$stubs/mount" <<'EOF'
#!/bin/sh
echo "/dev/rd0 on / (rw)"
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
	BOARDINFO_PATH=$stubs STUB_MISSING="$3" sh "$1" > "$2"
}

run "$script" "$work/bi-full.out" ""
check "$work/bi-full.out" "kern.version: DiscoBSD 0.0 (PICOCALC) #1"
check "$work/bi-full.out" "kern.hostname: testboard"
check "$work/bi-full.out" "machdep.cpu_khz: 125000"
check "$work/bi-full.out" "machdep.image_crc: val-machdep.image_crc"
check "$work/bi-full.out" "board: PicoCalc registers answer"
check "$work/bi-full.out" "flash unique id: not exported"
check "$work/bi-full.out" "/dev/rd0 on / (rw)"

run "$script" "$work/bi-pico.out" "machdep.picocalc.battery machdep.image_crc"
check "$work/bi-pico.out" "board: no PicoCalc registers"
check "$work/bi-pico.out" "machdep.image_crc: unavailable"
check "$work/bi-pico.out" "kern.hostname: testboard"

# Calibration: a misspelled name must be caught by the same checks.
sed 's/kern.version/kern.versio/' "$script" > "$work/bi-mutant.sh"
if cmp -s "$script" "$work/bi-mutant.sh"; then
	echo "boardinfo_test: mutant did not change the script" >&2
	exit 1
fi
STUB_MISSING="kern.versio" BOARDINFO_PATH=$stubs sh "$work/bi-mutant.sh" > "$work/bi-mutant.out"
if grep -Fq "kern.version: DiscoBSD" "$work/bi-mutant.out"; then
	echo "boardinfo_test: mutant output passed the check" >&2
	fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "boardinfo_test: all checks passed"
