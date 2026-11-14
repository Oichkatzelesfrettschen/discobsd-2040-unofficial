"""Check native absolute imports through final and repeated partial links."""

import argparse
import hashlib
import json
import struct
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', type=Path, default=ROOT / 'tools/aoututils/ld/ld')
    parser.add_argument('--assembler', type=Path, default=ROOT / 'tools/bin/rp2040/as')
    args = parser.parse_args()
    linker, assembler = args.linker.resolve(), args.assembler.resolve()
    work = ROOT / '.build-logs' / ('native-branches-' + uuid.uuid4().hex)
    work.mkdir(parents=True)
    rows = []

    def run(command, expected=0, diagnostic=None):
        result = subprocess.run([str(part) for part in command], cwd=work,
                                capture_output=True, check=False, timeout=30)
        index = len(rows)
        (work / f'{index}.stdout').write_bytes(result.stdout)
        (work / f'{index}.stderr').write_bytes(result.stderr)
        rows.append({'command': [str(part) for part in command], 'exit': result.returncode})
        stdout = result.stdout.decode(errors='replace')
        if result.returncode != expected or result.stderr or (
                diagnostic is not None and diagnostic not in stdout):
            raise RuntimeError(f'command {index} violates its exit/diagnostic contract: '
                               + stdout)

    def assemble(name, source):
        path = work / (name + '.s')
        path.write_text(source)
        output = path.with_suffix('.o')
        run([assembler, path, '-o', output])
        return output

    def target(path, instruction, offset=0):
        data = path.read_bytes()
        first = struct.unpack_from('<H', data, 32 + offset)[0]
        if instruction == 'bl':
            second = struct.unpack_from('<H', data, 34 + offset)[0]
            value, bits = ((first & 2047) << 12) | ((second & 2047) << 1), 23
        else:
            bits = 12 if instruction == 'b' else 9
            value = (first & ((1 << (bits - 1)) - 1)) << 1
        if value & (1 << (bits - 1)):
            value -= 1 << bits
        return (0x20000000 + offset + 4 + value) & 0xffffffff

    prefix = assemble('prefix', '.text\n.space 8\n')
    near = assemble('near', '.global target\n.equ target, 0x20000041\n')
    far = assemble('far', '.global target\n.equ target, 0x10020001\n')
    for instruction in ('bl', 'b', 'beq'):
        caller = assemble(instruction, '.syntax unified\n.thumb\n.text\n.global _start\n'
                          f'.thumb_func\n_start:\n{instruction} target\nmovs r0, #1\nbx lr\n')
        final = work / (instruction + '.aout')
        run([linker, '-o', final, caller, near])
        if target(final, instruction) != 0x20000040:
            raise RuntimeError(instruction + ': final absolute target differs')
        run([linker, '-o', work / (instruction + '-far.aout'), caller, far],
            expected=4, diagnostic='out of range')
        partial = work / (instruction + '-partial.o')
        moved = work / (instruction + '-moved.o')
        run([linker, '-r', '-o', partial, caller, near])
        data = partial.read_bytes()
        header = struct.unpack_from('<8I', data)
        if 32 + sum(header[i] for i in (1, 2, 4, 5, 6)) != len(data):
            raise RuntimeError('partial output header differs from file extent')
        run([linker, '-r', '-o', moved, prefix, partial])
        run([linker, '-o', final, prefix, moved])
        if target(final, instruction, 16) != 0x20000040:
            raise RuntimeError(instruction + ': absolute target changes after partial links')
        run([linker, '-r', '-o', partial, caller])
        run([linker, '-r', '-o', moved, prefix, partial])
        run([linker, '-o', final, prefix, moved, near])
        if target(final, instruction, 16) != 0x20000040:
            raise RuntimeError(instruction + ': late absolute definition differs')

        provider = assemble(instruction + '-text', '.syntax unified\n.thumb\n.text\n'
                            '.global target\n.thumb_func\ntarget:\nbx lr\n')
        caller_size = struct.unpack_from('<I', caller.read_bytes(), 4)[0]
        run([linker, '-r', '-o', partial, caller, provider])
        run([linker, '-r', '-o', moved, prefix, partial])
        run([linker, '-o', final, prefix, moved])
        if target(final, instruction, 16) != 0x20000000 + 16 + caller_size:
            raise RuntimeError(instruction + ': already-applied text relocation differs')

        limit = {'bl': 1 << 22, 'b': 2048, 'beq': 256}[instruction]
        for displacement in (-limit - 2, -limit, limit - 2, limit):
            address = 0x20000004 + displacement
            boundary = assemble(instruction + '-boundary-' + str(displacement),
                                f'.global target\n.equ target, {address | 1}\n')
            accepted = -limit <= displacement < limit
            run([linker, '-o', final, caller, boundary],
                expected=0 if accepted else 4,
                diagnostic=None if accepted else 'out of range')
            if accepted and target(final, instruction) != address:
                raise RuntimeError(instruction + ': displacement boundary differs')

        for addend in (-2, 2):
            data = bytearray(caller.read_bytes())
            if instruction == 'bl':
                struct.pack_into('<HH', data, 32, 0xf000 | ((addend >> 12) & 2047),
                                 0xf800 | ((addend >> 1) & 2047))
            else:
                opcode = 0xe000 if instruction == 'b' else 0xd000
                mask = 2047 if instruction == 'b' else 255
                struct.pack_into('<H', data, 32, opcode | ((addend >> 1) & mask))
            additive = work / (instruction + '-addend-' + str(addend) + '.o')
            additive.write_bytes(data)
            run([linker, '-r', '-o', partial, additive, near])
            run([linker, '-r', '-o', moved, prefix, partial])
            run([linker, '-o', final, prefix, moved])
            if target(final, instruction, 16) != 0x20000040 + addend:
                raise RuntimeError(instruction + ': instruction addend differs')

    word = assemble('word-import', '.text\n.global _start\n_start:\n.word target\n')
    word_partial, word_moved, word_final = [work / name for name in
                                          ('word-partial.o', 'word-moved.o', 'word.aout')]
    run([linker, '-r', '-o', word_partial, word, near])
    run([linker, '-r', '-o', word_moved, prefix, word_partial])
    run([linker, '-o', word_final, prefix, word_moved])
    if struct.unpack_from('<I', word_final.read_bytes(), 48)[0] != 0x20000041:
        raise RuntimeError('absolute data word changes after partial links')

    for absolute_count, undefined_count in ((501, 0), (800, 0), (300, 499),
                                             (300, 500), (300, 501)):
        name = f'capacity-{absolute_count}-{undefined_count}'
        source = ''.join(f'.global a{i}\n.equ a{i}, {i}\n' for i in range(absolute_count))
        source += ''.join(f'.global u{i}\n' for i in range(undefined_count))
        obj = assemble(name, source)
        run([linker, '-r', '-o', work / (name + '-partial.o'), obj],
            expected=4 if undefined_count == 501 else 0,
            diagnostic='local symbol table overflow' if undefined_count == 501 else None)

    run([linker, '-r', '-o', work / 'conflicting.o', prefix, near, far],
        expected=1, diagnostic='target: ld:')

    for field in (4, 8, 16, 20, 24):
        for size in (len(near.read_bytes()) + 4, 0xfffffffc):
            malformed = bytearray(near.read_bytes())
            struct.pack_into('<I', malformed, field, size)
            path = work / f'extent-{field}-{size}.o'
            path.write_bytes(malformed)
            run([linker, '-r', '-o', work / 'extent.o', path], expected=4,
                diagnostic='object sections exceed input extent')

    member = near.read_bytes()
    for declared in (len(member), len(member) - 4, len(member) + 4):
        header = (f'{"near.o":<16}{0:<12}{0:<6}{0:<6}{"100644":<8}'
                  f'{declared:<10}`\n').encode()
        archive = work / f'extent-{declared}.a'
        archive.write_bytes(b'!<arch>\n' + header + member)
        run([linker, '-r', '-o', work / 'archive-extent.o', word, archive],
            expected=0 if declared == len(member) else 4,
            diagnostic=None if declared == len(member) else 'input extent')

    for declared in range(1, 12):
        malformed = bytearray(member)
        struct.pack_into('<I', malformed, 24, declared)
        path = work / f'symbol-extent-{declared}.o'
        path.write_bytes(malformed)
        run([linker, '-r', '-o', work / 'symbol-extent.o', path], expected=4,
            diagnostic='symbol exceeds declared table extent')
    for declared in (0, 12):
        bounded = bytearray(member)
        struct.pack_into('<I', bounded, 24, declared)
        path = work / f'symbol-boundary-{declared}.o'
        path.write_bytes(bounded)
        run([linker, '-r', '-o', work / 'symbol-boundary.o', path])
        if (b'target' in (work / 'symbol-boundary.o').read_bytes()) != (declared == 12):
            raise RuntimeError('symbol beyond declared table enters output')
    malformed = bytearray(member)
    malformed[-1] = 1
    path = work / 'symbol-padding.o'
    path.write_bytes(malformed)
    run([linker, '-r', '-o', work / 'symbol-padding-result.o', path], expected=4,
        diagnostic='invalid input symbol padding')

    receipt = {'worktree_source_sha256': hashlib.sha256(
                   (ROOT / 'usr.bin/ld/ld.c').read_bytes()).hexdigest(),
               'linker_sha256': hashlib.sha256(linker.read_bytes()).hexdigest(),
               'assembler_sha256': hashlib.sha256(assembler.read_bytes()).hexdigest(),
               'commands': rows}
    (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS native absolute branches and symbol capacities: {len(rows)} commands; {work}')


if __name__ == '__main__':
    main()
