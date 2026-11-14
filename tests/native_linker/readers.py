"""Verify complete little-endian words and variable-length native symbols."""

import hashlib
import json
import os
import shlex
import struct
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'usr.bin/ld/ld.c'


def matches_case(binary, case):
    mode, data, expected_exit, expected_output = case
    result = subprocess.run([binary, mode], input=data, capture_output=True,
                            check=False, timeout=10)
    output = result.stdout.decode(errors='replace')
    matches = (output == expected_output if expected_exit == 0
               else expected_output in output)
    return result, result.returncode == expected_exit and matches and not result.stderr


def main():
    work = ROOT / '.build-logs' / ('native-readers-' + uuid.uuid4().hex)
    work.mkdir(parents=True)
    flags = []
    if sys.platform.startswith('linux'):
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'libbsd-overlay'], text=True))
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    binary = work / 'readers'
    command = [*compiler, '-std=gnu17', '-Wall', '-Wextra', '-Werror',
               '-DCROSS', '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
               '-I' + str(ROOT / 'tools/aoututils/include'),
               '-idirafter', str(ROOT / 'include'),
               '-idirafter', str(ROOT / 'tools/aoututils/ar'),
               '-DNATIVE_LINKER_SOURCE="' + str(SOURCE) + '"',
               str(ROOT / 'tests/native_linker/readers.c'), '-o', str(binary), *flags]
    build = subprocess.run(command, capture_output=True, check=False)
    (work / 'build.log').write_bytes(build.stdout + build.stderr)
    if build.returncode or build.stderr:
        raise RuntimeError('reader build fails; see ' + str(work / 'build.log'))
    cases = []
    for value in (0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678):
        cases.append(('word', struct.pack('<I', value), 0, f'{value}\n'))
    for length in range(4):
        cases.append(('word', bytes(length), 4, 'truncated input word'))
    for length in (1, 3, 63, 255):
        name = b'x' * length
        record = bytes((length, 3)) + struct.pack('<I', 0x80000001) + name
        expected = f'{length + 6} 3 2147483649 {name.decode()}\n'
        cases.append(('symbol', record, 0, expected))
        for cut in range(1, len(record)):
            diagnostic = ('truncated input symbol type' if cut == 1 else
                          'truncated input word' if cut < 6 else
                          'truncated input symbol name')
            cases.append(('symbol', record[:cut], 4, diagnostic))
    cases.extend([('symbol', b'', 0, 'end\n'), ('symbol', b'\0', 0, 'end\n')])
    for flags in (0x01, 0x78):
        for address in (0, 0x7fffffff, 0x80000000, 0xffffffff):
            index = 0xffffff if flags == 0x78 else 0
            record = bytes((flags,)) + struct.pack('<I', address)
            if flags == 0x78:
                record += index.to_bytes(3, 'little')
            cases.append(('relocation', record, 0, f'{flags} {address} {index}\n'))
            for cut in range(len(record)):
                diagnostic = ('truncated input relocation flags' if cut == 0 else
                              'truncated input word' if cut < 5 else
                              'truncated input relocation index')
                cases.append(('relocation', record[:cut], 4, diagnostic))
    for form, width in ((1, 4), (8, 4), (9, 2), (10, 2)):
        for address in (0, 8 - width, 9 - width, 8, 0xffffffff):
            record = struct.pack('<IBI', 5, form, address)
            valid = address <= 8 - width
            cases.append(('section', record, 0 if valid else 4,
                          '8\n' if valid else 'relocation outside input section'))
    cases.append(('section', struct.pack('<IBI', 5, 0x78, 0) + bytes(3),
                  4, 'truncated relocation record extent'))
    for available in range(9):
        cases.append(('short-section', struct.pack('<IB', 0, available),
                      0 if available == 8 else 4,
                      '8\n' if available == 8 else 'truncated input section'))
    padded = struct.pack('<IBI', 8, 9, 0)
    cases.append(('section', padded + bytes(3), 0, '8\n'))
    for length in range(3):
        cases.append(('section', padded + bytes(length), 4,
                      'truncated relocation padding'))
    for offset in range(3):
        padding = bytearray(3)
        padding[offset] = 1
        cases.append(('section', padded + padding, 4, 'invalid relocation padding'))
    cases.append(('section', struct.pack('<I', 4) + bytes(4), 4,
                  'invalid relocation padding extent'))
    rows = []
    for number, case in enumerate(cases):
        mode, data, expected_exit, _ = case
        result, passed = matches_case(binary, case)
        row = {'case': number, 'mode': mode, 'bytes': len(data),
               'exit': result.returncode, 'expected_exit': expected_exit,
               'pass': passed}
        rows.append(row)
        if not row['pass']:
            (work / 'failure.stdout').write_bytes(result.stdout)
            (work / 'failure.stderr').write_bytes(result.stderr)
            raise RuntimeError('reader contract fails: ' + json.dumps(row))
    source = SOURCE.read_text()
    controls = [
        ('short-word', '!= sizeof(bytes)', '> sizeof(bytes)',
         ('word', b'\0\0\0', 4, 'truncated input word')),
        ('signed-shift', '(unsigned int)bytes[3] << 24', 'bytes[3] << 24',
         ('word', b'\0\0\0\x80', 0, '2147483648\n')),
        ('short-type', 'if (c == EOF)\n\t\terror (2, "truncated input symbol type")',
         'if (c < EOF)\n\t\terror (2, "truncated input symbol type")',
         ('symbol', b'\3', 4, 'truncated input symbol type')),
        ('short-name', '!= sym->n_len)', '> sym->n_len)',
         ('symbol', b'\3\3\1\0\0\0x', 4, 'truncated input symbol name')),
        ('short-relocation-flags', 'if (flags == EOF)', 'if (flags < EOF)',
         ('relocation', b'', 4, 'truncated input relocation flags')),
        ('short-relocation-index', '!= sizeof(index)', '> sizeof(index)',
         ('relocation', b'\x78' + bytes(6), 4, 'truncated input relocation index')),
        ('relocation-extent', 'if (record_size > relsize - n)',
         'if (0 && record_size > relsize - n)',
         ('section', struct.pack('<IBI', 5, 0x78, 0) + bytes(3),
          4, 'truncated relocation record extent')),
        ('relocation-section', 'if (rel.addr > len || width > len - rel.addr)',
         'if (0 && (rel.addr > len || width > len - rel.addr))',
         ('section', struct.pack('<IBI', 5, 9, 7),
          4, 'relocation outside input section')),
        ('section-zero-fill', 'error(2, "truncated input section");', 'c = 0;',
         ('short-section', struct.pack('<IB', 0, 0), 4, 'truncated input section')),
        ('padding-zero-fill', 'error(2, "truncated relocation padding");', 'c = 0;',
         ('section', padded, 4, 'truncated relocation padding')),
        ('padding-value', 'if (c != 0)', 'if (0 && c != 0)',
         ('section', padded + b'\0\1\0', 4, 'invalid relocation padding')),
        ('padding-extent', 'if (relsize - n > 3)', 'if (0 && relsize - n > 3)',
         ('section', struct.pack('<I', 4) + bytes(4),
          4, 'invalid relocation padding extent')),
    ]
    mutations = []
    for name, old, new, case in controls:
        if source.count(old) != 1:
            raise RuntimeError('mutation anchor differs: ' + name)
        mutant = work / (name + '.c')
        mutant.write_text(source.replace(old, new).replace(
            'unsigned char bytes[4];', 'unsigned char bytes[4] = {0};').replace(
            'unsigned char index[3];', 'unsigned char index[3] = {0};'))
        executable = work / name
        mutant_command = [('-DNATIVE_LINKER_SOURCE="' + str(mutant) + '"'
                           if part.startswith('-DNATIVE_LINKER_SOURCE=') else
                           str(executable) if part == str(binary) else part)
                          for part in command]
        build = subprocess.run(mutant_command, capture_output=True, check=False)
        (work / (name + '.build.log')).write_bytes(build.stdout + build.stderr)
        if build.returncode or build.stderr:
            raise RuntimeError('mutation must compile without diagnostics: ' + name)
        result, passed = matches_case(executable, case)
        (work / (name + '.stdout')).write_bytes(result.stdout)
        (work / (name + '.stderr')).write_bytes(result.stderr)
        if passed:
            raise RuntimeError('mutation escapes its deciding case: ' + name)
        mutations.append({'name': name, 'exit': result.returncode, 'rejected': True})
    receipt = {'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
               'command': command, 'cases': rows, 'mutations': mutations}
    (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS native readers: {len(rows)} inputs, {len(mutations)} mutations rejected; {work}')


if __name__ == '__main__':
    main()
