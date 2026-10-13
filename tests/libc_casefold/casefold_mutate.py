"""Generate byte-folding, ordering, bound and errno contract falsifiers."""

import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
output = Path(sys.argv[2])
mutations = {
    "high_bytes": ("byte >= 'A' && byte <= 'Z'",
                   "(byte >= 'A' && byte <= 'Z') || (byte >= 193 && byte <= 218)", 1),
    "signed_order": ("(int)left_byte - (int)right_byte",
                     "(signed char)left_byte - (signed char)right_byte", 2),
    "count_early": ("while (length != 0)", "while (length > 1)", 1),
    "errno_loss": ("return byte;", "errno = EINVAL;\n\treturn byte;", 1),
}
for name, (anchor, replacement, count) in mutations.items():
    if source.count(anchor) != count:
        raise SystemExit(f"{name}: mutation anchor differs")
    candidate = source.replace(anchor, replacement)
    if name == "errno_loss":
        candidate = "#include <errno.h>\n" + candidate
    (output / f"casefold_{name}.mutant.c").write_text(candidate)
