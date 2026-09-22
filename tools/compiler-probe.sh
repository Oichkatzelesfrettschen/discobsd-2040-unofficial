#!/bin/sh
# A compiler capability includes loading and executing its output. Temporary
# products belong to one probe, including probes from concurrent submakes.
set -eu

width=$1
shift
probe_directory=$(mktemp -d "${TMPDIR:-/tmp}/discobsd-compiler-probe.XXXXXX")
trap 'rm -rf "$probe_directory"' EXIT HUP INT TERM

case $width in
ilp32) width_definition=-DREQUIRE_ILP32 ;;
native) width_definition= ;;
*) echo "ERROR compiler-probe: unknown width $width" >&2; exit 2 ;;
esac

cat > "$probe_directory/probe.c" <<'EOF'
#include <stddef.h>
#ifdef REQUIRE_ILP32
_Static_assert(sizeof(int) == 4, "int width");
_Static_assert(sizeof(long) == 4, "long width");
_Static_assert(sizeof(void *) == 4, "pointer width");
_Static_assert(sizeof(size_t) == 4, "size_t width");
#endif
int main(void) { return 0; }
EOF

# width_definition is a fixed option selected above, or the empty string.
# shellcheck disable=SC2086
if "$@" -std=c17 -Wall -Wextra -Werror $width_definition \
    -o "$probe_directory/probe" "$probe_directory/probe.c" \
    >"$probe_directory/build.log" 2>&1 &&
    "$probe_directory/probe" >"$probe_directory/run.log" 2>&1; then
    echo yes
else
    echo no
fi
