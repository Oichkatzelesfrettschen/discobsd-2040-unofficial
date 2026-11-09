#!/bin/sh
# usage: mutate.sh SOURCE OUTPUT OLD NEW
# Writes SOURCE to OUTPUT with the one occurrence of OLD replaced by NEW;
# both take \n and \t escapes. Fails unless OLD occurs exactly once, so a
# mutation that no longer matches the source cannot pass as rejected.
set -eu
PYTHON=${PYTHON:-python3}
"$PYTHON" - "$1" "$2" "$3" "$4" <<'PY'
import sys

src, out, old, new = sys.argv[1:5]
old = old.encode().decode("unicode_escape")
new = new.encode().decode("unicode_escape")
with open(src) as f:
    text = f.read()
if text.count(old) != 1:
    raise SystemExit("mutation does not match exactly once: %r" % old)
with open(out, "w") as f:
    f.write(text.replace(old, new, 1))
PY
