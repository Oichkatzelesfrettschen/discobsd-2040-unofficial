#!/bin/sh
#
# Test (c), the acceptance test: build libc and crt0 with this assembler,
# archive with the tree's ar, link a program with the tree's ld, and run it.
#
# The build routes the cross compiler's -B at a wrapper that keeps every
# assembler input, so each of libc's translation units is also compared
# against arm-none-eabi-as on the same input. Those are real compiler
# output across the whole library, which is a far wider net than the
# hand-written encoding inputs.
#
# The linked program is then executed under Unicorn against a stub kernel,
# which is the closest this test comes to the board without touching it.
# The a.out must reach main, resolve its string and syscall references and
# write the expected bytes.
#
set -eu

TOPSRC=${TOPSRC:?set TOPSRC to the top of the tree}
MACHINE=${MACHINE:-rp2040}
TOOLBINDIR=${TOOLBINDIR:-$TOPSRC/tools/bin}
CC=${CC:?set CC to the cross compiler command}
GNUAS=${GNUAS:-arm-none-eabi-as}
CPU=${CPU:-cortex-m0plus}
here=$(cd "$(dirname "$0")" && pwd)
work=$(cd "${WORK:-.}" && pwd)

wrap=$work/thumb-wrap
saved=$work/thumb-saved
rm -rf "$wrap" "$saved"
mkdir -p "$wrap" "$saved"

# Every tool but as is taken from the tree; as is wrapped so the inputs
# the compiler generates can be compared afterwards.
for t in "$TOOLBINDIR"/*; do
	case $(basename "$t") in
	as) ;;
	*) ln -sf "$t" "$wrap/$(basename "$t")" ;;
	esac
done
cat > "$wrap/as" <<EOF
#!/bin/sh
out=""
prev=""
for a in "\$@"; do
	[ "\$prev" = "-o" ] && out="\$a"
	prev="\$a"
done
for a in "\$@"; do
	case "\$a" in
	*.s) cp "\$a" "$saved/\$(basename "\$out" .o).s" 2>/dev/null || : ;;
	esac
done
exec "$TOOLBINDIR/as" "\$@"
EOF
chmod +x "$wrap/as"

echo "link: building crt0 and libc with the tree's Thumb assembler"
${MAKE:-bmake} -C "$TOPSRC/lib/libc_aout/startup" MACHINE="$MACHINE" \
	TOOLBINDIR="$wrap" >/dev/null
${MAKE:-bmake} -C "$TOPSRC/lib/libc_aout/libc" MACHINE="$MACHINE" clean \
	>/dev/null 2>&1 || :
${MAKE:-bmake} -C "$TOPSRC/lib/libc_aout/libc" MACHINE="$MACHINE" \
	TOOLBINDIR="$wrap" >/dev/null

units=0
fail=0
for s in "$saved"/*.s; do
	[ -e "$s" ] || continue
	units=$((units + 1))
	$GNUAS -mcpu="$CPU" -mthumb -o "$work/_gnu.o" "$s" 2>/dev/null || continue
	"$TOOLBINDIR/as" "$s" -o "$work/_mine.aout"
	if ! python3 "$here/thumb-aoutdiff.py" "$s" "$work/_mine.aout" \
	    "$work/_gnu.o" >"$work/_diff.txt"
	then
		cat "$work/_diff.txt"
		fail=$((fail + 1))
	fi
done
echo "link: $units libc units assembled, $fail disagree with $GNUAS"
[ "$fail" -eq 0 ] || exit 1

echo "link: linking with the tree's ld"
$CC -Os -fcommon -Wa,-x -B"$TOOLBINDIR/" -c "$here/thumb-hello.c" \
	-o "$work/thumb-hello.o"
"$TOOLBINDIR/ld" -o "$work/thumb-hello.aout" \
	"$TOPSRC/lib/libc_aout/crt0.o" "$work/thumb-hello.o" \
	"$TOPSRC/lib/libc_aout/libc.a"

# A linked executable carries no relocation; anything left means ld could
# not resolve a reference this assembler emitted.
python3 - "$work/thumb-hello.aout" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
mid, text, data, bss, rt, rd, sy, entry = struct.unpack("<8I", d[:32])
if (mid & 0xffff) != 0o407:
    sys.exit("link: magic is %#o, expected OMAGIC" % (mid & 0xffff))
# An executable carries MID_ZERO, the only machine id exec_aout_check()
# in sys/kern/exec_aout.c accepts; MID_ARM6 marks relocatable output.
if (mid >> 16) & 0x3ff != 0:
    sys.exit("link: mid is %d, expected MID_ZERO" % ((mid >> 16) & 0x3ff))
if entry >> 16 != 0x2000:
    sys.exit("link: entry %#x is outside the Cortex-M user window" % entry)
if rt or rd:
    sys.exit("link: %d text and %d data relocation bytes left over" % (rt, rd))
if entry & 1 == 0:
    sys.exit("link: entry %#x is even; the Thumb bit is missing" % entry)
print("link: text %d data %d bss %d, entry %#x, no relocation left"
      % (text, data, bss, entry))
EOF

if python3 -c "import unicorn" 2>/dev/null; then
	echo "link: running the result under Unicorn"
	got=$(python3 "$here/thumb-run.py" "$work/thumb-hello.aout")
	want="Hello, World!
hello"
	if [ "$got" != "$want" ]; then
		echo "link: the program printed:"
		echo "$got"
		echo "link: expected:"
		echo "$want"
		exit 1
	fi
	echo "link: the program ran and printed what it should"
else
	echo "link: not run -- python3 unicorn is absent"
fi
