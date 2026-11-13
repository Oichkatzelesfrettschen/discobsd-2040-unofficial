"""Check that executable-local library state participates in loader admission."""

import argparse

from elftools.elf.elffile import ELFFile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(path, size):
    with path.open('rb') as handle:
        elf = ELFFile(handle)
        require(elf.elfclass == 32 and elf.little_endian, 'expected little-endian ELF32')
        require(elf['e_machine'] == 'EM_ARM', 'expected ARM executable')
        state = elf.get_section_by_name('.libc_state')
        require(state is not None, 'library state reservation is absent')
        require(state['sh_type'] == 'SHT_NOBITS', 'library state must occupy BSS')
        require(state['sh_flags'] & 7 == 3, 'library state requires writable allocation')
        start = state['sh_addr']
        end = start + state['sh_size']
        require(state['sh_size'] == size and size > 0, 'library state extent differs')
        require(start % 8 == 0, 'library state alignment differs')
        require(0x20000000 <= start < end <= 0x20030000,
                'library state escapes the process window')
        for section in elf.iter_sections():
            if section.name != '.libc_state' and section['sh_flags'] & 2 and section['sh_size']:
                low, high = section['sh_addr'], section['sh_addr'] + section['sh_size']
                require(high <= start or end <= low,
                        'library state overlaps another allocated section')
        segments = [segment for segment in elf.iter_segments()
                    if segment['p_type'] == 'PT_LOAD' and segment['p_flags'] & 2
                    and segment['p_vaddr'] <= start
                    and end <= segment['p_vaddr'] + segment['p_memsz']]
        require(len(segments) == 1, 'library state escapes writable PT_LOAD memory')
        symbols = elf.get_section_by_name('.symtab')
        for name in ('_end', '__end__', '__libc_state_end'):
            entries = symbols.get_symbol_by_name(name) or []
            require(len(entries) == 1 and entries[0]['st_value'] == end,
                    name + ' excludes library state')
        for name in ('__app_bss_end', 'application_bss_end'):
            entries = symbols.get_symbol_by_name(name) or []
            if entries:
                require(entries[0]['st_value'] <= start,
                        'library state overlaps application storage')
        return start, end


def main():
    import pathlib

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=pathlib.Path)
    parser.add_argument('--state-size', type=int, required=True)
    args = parser.parse_args()
    try:
        start, end = check(args.elf, args.state_size)
    except (OSError, ValueError) as error:
        parser.exit(1, 'FAIL XIP object layout: ' + str(error) + '\n')
    print(f'PASS XIP object layout: state=[0x{start:x},0x{end:x})')


if __name__ == '__main__':
    main()
