"""Exercise shared ROM dispatch state with modeled ROM lookup and target calls."""

import hashlib
import json
import re
import struct
import subprocess
import uuid
from pathlib import Path

import unicorn
import unicorn.arm_const as arm
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCE = ROOT / 'lib/libc/arm/gen'
FLASH = 0x10020000
RAM = 0x20000000
MEMBERS = tuple((SOURCE / 'rom_float_members').read_text().split())
if not MEMBERS or len(MEMBERS) != len(set(MEMBERS)) or any(
        re.fullmatch(r'rom_float_[a-z0-9_]+', name) is None for name in MEMBERS):
    raise ValueError('invalid ROM member inventory')
REGS = [getattr(arm, 'UC_ARM_REG_R' + str(i)) for i in range(13)]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def command(argv):
    result = subprocess.run([str(x) for x in argv], capture_output=True, timeout=30)
    require(not result.returncode and not result.stdout and not result.stderr,
            'command fails: ' + result.stderr.decode(errors='replace'))


def build(work, static_resolver=False, unchecked_offset=False):
    objects = []
    for name in MEMBERS:
        source = SOURCE / (name + '.S')
        if unchecked_offset and name == 'rom_float_resolver':
            text = source.read_text()
            anchor = ('\tldr\tr2, =__libc_got_size\n'
                      '\tcmp\tr1, r2\n\tbcs\tLrom_float_invalid')
            require(text.count(anchor) == 1, 'GOT limit mutant anchor differs')
            source = work / 'unchecked-resolver.S'
            source.write_text(text.replace(anchor, '\tldr\tr2, =__libc_got_size\n\tnop\n\tnop'))
        target = work / (name + '.o')
        command(['arm-none-eabi-gcc', '-mcpu=cortex-m0plus', '-mthumb',
                 '-Wall', '-Wextra', '-Werror', '-nostdinc', '-I' + str(ROOT / 'include'),
                 '-I' + str(SOURCE),
                 *([] if static_resolver and name == 'rom_float_resolver'
                   else ['-DDISCOBSD_SHARED_LIBC=1']),
                 '-c', source, '-o', target])
        objects.append(target)
    image = work / 'library.elf'
    command(['arm-none-eabi-ld', '--fatal-warnings', '-z', 'noexecstack',
             '--emit-relocs', '-T', HERE / 'library.ld', '-o', image, *objects])
    with image.open('rb') as handle:
        elf = ELFFile(handle)
        symbols = {symbol.name: symbol['st_value']
                   for symbol in elf.get_section_by_name('.symtab').iter_symbols()}
        for section in elf.iter_sections():
            if not isinstance(section, RelocationSection):
                continue
            owner = elf.get_section(section['sh_info'])
            table = elf.get_section(section['sh_link'])
            for relocation in section.iter_relocations():
                symbol = table.get_symbol(relocation['r_info_sym'])
                require(not (owner['sh_flags'] & 2 and not owner['sh_flags'] & 1
                             and relocation['r_info_type'] == 2
                             and symbol.name.endswith('_cell')),
                        'read-only code contains an absolute cache-cell reference')
        got = elf.get_section_by_name('.got')
        require(got is not None and got['sh_size'] > 0 and got['sh_size'] % 4 == 0,
                'shared cell GOT is absent or differs')
        sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                    if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS']
        return symbols, sections, got['sh_addr'], got['sh_size']


