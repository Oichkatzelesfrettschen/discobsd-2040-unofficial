#!/bin/sh
#
# gets(3) takes no length, so every call stores as many characters as the
# line holds.  The process image is one flat window of text, data, bss and
# stack with no MMU behind it, so a store past the buffer reaches the rest of
# the program rather than faulting.  C11 removed the function from the
# standard, the tree's <stdio.h> no longer declares it, and neither libc
# builds an object for it.
#
# Those two facts are what the check rests on, and between them they need no
# source sweep: a call in any file the tree compiles fails at its implicit
# declaration, and a file that reaches the declaration some other way fails
# at the link. A grep for callers would add only a heuristic that cannot
# tell "*p = gets(b);" from a line of block comment.
#
# Usage: stdio_bounds.sh TOPSRC HOSTCC WORKDIR [LIBC_ARCHIVE NM]
set -eu

TOPSRC=$1
HOSTCC=$2
WORK=$3
ARCHIVE=${4:-}
NM=${5:-nm}

checks=0
failures=0

fail() {
	failures=$((failures + 1))
	echo "FAIL: $1" >&2
}

rm -rf "$WORK"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# The tree's own headers, which is where the declaration would be.
TREE="-nostdinc -I${TOPSRC}/include"

cat > "$WORK/caller.c" <<'EOF'
#include <stdio.h>
char buf[80];
int main(void) { gets(buf); return 0; }
EOF

cat > "$WORK/bounded.c" <<'EOF'
#include <stdio.h>
char buf[80];
int main(void) { return fgets(buf, sizeof buf, stdin) == NULL; }
EOF

# The header refuses the caller. -Werror=implicit-function-declaration is
# what turns the missing declaration into a compile failure on a compiler
# that still only warns.
checks=$((checks + 1))
if $HOSTCC -std=c17 -Werror=implicit-function-declaration $TREE \
    -fsyntax-only "$WORK/caller.c" >/dev/null 2>&1; then
	fail "<stdio.h> still lets a gets() call compile"
fi

# The same compile with fgets succeeds, so what the first case proves is the
# absent declaration rather than a broken include path or an unusable header.
checks=$((checks + 1))
if ! $HOSTCC -std=c17 -Werror=implicit-function-declaration $TREE \
    -fsyntax-only "$WORK/bounded.c" >/dev/null 2>&1; then
	fail "the tree headers reject an fgets() call too, so rejecting gets() proves nothing"
fi

# Neither library builds an object for it, so a caller that declares it
# itself fails at the link rather than finding a definition.
for mk in lib/libc/stdio/Makefile lib/libc_aout/libc/Makefile; do
	checks=$((checks + 1))
	if grep -q '[^Af]gets\.[co]' "${TOPSRC}/${mk}"; then
		fail "${mk} still builds gets"
	fi
	checks=$((checks + 1))
	if ! grep -q 'fgets\.[co]' "${TOPSRC}/${mk}"; then
		fail "${mk} lists no fgets either, so the gets check reads the wrong lines"
	fi
done

# A built archive settles it by symbol rather than by Makefile text. The
# fgets half is the control: an archive that defines neither would pass the
# gets half while proving nothing.
#
# ar keeps a member the Makefile has stopped listing, so an incremental tree
# can carry a gets.o no rule rebuilds. Reporting that is right: it is what
# the archive holds, and "bmake clean" in lib is what clears it.
if [ -n "$ARCHIVE" ] && [ -r "$ARCHIVE" ]; then
	checks=$((checks + 1))
	if $NM --defined-only "$ARCHIVE" 2>/dev/null \
	    | awk '$3 == "gets" { found = 1 } END { exit !found }'; then
		fail "$ARCHIVE defines gets"
	fi
	checks=$((checks + 1))
	if ! $NM --defined-only "$ARCHIVE" 2>/dev/null \
	    | awk '$3 == "fgets" { found = 1 } END { exit !found }'; then
		fail "$ARCHIVE defines no fgets, so reading it for gets proves nothing"
	fi
else
	echo "stdio_bounds: no libc archive given; the symbol cases did not run" >&2
fi

echo "stdio_bounds: ${checks} checks, ${failures} failures"
[ "$failures" -eq 0 ]
