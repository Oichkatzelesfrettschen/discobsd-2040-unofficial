"""Exercise executable-tail object imports with actual ARM ELF and a.out artifacts."""

import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import uuid

from check_layout import check, require
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FUNCTIONS = ('shared_errno_address', 'shared_stream_address', 'shared_environ_address',
             'shared_initial_state', 'shared_callback')
OBJECTS = ('errno', '_iob', 'environ', '__progname', '_curbrk',
           'library_errno_pointer', 'library_stream_pointer', 'library_null_pointer')
PUBLIC_OBJECTS = OBJECTS[:5]
PRODUCTION = ('lib/libc/stdio/exit.c', 'lib/libc/stdio/findiop.c',
              'lib/startup-arm/crt0.c', 'lib/libc/arm/sys/sbrk.c')
FLAGS = ['-std=c17', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
         '-mcpu=cortex-m0plus', '-mthumb', '-mfloat-abi=soft', '-Os',
         '-ffreestanding', '-fno-builtin', '-fno-common', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-nostdinc',
         '-I' + str(ROOT / 'include')]
PIC = ['-fPIC', '-msingle-pic-base', '-mpic-register=r9', '-mno-pic-data-is-text-relative']


class Runner:
    def __init__(self, work):
        self.work = work
        self.records = []

    def run(self, argv, expected=0, cwd=None):
        argv = [str(value) for value in argv]
        result = subprocess.run(argv, cwd=cwd or self.work, capture_output=True,
                                timeout=90, check=False)
        index = len(self.records)
        (self.work / f'command-{index}.stdout').write_bytes(result.stdout)
        (self.work / f'command-{index}.stderr').write_bytes(result.stderr)
        self.records.append({'argv': argv, 'exit': result.returncode})
        require(result.returncode == expected and not result.stderr,
                f'command {index} expects exit {expected}, gets {result.returncode}: '
                + result.stderr.decode(errors='replace'))
        return result.stdout


def symbol_table(elf):
    table = elf.get_section_by_name('.symtab')
    require(table is not None, 'ELF symbol table is absent')
    return {symbol.name: symbol for symbol in table.iter_symbols() if symbol.name}


def got_symbol_is_state(elf, symbol, extent, flash_end):
    index = symbol['st_shndx']
    if symbol.name == '_end':
        require(symbol['st_value'] == extent, 'state end symbol differs')
        return True
    if index == 'SHN_UNDEF':
        require(symbol['st_info']['bind'] == 'STB_WEAK' and symbol['st_value'] == 0,
                'GOT contains an unresolved strong or nonzero import')
        return False
    if index == 'SHN_ABS':
        return False
    require(isinstance(index, int), 'GOT symbol lacks a supported section owner')
    owner = elf.get_section(index)
    require(owner['sh_flags'] & 2, 'GOT symbol belongs to an unallocated section')
    start, end = owner['sh_addr'], owner['sh_addr'] + owner['sh_size']
    if owner['sh_flags'] & 1:
        require(end <= extent, 'GOT state owner leaves the state reservation')
        return True
    require(0x10020000 <= start <= end <= flash_end,
            'GOT fixed owner leaves the flash image')
    return False


