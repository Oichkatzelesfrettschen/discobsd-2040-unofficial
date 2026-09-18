#!/usr/bin/env python3
"""Read-only inventory for Discobsd versus 4.4BSD-Lite2 APIs."""
from pathlib import Path
import re
import json

DISC = Path('~/Github/discobsd-pico-unofficial')
BSD44 = Path('~/Github/4.4BSD-Lite2')

FUNCTION_RE = re.compile(r'^\s*(?:[A-Za-z_][\w\s*]*\s+)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;\s*$', re.M)

def files(root, subtrees):
    out = []
    for subtree in subtrees:
        base = root / subtree
        if base.exists():
            out.extend(p for p in base.rglob('*') if p.is_file() and p.suffix in {'.h', '.c'})
    return sorted(set(out))

def declared(root, subtrees):
    names = {}
    for path in files(root, subtrees):
        try:
            text = path.read_text(errors='replace')
        except OSError:
            continue
        for match in FUNCTION_RE.finditer(text):
            name = match.group(1)
            if name not in {'if', 'for', 'while', 'switch'}:
                names.setdefault(name, []).append(str(path.relative_to(root)))
    return names

def source_basenames(root, subtrees):
    names = {}
    for path in files(root, subtrees):
        if path.suffix == '.c':
            names.setdefault(path.stem, []).append(str(path.relative_to(root)))
    return names

disc_decl = declared(DISC, ['include', 'sys/sys'])
bsd_decl = declared(BSD44, ['include', 'sys/sys'])
disc_src = source_basenames(DISC, ['lib/libc'])
bsd_src = source_basenames(BSD44, ['lib/libc'])

result = {
    'discobsd_declared': disc_decl,
    'bsd44_declared': bsd_decl,
    'declared_only_bsd44': sorted(set(bsd_decl) - set(disc_decl)),
    'discobsd_libc_stems': sorted(disc_src),
    'bsd44_libc_stems': sorted(bsd_src),
    'libc_stems_only_bsd44': sorted(set(bsd_src) - set(disc_src)),
}
print(json.dumps(result, indent=2, sort_keys=True))
