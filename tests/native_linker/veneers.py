"""Exercise native-linked far-call veneers on the Unicorn Cortex-M0 model."""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import uuid
from pathlib import Path

import unicorn
import unicorn.arm_const as arm

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.build-logs' / ('native-veneer-' + uuid.uuid4().hex)
WORK.mkdir()
BASE = 0x20000000
FLASH = 0x10020000
SP = BASE + 0x2f000
REGS = [getattr(arm, 'UC_ARM_REG_R' + str(i)) for i in range(13)]
TOOLS = {name: ROOT / 'tools/bin/rp2040' / name for name in ('as', 'ld')}
for name in ('arm-none-eabi-as', 'arm-none-eabi-objcopy'):
    path = shutil.which(name)
    if path is None:
        raise RuntimeError('required ARM instruction validation tool is missing: ' + name)
    TOOLS[name] = Path(path)
rows = []


def run(args):
    result = subprocess.run([str(x) for x in args], capture_output=True, timeout=30)
    if result.returncode or result.stdout or result.stderr:
        raise RuntimeError((args, result.returncode, result.stdout, result.stderr))


def case(name, target, padding, body, expected):
    source = WORK / (name + '.s')
    source.write_text('.syntax unified\n.cpu cortex-m0plus\n.thumb\n.text\n'
                      '.global _start\n.thumb_func\n_start:\nbl bridge\nbkpt #0\n'
                      + '.space ' + str(padding) + '\n.p2align 2\n'
                      'bridge:\n' + body + '\n.pool\n'
                      '.global target\n.equ target, ' + hex(target) + '\n'
                      '.data\n.word target\n')
    obj, binary = source.with_suffix('.o'), source.with_suffix('.aout')
    run([TOOLS['as'], source, '-o', obj])
    run([TOOLS['ld'], '-o', binary, obj])
    data = binary.read_bytes()
    input_text_size = struct.unpack_from('<I', obj.read_bytes(), 4)[0]
    header = struct.unpack('<8I', data[:32])
    text_size, data_size = header[1:3]
    if text_size != (input_text_size + 15) & ~15:
        raise RuntimeError('linked text extent differs from native 16-byte alignment')
    if data_size != 4 or struct.unpack_from('<I', data, 32 + text_size)[0] != target:
        raise RuntimeError('absolute function pointer differs from flash target')
    machine = unicorn.Uc(unicorn.UC_ARCH_ARM,
                         unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
    machine.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M0)
    machine.mem_map(BASE, 0x30000)
    machine.mem_map(FLASH, 0xc000)
    machine.mem_write(BASE, data[32:32 + text_size + data_size])
    machine.mem_write(target & ~1, b'\x70\x47')
    values = [(0x12340000 + 0x101 * i) for i in range(13)]
    for reg, value in zip(REGS, values):
        machine.reg_write(reg, value)
    machine.reg_write(arm.UC_ARM_REG_SP, SP)
    machine.reg_write(arm.UC_ARM_REG_LR, 0x2000ffff)
    machine.reg_write(arm.UC_ARM_REG_APSR, 0xa0000000)
    stack_args = bytes(range(32))
    machine.mem_write(SP, stack_args)
    state = {'target': False, 'returned': False, 'minimum_sp': SP, 'failure': None}

    def step(uc, address, size, unused):
        current_sp = uc.reg_read(arm.UC_ARM_REG_SP)
        state['minimum_sp'] = min(state['minimum_sp'], current_sp)
        if current_sp % 8:
            state['failure'] = 'stack-alignment'
        if address == (target & ~1):
            state['target'] = True
            actual = [uc.reg_read(reg) for reg in REGS]
            if actual != values:
                state['failure'] = 'argument-or-saved-register'
            elif uc.reg_read(arm.UC_ARM_REG_SP) != SP:
                state['failure'] = 'target-stack-pointer'
            elif bytes(uc.mem_read(SP, 32)) != stack_args:
                state['failure'] = 'stack-arguments'
            elif uc.reg_read(arm.UC_ARM_REG_LR) != BASE + 5:
                state['failure'] = 'return-address'
            elif uc.reg_read(arm.UC_ARM_REG_APSR) & 0xf0000000 != 0xa0000000:
                state['failure'] = 'flags'
        if address == BASE + 4:
            state['returned'] = True
            uc.emu_stop()

    machine.hook_add(unicorn.UC_HOOK_CODE, step)
    try:
        machine.emu_start(BASE | 1, 0, count=32)
    except unicorn.UcError:
        state['failure'] = state['failure'] or 'execution-fault'
    if not state['returned']:
        state['failure'] = state['failure'] or 'no-return'
    if not state['target']:
        state['failure'] = state['failure'] or 'no-target'
    if state['failure'] != expected:
        raise RuntimeError((name, state, expected))
    bridge_offset = (6 + padding + 3) & ~3
    if expected is None:
        legal_source = WORK / (name + '-armv6m.s')
        legal_source.write_text('.syntax unified\n.thumb\n.text\n.p2align 2\n'
                                + body + '\n.pool\n.equ target, ' + hex(target) + '\n')
        legal_object, legal_bytes = legal_source.with_suffix('.o'), legal_source.with_suffix('.bin')
        run([TOOLS['arm-none-eabi-as'], '-mcpu=cortex-m0plus', legal_source,
             '-o', legal_object])
        run([TOOLS['arm-none-eabi-objcopy'], '-O', 'binary', '-j', '.text',
             legal_object, legal_bytes])
        if legal_bytes.read_bytes() != data[32 + bridge_offset:32 + input_text_size]:
            raise RuntimeError('native veneer differs from ARMv6-M assembler encoding')
    state.update(name=name, target_address=hex(target), text_size=text_size,
                 bridge_and_literal_bytes=input_text_size - bridge_offset,
                 final_text_alignment_bytes=text_size - input_text_size,
                 peak_stack_bytes=SP - state['minimum_sp'],
                 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                 binary_sha256=hashlib.sha256(data).hexdigest())
    if expected is None and (state['bridge_and_literal_bytes'] != 12
                             or state['peak_stack_bytes'] != 8):
        raise RuntimeError(('unexpected veneer resources', state))
    rows.append(state)