def library_layout(path):
    with path.open('rb') as handle:
        elf = ELFFile(handle)
        symbols = symbol_table(elf)
        extent = symbols['__libc_state_size']['st_value']
        initialized = symbols['__libc_init_end']['st_value']
        flash_end = symbols['__libc_flash_end']['st_value']
        got = elf.get_section_by_name('.got')
        require(got is not None and got['sh_size'] % 4 == 0, 'GOT is absent or unaligned')
        got_offset = got['sh_addr']
        require(0 < initialized <= extent < 196608 and extent % 8 == 0,
                'invalid library state extent')
        entries = {}
        for name in OBJECTS:
            symbol = symbols[name]
            offset, size = symbol['st_value'], symbol['st_size']
            require(symbol['st_info']['type'] == 'STT_OBJECT' and size > 0
                    and offset + size <= extent, 'invalid object extent: ' + name)
            entries[name] = {'offset': offset, 'size': size}
        relocations = set()
        got_owners = {}
        for section in elf.iter_sections():
            if not isinstance(section, RelocationSection):
                continue
            target = elf.get_section(section['sh_info'])
            table = elf.get_section(section['sh_link'])
            for relocation in section.iter_relocations():
                symbol = table.get_symbol(relocation['r_info_sym'])
                index = symbol['st_shndx']
                state_reference = symbol.name == '_end'
                if isinstance(index, int):
                    owner = elf.get_section(index)
                    state_reference |= bool(owner['sh_flags'] & 2
                                            and owner['sh_addr'] < extent)
                if target['sh_flags'] & 4:
                    require(not (state_reference and relocation['r_info_type'] == 2),
                            'code contains an absolute process-state reference')
                    if relocation['r_info_type'] == 26:
                        displacement = struct.unpack_from(
                            '<I', target.data(), relocation['r_offset'] - target['sh_addr'])[0]
                        require(displacement % 4 == 0 and displacement + 4 <= got['sh_size'],
                                'GOT relocation leaves the compiler base interval')
                        require(struct.unpack_from('<I', got.data(), displacement)[0]
                                == symbol['st_value'], 'GOT relocation names a different object')
                        state = got_symbol_is_state(elf, symbol, extent, flash_end)
                        require(got_owners.setdefault(displacement, state) == state,
                                'GOT cell has conflicting symbol ownership')
                    continue
                if not target['sh_flags'] & 2:
                    continue
                require(relocation['r_info_type'] == 2, 'unsupported writable relocation')
                if state_reference:
                    relocations.add(relocation['r_offset'])
        require(set(got_owners) == set(range(0, got['sh_size'], 4)),
                'GOT cell lacks relocation ownership')
        for displacement, state in got_owners.items():
            if state:
                relocations.add(got['sh_addr'] + displacement)
        require(all(offset % 4 == 0 and offset + 4 <= initialized for offset in relocations),
                'relocation escapes initialized state')
        return {'state_size': extent, 'initialized_size': initialized,
                'got_offset': got_offset, 'objects': entries,
                'fixed_got_offsets': sorted(got_offset + offset
                                            for offset, state in got_owners.items() if not state),
                'relocations': sorted(relocations), 'functions': list(FUNCTIONS)}


def imports(layout, wrong_errno=False):
    definitions = []
    for name, entry in layout['objects'].items():
        if name not in PUBLIC_OBJECTS:
            continue
        offset = entry['offset'] + (4 if name == 'errno' and wrong_errno else 0)
        definitions.append(f'{name} = __libc_state_start + {offset};')
    return '\n'.join(definitions) + f'''
SECTIONS
{{
  .libc_state ALIGN(8) (NOLOAD) :
  {{
    __libc_state_start = .;
    . += {layout['state_size']};
    __libc_state_end = .;
  }} :data
}}
INSERT AFTER .bss;
'''


def state_header(layout, omit_pointer=False, wrong_base=False, relocate_fixed=False):
    relocations = layout['relocations']
    if omit_pointer:
        offset = layout['objects']['library_stream_pointer']['offset']
        require(offset in relocations, 'pointer relocation control anchor is absent')
        relocations = [entry for entry in relocations if entry != offset]
    if relocate_fixed:
        require(layout['fixed_got_offsets'], 'fixed GOT control anchor is absent')
        relocations = sorted(relocations + layout['fixed_got_offsets'])
    return (f'#define ABI_STATE_SIZE {layout["state_size"]}u\n'
            f'#define ABI_GOT_OFFSET {layout["got_offset"] + (4 if wrong_base else 0)}u\n'
            f'#define ABI_RELOCATION_COUNT {len(relocations)}u\n'
            'static const uint32_t abi_relocations[] = {'
            + ', '.join(str(value) + 'u' for value in relocations) + '};\n')


def verify_aout(path, end):
    fields = struct.unpack_from('<8I', path.read_bytes())
    require(fields[0] == 0o407, 'converted image lacks OMAGIC')
    require(0x20000000 + fields[1] + fields[2] + fields[3] == end,
            'converted a_bss excludes library state')


def check_interposition(paths):
    exports = set(PUBLIC_OBJECTS) | set(FUNCTIONS)
    for path in paths:
        with path.open('rb') as handle:
            for name, symbol in symbol_table(ELFFile(handle)).items():
                require(not (name in exports and symbol['st_shndx'] != 'SHN_UNDEF'
                             and symbol['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK')),
                        'application defines library import: ' + name)