def execute(layout, member, base, fault=None, integer=None):
    symbols, sections, got_offset, got_size = layout
    machine = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
    machine.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M0)
    machine.mem_map(0, 0x4000)
    machine.mem_map(FLASH, 0x10000)
    machine.mem_map(RAM, 0x30000)
    for address, data in sections:
        machine.mem_write(address if address >= FLASH else base + address, data)
    for offset in range(0, got_size, 4):
        address = base + got_offset + offset
        value = struct.unpack('<I', machine.mem_read(address, 4))[0]
        require(value % 4 == 0 and value + 4 <= symbols['__state_end'],
                'fixture GOT value leaves the writable state')
        machine.mem_write(address, struct.pack('<I', base + value))
    cell_member = 'clz' if integer is not None else member
    cell = base + symbols['__rp2040_rom_float_' + cell_member + '_cell']
    cold = struct.unpack('<I', machine.mem_read(cell, 4))[0] & ~1
    descriptor = cold + 6
    require(descriptor % 4 == 0, 'cold descriptor is unaligned')
    if fault == 'mode':
        value = machine.mem_read(descriptor + 7, 1)[0]
        machine.mem_write(descriptor + 7, bytes([value & ~0x40]))
    elif fault == 'got-limit':
        machine.mem_write(descriptor, struct.pack('<I', got_size))
    elif fault == 'got-alignment':
        machine.mem_write(descriptor, struct.pack('<I', 1))
    elif fault == 'cell-outside':
        displacement = struct.unpack('<I', machine.mem_read(descriptor, 4))[0]
        machine.mem_write(base + got_offset + displacement, struct.pack('<I', RAM + 0x30000))
    machine.mem_write(0x10, b'Mu\x01\x03')
    machine.mem_write(0x14, struct.pack('<HHH', 0x1100, 0x1200, 0x1001))
    machine.mem_write(0x2000, struct.pack('<32I', *([0x3001] * 32)))
    machine.mem_write(0x3000, b'\x70\x47')
    machine.mem_protect(FLASH, 0x10000, unicorn.UC_PROT_READ | unicorn.UC_PROT_EXEC)
    machine.mem_protect(0, 0x4000, unicorn.UC_PROT_READ | unicorn.UC_PROT_EXEC)
    values = [0x12340000 + i * 0x101 for i in range(13)]
    if integer is not None:
        values[0] = integer & 0xffffffff
        if member in ('l2f', 'ul2f'):
            values[1] = 0xffffffff if integer < 0 else 0
    values[9] = base + got_offset
    sp, lr = RAM + 0x2f000, FLASH + 0xf001
    state = {'lookup': 0, 'target': 0, 'returned': 0, 'rejected': False,
             'failure': None, 'invalid_got_read': False}
    stack_arguments = bytes(range(32))
    machine.mem_write(sp, stack_arguments)

    def memory_read(uc, access, address, size, value, unused):
        if fault in ('got-limit', 'got-alignment'):
            bad_offset = got_size if fault == 'got-limit' else 1
            if address == base + got_offset + bad_offset:
                state['invalid_got_read'] = True

    machine.hook_add(unicorn.UC_HOOK_MEM_READ, memory_read)

    def step(uc, address, size, unused):
        if address == 0x1000:
            state['lookup'] += 1
            require(uc.reg_read(arm.UC_ARM_REG_SP) % 8 == 0, 'ROM lookup stack is unaligned')
            lookup_code = uc.reg_read(arm.UC_ARM_REG_R1)
            for index in (1, 2, 3, 12):
                uc.reg_write(REGS[index], 0xa5000000 + index)
            uc.reg_write(arm.UC_ARM_REG_R0, 0x3101 if lookup_code == 0x334c else 0x2000)
            uc.reg_write(arm.UC_ARM_REG_PC, uc.reg_read(arm.UC_ARM_REG_LR))
        if address == 0x3100:
            state['target'] += 1
            value = uc.reg_read(arm.UC_ARM_REG_R0)
            for index in (1, 2, 3, 12):
                uc.reg_write(REGS[index], 0xb6000000 + index)
            uc.reg_write(arm.UC_ARM_REG_R0, 32 - value.bit_length())
            uc.reg_write(arm.UC_ARM_REG_PC, uc.reg_read(arm.UC_ARM_REG_LR))
        if address == 0x3000:
            state['target'] += 1
            required = ((0, 1, *range(4, 12)) if member in ('fadd', 'l2f', 'ul2f')
                        else tuple(range(12)))
            if any(uc.reg_read(REGS[i]) != values[i] for i in required):
                state['failure'] = 'argument-or-saved-register'
            if uc.reg_read(arm.UC_ARM_REG_SP) != sp or uc.reg_read(arm.UC_ARM_REG_LR) != lr:
                state['failure'] = 'stack-or-return'
        if address == lr & ~1:
            state['returned'] += 1
            uc.emu_stop()
        if bytes(uc.mem_read(address, 2)) == b'\x01\xdf':
            state['rejected'] = uc.reg_read(arm.UC_ARM_REG_R0) == 70
            uc.emu_stop()

    machine.hook_add(unicorn.UC_HOOK_CODE, step)
    for call in range(1 if fault else 2):
        for reg, value in zip(REGS, values):
            machine.reg_write(reg, value)
        machine.reg_write(arm.UC_ARM_REG_SP, sp)
        machine.reg_write(arm.UC_ARM_REG_LR, lr)
        entry = cold if fault else symbols['__aeabi_' + member]
        machine.emu_start(entry | 1, 0, count=300)
        if not fault:
            require(state['returned'] == call + 1 and state['failure'] is None,
                    str((member, base, fault, state)))
            require(struct.unpack('<I', machine.mem_read(cell, 4))[0]
                    == (0x3101 if integer is not None else 0x3001),
                    'cache cell does not publish target')
            require(all(machine.reg_read(REGS[i]) == values[i] for i in range(4, 12))
                    and machine.reg_read(arm.UC_ARM_REG_SP) == sp
                    and bytes(machine.mem_read(sp, 32)) == stack_arguments,
                    'call corrupts saved registers or stack arguments')
            if integer is not None:
                bits = struct.pack('<f' if member.endswith('f') else '<d', integer)
                actual = struct.pack('<I', machine.reg_read(arm.UC_ARM_REG_R0))
                if member.endswith('d'):
                    actual += struct.pack('<I', machine.reg_read(arm.UC_ARM_REG_R1))
                require(actual == bits, 'integer conversion result differs')
    if fault:
        require(not state['invalid_got_read'], 'descriptor reads invalid GOT slot')
        require(state['rejected'] and not state['lookup'] and not state['target'],
                'malformed descriptor reaches lookup or target: ' + str(state))
    else:
        require(state['lookup'] == 1 and state['target'] == 2, 'hot call repeats lookup')
    return {'member': member, 'base': base, 'fault': fault, 'integer': integer, **state}