body = 'push {r3, r4}\nldr r3, =target\nstr r3, [sp, #4]\npop {r3, pc}'
illegal_source = WORK / 'thumb2-control.s'
illegal_source.write_text('.syntax unified\n.thumb\nmovw r0, #1\n')
illegal = subprocess.run([str(TOOLS['arm-none-eabi-as']), '-mcpu=cortex-m0plus',
                          str(illegal_source), '-o', str(WORK / 'thumb2-control.o')],
                         capture_output=True, timeout=30,
                         env={**os.environ, 'LC_ALL': 'C'})
(WORK / 'thumb2-control.stderr').write_bytes(illegal.stderr)
if (illegal.returncode == 0 or b'does not support' not in illegal.stderr
        or b'movw' not in illegal.stderr):
    raise RuntimeError('ARMv6-M assembler fails to reject the Thumb-2 control')

cpu_control = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
cpu_control.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M0)
cpu_control.mem_map(BASE, 4096)
cpu_control.mem_write(BASE, bytes.fromhex('40f20100'))
unicorn_rejects_thumb2 = False
try:
    cpu_control.emu_start(BASE | 1, 0, count=1)
except unicorn.UcError:
    unicorn_rejects_thumb2 = True

for target in (0x10020001, 0x1002bffd):
    for padding in (0, 2, 4, 6):
        case('good-' + hex(target) + '-' + str(padding), target, padding, body, None)
case('clobbered-argument', 0x10020001, 0, 'ldr r3, =target\nbx r3',
     'argument-or-saved-register')
case('wrong-stack-slot', 0x10020001, 0, body.replace('[sp, #4]', '[sp, #0]'),
     'execution-fault')
case('even-target', 0x10020000, 0, body, 'execution-fault')
(WORK / 'receipt.json').write_text(json.dumps({
    'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'unicorn_version': unicorn.__version__, 'cpu': 'Cortex-M0',
    'unicorn_rejects_thumb2': unicorn_rejects_thumb2,
    'armv6m_assembler_rejects_thumb2': True,
    'tools': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in TOOLS.items()},
    'cases': rows}, indent=2) + '\n')
print('PASS native veneers: 8 Unicorn executions, 3 runtime controls, '
      'ARMv6-M encoding agreement and rejected Thumb-2 control; ' + str(WORK))
print('Unicorn Cortex-M0 model rejects Thumb-2 MOVW: ' + str(unicorn_rejects_thumb2))