def layout_controls(source, layout, runner):
    with source.open('rb') as handle:
        elf = ELFFile(handle)
        state = elf.get_section_by_name('.libc_state')
        symbols = elf.get_section_by_name('.symtab')
        end_index = next(index for index, symbol in enumerate(symbols.iter_symbols())
                         if symbol.name == '_end')
        writable = next(index for index, segment in enumerate(elf.iter_segments())
                        if segment['p_type'] == 'PT_LOAD' and segment['p_flags'] & 2)
        memory_size = elf['e_phoff'] + writable * elf['e_phentsize'] + 20
        truncated = state['sh_addr'] - elf.get_segment(writable)['p_vaddr']
        end_value = symbols['sh_offset'] + end_index * symbols['sh_entsize'] + 4
        controls = [('unadmitted-state', memory_size, truncated,
                     'library state escapes writable PT_LOAD memory'),
                    ('early-end-symbol', end_value, state['sh_addr'],
                     '_end excludes library state')]
    for name, offset, value, diagnostic in controls:
        changed = bytearray(source.read_bytes())
        struct.pack_into('<I', changed, offset, value)
        path = runner.work / (name + '.elf')
        path.write_bytes(changed)
        try:
            check(path, layout['state_size'])
        except ValueError as error:
            require(str(error) == diagnostic, name + ' reaches a different rejection')
        else:
            raise ValueError(name + ' escapes the layout verifier')
        print('PASS rejected ' + name + ': ' + diagnostic, flush=True)


def got_ownership_controls(source, runner):
    with source.open('rb') as handle:
        elf = ELFFile(handle)
        references = {}
        for section in elf.iter_sections():
            if not isinstance(section, RelocationSection):
                continue
            table = elf.get_section(section['sh_link'])
            for index, relocation in enumerate(section.iter_relocations()):
                if relocation['r_info_type'] != 26:
                    continue
                symbol_index = relocation['r_info_sym']
                symbol = table.get_symbol(symbol_index)
                location = section['sh_offset'] + index * section['sh_entsize']
                references.setdefault(symbol.name, []).append((location, relocation, symbol,
                    table['sh_offset'] + symbol_index * table['sh_entsize']))
        require(len(references.get('optional_object', [])) == 1,
                'weak GOT control requires one reference')
        location, relocation, symbol, symbol_location = references['optional_object'][0]
        zero_state_references = [reference for name in OBJECTS
                                 for reference in references.get(name, [])
                                 if reference[2]['st_value'] == 0]
        require(zero_state_references, 'state-zero GOT control anchor is absent')
        state_location, _, state_symbol, _ = zero_state_references[0]
        require(symbol['st_value'] == state_symbol['st_value'] == 0,
                'null and state-zero control anchors differ')
        controls = [
            ('missing-got-owner', location + 4, '<I', relocation['r_info'] & ~255,
             'GOT cell lacks relocation ownership'),
            ('conflicting-got-owner', state_location, '<I', relocation['r_offset'],
             'GOT cell has conflicting symbol ownership'),
            ('strong-undefined-got', symbol_location + 12, '<B', 0x10,
             'GOT contains an unresolved strong or nonzero import')]
    for name, offset, encoding, value, diagnostic in controls:
        content = bytearray(source.read_bytes())
        struct.pack_into(encoding, content, offset, value)
        path = runner.work / (name + '.elf')
        path.write_bytes(content)
        try:
            library_layout(path)
        except ValueError as error:
            require(str(error) == diagnostic,
                    name + ' reaches a different rejection: ' + str(error))
        else:
            raise ValueError(name + ' escapes the GOT ownership verifier')
        print('PASS rejected ' + name + ': ' + diagnostic, flush=True)


