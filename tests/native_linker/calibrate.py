"""Require compiled relocation faults to fail their independent branch witnesses."""

import hashlib
import json
import os
import shlex
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'usr.bin/ld/ld.c').read_text()
    work = ROOT / '.build-logs' / ('native-relocation-mutants-' + uuid.uuid4().hex)
    work.mkdir(parents=True)
    flags = []
    if sys.platform.startswith('linux'):
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'libbsd-overlay'], text=True))
    controls = [
        ('resolved-absolute-skipped', 'if (incoming_segment == RABS)',
         'if ((rel->flags & RSMASK) == RABS)', 3, 'bl: final absolute target differs'),
        ('partial-absolute-applied', 'if (output_relinfo && sp->n_type == N_EXT + N_ABS &&',
         'if (0 && output_relinfo && sp->n_type == N_EXT + N_ABS &&', 1,
         'bl: absolute target changes after partial links'),
        ('absolute-consumes-local', 'cursym.n_type == N_EXT+N_COMM)\n\t\t{',
         'cursym.n_type == N_EXT+N_COMM || cursym.n_type == N_EXT+N_ABS)\n\t\t{', 1,
         'local symbol table overflow'),
        ('object-extent', 'if (lengths[index] > remaining)',
         'if (0 && lengths[index] > remaining)', 1, 'truncated input symbol table'),
        ('output-symbol-extent', 'while (ssize % AOUT_ALIGNMENT)',
         'while (ssize++ % AOUT_ALIGNMENT)', 1,
         'partial output header differs from file extent'),
        ('symbol-extent', 'if ((unsigned)length + 6 > *remaining)',
         'if (0 && (unsigned)length + 6 > *remaining)', 1,
         'invalid input symbol padding'),
    ]
    rows = []
    for name, old, new, count, diagnostic in controls:
        if source.count(old) != count:
            raise RuntimeError('mutation anchor differs: ' + name)
        content = source.replace(old, new)
        if name == 'resolved-absolute-skipped':
            content = content.replace('int addend;', 'int addend;\n        (void)incoming_segment;')
        if name == 'output-symbol-extent':
            increment = '                        ssize++;'
            if content.count(increment) != 1:
                raise RuntimeError('symbol padding increment anchor differs')
            content = content.replace(increment, '')
        path, binary = work / (name + '.c'), work / name
        path.write_text(content)
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu17',
                   '-Wall', '-Wextra', '-Werror', '-DCROSS',
                   '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                   '-I' + str(ROOT / 'tools/aoututils/include'),
                   '-idirafter', str(ROOT / 'include'),
                   '-idirafter', str(ROOT / 'tools/aoututils/ar'),
                   str(path), '-o', str(binary), *flags]
        build = subprocess.run(command, capture_output=True, check=False, timeout=60)
        (work / (name + '.build.log')).write_bytes(build.stdout + build.stderr)
        if build.returncode or build.stderr:
            raise RuntimeError('mutation must compile without diagnostics: ' + name)
        result = subprocess.run([sys.executable, ROOT / 'tests/native_linker/branches.py',
                                 '--linker', binary], capture_output=True, check=False, timeout=60)
        (work / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode != 1 or diagnostic not in result.stderr.decode():
            raise RuntimeError('mutation misses its named rejection: ' + name)
        rows.append({'name': name, 'diagnostic': diagnostic, 'rejected': True})
        print('PASS native relocation mutation: ' + name, flush=True)
    (work / 'receipt.json').write_text(json.dumps({
        'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
        'mutations': rows}, indent=2) + '\n')


if __name__ == '__main__':
    main()