def main():
    work = ROOT / '.build-logs' / ('shared-rom-' + uuid.uuid4().hex)
    work.mkdir()
    receipt = {'state': 'failed', 'unicorn': unicorn.__version__, 'cases': []}
    try:
        layout = build(work)
        for member in ('fadd', 'dadd'):
            for offset in (0x100, 0x718, 0x1008):
                receipt['cases'].append(execute(layout, member, RAM + offset))
            for fault in ('mode', 'got-limit', 'got-alignment', 'cell-outside'):
                receipt['cases'].append(execute(layout, member, RAM + 0x718, fault))
        for member in ('l2f', 'ul2f'):
            for offset in (0x100, 0x718, 0x1008):
                receipt['cases'].append(execute(layout, member, RAM + offset))
        for member in ('i2f', 'ui2f', 'i2d', 'ui2d', 'l2f', 'ul2f'):
            values = ((1, 0x7fffffff, -17) if member in ('i2f', 'i2d', 'l2f')
                      else (1, 0xffffffff, 17))
            for value in values:
                receipt['cases'].append(execute(layout, member, RAM + 0x718, integer=value))
        mixed = work / 'mixed'
        mixed.mkdir()
        mixed_layout = build(mixed, static_resolver=True)
        for member in ('fadd', 'dadd'):
            receipt['cases'].append(execute(mixed_layout, member, RAM + 0x718, 'mixed-resolver'))
        mutant = work / 'unchecked-offset'
        mutant.mkdir()
        mutant_layout = build(mutant, unchecked_offset=True)
        try:
            execute(mutant_layout, 'dadd', RAM + 0x718, 'got-limit')
        except ValueError as error:
            require(str(error) == 'descriptor reads invalid GOT slot',
                    'unchecked GOT offset mutant reaches a different failure')
        else:
            raise ValueError('unchecked GOT offset mutant escapes detection')
        receipt['unchecked_offset_mutant'] = 'rejected'
        receipt['state'] = 'completed'
        print('PASS shared ROM: 12 layouts, 18 conversions, 10 rejection controls')
    finally:
        receipt['sources'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in [HERE / 'run.py', HERE / 'library.ld',
                                        SOURCE / 'rom_float.inc', SOURCE / 'rom_float_members',
                                        *(SOURCE / (name + '.S')
                                          for name in MEMBERS)]}
        (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