def linux_flat_image(source, destination):
    """Give Linux one flat process mapping while preserving every admitted byte.

    DiscoBSD's adjacent RX/RW segments share a Linux page. Linux replaces that
    page's execute permission with the last segment's RW permission. This
    fixture-only envelope models the flat process window; it proves no MPU or
    application text protection. The separately mapped library stays RX.
    """
    content = bytearray(source.read_bytes())
    with source.open('rb') as handle:
        elf = ELFFile(handle)
        loads = [(index, segment) for index, segment in enumerate(elf.iter_segments())
                 if segment['p_type'] == 'PT_LOAD']
        require(len(loads) == 2, 'flat runtime envelope requires two target LOADs')
        first_index, first = loads[0]
        base, offset = first['p_vaddr'], first['p_offset']
        memory_end = max(segment['p_vaddr'] + segment['p_memsz'] for _, segment in loads)
        file_end = base
        for _, segment in loads:
            if segment['p_filesz']:
                require(segment['p_offset'] - offset == segment['p_vaddr'] - base,
                        'runtime envelope would relocate admitted bytes')
                file_end = max(file_end, segment['p_vaddr'] + segment['p_filesz'])
        header = elf['e_phoff'] + first_index * elf['e_phentsize']
        require(elf['e_phentsize'] == 32, 'unexpected ELF32 program-header width')
        struct.pack_into('<III', content, header + 16, file_end - base,
                         memory_end - base, 7)
        for index, _ in loads[1:]:
            struct.pack_into('<I', content, elf['e_phoff'] + index * 32, 0)
        require(content[offset:] == source.read_bytes()[offset:],
                'runtime envelope changes executable or initialized bytes')
    destination.write_bytes(content)
    destination.chmod(source.stat().st_mode & 0o777)


