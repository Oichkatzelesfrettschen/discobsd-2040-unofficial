"""Restore getwd's release-before-copy lifetime violation."""

import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
anchor = '\t\tpnptr = prepend("/", prepend(dir->d_name, pnptr));\n\t\tclosedir(dirp);'
replacement = '\t\tclosedir(dirp);\n\t\tpnptr = prepend("/", prepend(dir->d_name, pnptr));'
if source.count(anchor) != 1:
    raise SystemExit("getwd lifetime mutation anchor differs")
Path(sys.argv[2]).write_text(source.replace(anchor, replacement, 1))