def execute(args, runner):
    prefix = args.cc.removesuffix('gcc')
    require(prefix != args.cc, 'compiler must name a GCC driver')
    compiler = args.cc
    runner.run([compiler, '--version'])
    runner.run([args.qemu, '--version'])
    objects = []
    for index, source in enumerate([HERE / 'library.c', *(ROOT / name for name in PRODUCTION)]):
        target = runner.work / f'library-{index}.o'
        runner.run([compiler, *FLAGS, *PIC, '-ffunction-sections', '-fdata-sections',
                    '-c', source, '-o', target])
        objects.append(target)
    library = runner.work / 'library.elf'
    library_link = [prefix + 'ld', '--fatal-warnings', '-z', 'noexecstack',
                    '--gc-sections', '--emit-relocs', '-T', HERE / 'library.ld',
                    *(f'--undefined={name}' for name in FUNCTIONS)]
    runner.run([*library_link, '-o', library, *objects])
    layout = library_layout(library)
    (runner.work / 'layout.json').write_text(json.dumps(layout, indent=2) + '\n')
    got_ownership_controls(library, runner)
    runner.run([prefix + 'objcopy', '-O', 'binary', '--only-section=.text',
                '--only-section=.rodata', library, 'flash.bin'])
    runner.run([prefix + 'objcopy', '-O', 'binary', '--only-section=.data',
                '--only-section=.got', '--only-section=.got.plt', library, 'init.bin'])
    runner.run([prefix + 'objcopy', '--strip-all',
                *(f'--keep-symbol={name}' for name in FUNCTIONS), library, 'functions.elf'])
    (runner.work / 'blobs.S').write_text('''
.section .rodata.fixture,"a",%progbits
.balign 4
.global fixture_flash_start, fixture_flash_end, fixture_init_start, fixture_init_end
fixture_flash_start:
.incbin "flash.bin"
fixture_flash_end:
.balign 4
fixture_init_start:
.incbin "init.bin"
fixture_init_end:
.section .note.GNU-stack,"",%progbits
''')
    for source, target in [(runner.work / 'blobs.S', 'blobs.o'), (HERE / 'runtime.S', 'runtime.o')]:
        runner.run([compiler, '-mcpu=cortex-m0plus', '-mthumb', '-c', source, '-o', target])
    observations = []
    variants = [('zero-data', 0, 0, False), ('plain', 0, 1, False),
                ('odd-bss', 31, 1, False), ('large-bss', 4097, 1, False),
                ('overlay', 13, 1, True), ('wrong-import', 31, 1, False),
                ('wrong-got-base', 31, 1, False),
                ('missing-relocation', 31, 1, False),
                ('relocated-fixed-got', 31, 1, False)]
    for name, bss, data, overlay in variants:
        (runner.work / 'state.h').write_text(state_header(
            layout, name == 'missing-relocation', name == 'wrong-got-base',
            name == 'relocated-fixed-got'))
        (runner.work / 'imports.ld').write_text(imports(layout, name == 'wrong-import'))
        runner.run([compiler, *FLAGS, '-ffixed-r9', '-I' + str(runner.work),
                    f'-DAPP_BSS={bss}', f'-DAPP_DATA={data}', '-c', HERE / 'application.c',
                    '-o', 'application.o'])
        extra = []
        if overlay:
            for applet, size in [('alpha', 33), ('beta', 257)]:
                (runner.work / f'{applet}.S').write_text(
                    f'.section .app_bss_{applet},"aw",%nobits\n.balign 8\n.space {size}\n')
                runner.run([compiler, '-mcpu=cortex-m0plus', '-mthumb', '-c',
                            f'{applet}.S', '-o', f'{applet}.tool.o'])
            runner.run(['sh', ROOT / 'tools/generate_multicall_bss_overlay.sh',
                        'overlay.ld', 'alpha.tool.o', 'beta.tool.o'])
            extra = ['-T', 'overlay.ld', 'alpha.tool.o', 'beta.tool.o']
        image = runner.work / (name + '.elf')
        check_interposition([runner.work / member for member in
                             ('application.o', 'runtime.o', 'blobs.o')])
        runner.run([prefix + 'ld', '--fatal-warnings', '-z', 'noexecstack',
                    '-T', 'imports.ld', *extra,
                    '-T', ROOT / 'lib/elf32-arm.ld', '--just-symbols=functions.elf',
                    '-o', image, 'application.o', 'runtime.o', 'blobs.o'])
        start, end = check(image, layout['state_size'])
        runner.run([ROOT / 'tools/bin/rp2040/elf2aout', image, name + '.aout'])
        verify_aout(runner.work / (name + '.aout'), end)
        expected = {'wrong-import': 13, 'wrong-got-base': 13,
                    'missing-relocation': 15, 'relocated-fixed-got': 15}.get(name, 0)
        runtime_image = runner.work / (name + '.runtime.elf')
        linux_flat_image(image, runtime_image)
        runner.run([args.qemu, '-cpu', 'cortex-m0', runtime_image], expected=expected)
        observations.append({'name': name, 'state_start': start, 'state_end': end,
                             'expected_exit': expected})
        print(f'PASS {name}: state=0x{start:x}..0x{end:x}, QEMU exit={expected}', flush=True)
    layout_controls(runner.work / 'plain.elf', layout, runner)
    (runner.work / 'absolute-state.c').write_text(
        '#include <errno.h>\nint *absolute_errno_address(void);\n'
        'int *absolute_errno_address(void) { return &errno; }\n')
    runner.run([compiler, *FLAGS, '-ffunction-sections', '-fdata-sections',
                '-c', 'absolute-state.c', '-o', 'absolute-state.o'])
    runner.run([*library_link, '--undefined=absolute_errno_address',
                '-o', 'absolute-state.elf', 'absolute-state.o', *objects])
    try:
        library_layout(runner.work / 'absolute-state.elf')
    except ValueError as error:
        require(str(error) == 'code contains an absolute process-state reference',
                'non-PIC control reaches a different rejection')
    else:
        raise ValueError('non-PIC state reference escapes inspection')
    print('PASS rejected non-PIC state references', flush=True)
    (runner.work / 'interposition.c').write_text('int errno;\n')
    runner.run([compiler, *FLAGS, '-c', 'interposition.c', '-o', 'interposition.o'])
    try:
        check_interposition([runner.work / 'interposition.o'])
    except ValueError as error:
        require(str(error) == 'application defines library import: errno',
                'interposition control reaches a different rejection')
    else:
        raise ValueError('application definition overrides a library import')
    print('PASS rejected application errno definition', flush=True)
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', default='xip-object-abi-' + uuid.uuid4().hex)
    parser.add_argument('--cc', default='arm-none-eabi-gcc')
    parser.add_argument('--qemu', default='qemu-arm')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]*', args.label):
        parser.error('unsafe evidence label')
    work = ROOT / '.build-logs' / args.label
    try:
        work.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(str(error))
    runner = Runner(work)
    inputs = [*(ROOT / name for name in PRODUCTION), ROOT / 'lib/elf32-arm.ld',
              ROOT / 'tools/generate_multicall_bss_overlay.sh',
              ROOT / 'tools/bin/rp2040/elf2aout',
              *(path for path in HERE.iterdir() if path.suffix in ('.c', '.S', '.ld', '.py'))]
    receipt = {'state': 'failed', 'sources': {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in inputs}}
    try:
        receipt['variants'] = execute(args, runner)
        receipt['state'] = 'completed'
    finally:
        receipt['commands'] = runner.records
        (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
