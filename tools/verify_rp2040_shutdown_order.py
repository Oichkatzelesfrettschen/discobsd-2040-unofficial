"""Verify compiled RP2040 shutdown ordering in Cortex-M0+ objects."""

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

INSTRUCTION_PATTERN = re.compile(
    r"^[ \t]*(?P<address>[0-9a-f]+):[ \t]+"
    r"(?P<encoding>[0-9a-f]{4}(?:[ \t]+[0-9a-f]{4})?)[ \t]+"
    r"(?P<mnemonic>[a-z][a-z0-9.]*)"
    r"(?:[ \t]+(?P<operands>.*?))?[ \t]*$",
    re.IGNORECASE | re.MULTILINE,
)
CONDITIONAL_BRANCHES = {
    "bcc",
    "bcs",
    "beq",
    "bge",
    "bgt",
    "bhi",
    "bhs",
    "ble",
    "blo",
    "bls",
    "blt",
    "bmi",
    "bne",
    "bpl",
    "bvc",
    "bvs",
    "cbnz",
    "cbz",
}
PROGRAM_COUNTER_DESTINATIONS = {
    "adc",
    "adcs",
    "add",
    "adds",
    "adr",
    "and",
    "ands",
    "asr",
    "asrs",
    "bic",
    "bics",
    "eor",
    "eors",
    "ldr",
    "lsl",
    "lsls",
    "lsr",
    "lsrs",
    "mov",
    "movs",
    "mvn",
    "mvns",
    "orr",
    "orrs",
    "ror",
    "rors",
    "rsb",
    "rsbs",
    "sbc",
    "sbcs",
    "sub",
    "subs",
}


class VerificationError(Exception):
    """Compiled shutdown ordering differs from the required sequence."""


@dataclass(frozen=True)
class Instruction:
    """One decoded instruction line from objdump output."""

    address: int
    size: int
    mnemonic: str
    operands: str


def parse_instructions(disassembly):
    instructions = []
    for match in INSTRUCTION_PATTERN.finditer(disassembly):
        encoding = match.group("encoding")
        instructions.append(
            Instruction(
                address=int(match.group("address"), 16),
                size=2 * len(encoding.split()),
                mnemonic=match.group("mnemonic").lower(),
                operands=(match.group("operands") or "").strip(),
            )
        )
    if not instructions:
        raise VerificationError("objdump output contains no instructions")
    addresses = [instruction.address for instruction in instructions]
    if len(addresses) != len(set(addresses)):
        raise VerificationError("objdump output contains duplicate addresses")
    if addresses != sorted(addresses):
        raise VerificationError("objdump instructions are not address ordered")
    return instructions


def relocation_addresses(disassembly, symbol_name):
    pattern = re.compile(
        rf"^[ \t]*([0-9a-f]+):[ \t]+R_ARM_THM_CALL[ \t]+"
        rf"{re.escape(symbol_name)}(?:[ \t]|$)",
        re.IGNORECASE | re.MULTILINE,
    )
    return [int(match.group(1), 16) for match in pattern.finditer(disassembly)]


def mnemonic_base(mnemonic):
    return mnemonic.removesuffix(".n").removesuffix(".w")


def branch_target(instruction):
    target_match = re.search(
        r"(?:^|,\s*)([0-9a-f]+)\s*(?:<|$)",
        instruction.operands,
        re.IGNORECASE,
    )
    if target_match is None:
        raise VerificationError(
            f"branch at 0x{instruction.address:x} has no direct target"
        )
    return int(target_match.group(1), 16)


def instruction_successors(instructions, instruction_index):
    instruction = instructions[instruction_index]
    base_mnemonic = mnemonic_base(instruction.mnemonic)
    next_address = (
        instructions[instruction_index + 1].address
        if instruction_index + 1 < len(instructions)
        else None
    )
    if (
        next_address is not None
        and next_address != instruction.address + instruction.size
    ):
        next_address = None

    if base_mnemonic == "b":
        return (branch_target(instruction),)
    if base_mnemonic in CONDITIONAL_BRANCHES:
        if next_address is None:
            raise VerificationError(
                f"conditional branch at 0x{instruction.address:x} has no "
                "fallthrough"
        )
        return (branch_target(instruction), next_address)
    destination = instruction.operands.split(",", 1)[0].strip()
    register_list_writes_pc = (
        base_mnemonic == "pop" or base_mnemonic.startswith("ldm")
    ) and re.search(r"\bpc\b", instruction.operands) is not None
    if (
        base_mnemonic in {"bkpt", "bx", "svc", "tbb", "tbh", "udf"}
        or register_list_writes_pc
        or (
            base_mnemonic in PROGRAM_COUNTER_DESTINATIONS
            and destination == "pc"
        )
    ):
        return ()
    if next_address is None:
        return ()
    return (next_address,)


def control_flow(disassembly):
    instructions = parse_instructions(disassembly)
    address_to_index = {
        instruction.address: instruction_index
        for instruction_index, instruction in enumerate(instructions)
    }
    successors = {}
    for instruction_index, instruction in enumerate(instructions):
        instruction_edges = instruction_successors(
            instructions, instruction_index
        )
        for target_address in instruction_edges:
            if target_address not in address_to_index:
                raise VerificationError(
                    f"control transfer at 0x{instruction.address:x} targets "
                    f"unknown address 0x{target_address:x}"
                )
        successors[instruction.address] = instruction_edges
    return instructions, address_to_index, successors


def reaches_before_stop(successors, start_address, target_address, stop_address):
    pending = [start_address]
    visited = set()
    while pending:
        address = pending.pop()
        if address == target_address:
            return True
        if address == stop_address or address in visited:
            continue
        visited.add(address)
        pending.extend(successors[address])
    return False


def is_reachable(successors, start_address, target_address):
    pending = [start_address]
    visited = set()
    while pending:
        address = pending.pop()
        if address == target_address:
            return True
        if address in visited:
            continue
        visited.add(address)
        pending.extend(successors[address])
    return False


def require_all_paths_reach(
    successors,
    start_address,
    target_address,
    target_name,
    active_addresses=None,
    proven_addresses=None,
):
    if start_address == target_address:
        return
    if active_addresses is None:
        active_addresses = set()
    if proven_addresses is None:
        proven_addresses = set()
    if start_address in proven_addresses:
        return
    if start_address in active_addresses:
        raise VerificationError(
            f"control flow cycles at 0x{start_address:x} before "
            f"{target_name}"
        )
    instruction_edges = successors[start_address]
    if not instruction_edges:
        raise VerificationError(
            f"control flow exits at 0x{start_address:x} before "
            f"{target_name}"
        )
    active_addresses.add(start_address)
    for target in instruction_edges:
        require_all_paths_reach(
            successors,
            target,
            target_address,
            target_name,
            active_addresses,
            proven_addresses,
        )
    active_addresses.remove(start_address)
    proven_addresses.add(start_address)


def require_contiguous_successor(successors, address, event_name):
    instruction_edges = successors[address]
    if len(instruction_edges) != 1:
        raise VerificationError(
            f"{event_name} at 0x{address:x} must have one contiguous "
            "successor"
        )
    return instruction_edges[0]


def instruction_addresses(instructions, mnemonic, operand_pattern=None):
    addresses = []
    for instruction in instructions:
        if mnemonic_base(instruction.mnemonic) != mnemonic:
            continue
        if operand_pattern is not None and re.fullmatch(
            operand_pattern, instruction.operands
        ) is None:
            continue
        addresses.append(instruction.address)
    return addresses


def verify_boot(disassembly):
    instructions, address_to_index, successors = control_flow(disassembly)
    helper_calls = relocation_addresses(disassembly, "rp2040_shutdown_sync")
    if len(helper_calls) != 1:
        raise VerificationError(
            "boot must call rp2040_shutdown_sync exactly once"
        )
    helper_call = helper_calls[0]
    if helper_call not in address_to_index or mnemonic_base(
        instructions[address_to_index[helper_call]].mnemonic
    ) not in {"bl", "blx"}:
        raise VerificationError("shutdown helper relocation is not a call")
    final_masks = instruction_addresses(instructions, "cpsid", r"i")
    if len(final_masks) != 1:
        raise VerificationError("boot must contain one final cpsid i")
    final_mask = final_masks[0]
    entry_address = instructions[0].address
    if not reaches_before_stop(
        successors, entry_address, helper_call, final_mask
    ):
        raise VerificationError(
            "boot cannot reach shutdown synchronization before the final mask"
        )
    helper_return = require_contiguous_successor(
        successors, helper_call, "shutdown helper call"
    )
    require_all_paths_reach(
        successors,
        helper_return,
        final_mask,
        "the final interrupt mask",
    )
    for mask_successor in successors[final_mask]:
        if is_reachable(successors, mask_successor, helper_call):
            raise VerificationError(
                "boot can reenter shutdown synchronization after the final "
                "mask"
            )


def verify_helper(disassembly):
    instructions, address_to_index, successors = control_flow(disassembly)
    interrupt_masks = instruction_addresses(instructions, "cpsid", r"i")
    if len(interrupt_masks) != 1:
        raise VerificationError("shutdown helper must contain one cpsid i")
    interrupt_mask = interrupt_masks[0]
    sync_calls = relocation_addresses(disassembly, "sync")
    if len(sync_calls) != 1:
        raise VerificationError(
            "shutdown helper must call sync exactly once"
        )
    sync_call = sync_calls[0]
    if sync_call not in address_to_index:
        raise VerificationError("sync relocation has no instruction")
    entry_address = instructions[0].address
    if reaches_before_stop(
        successors, entry_address, sync_call, interrupt_mask
    ):
        raise VerificationError("shutdown helper can call sync before masking")
    if not reaches_before_stop(
        successors, entry_address, interrupt_mask, sync_call
    ):
        raise VerificationError(
            "shutdown helper cannot reach its interrupt mask before sync"
        )
    mask_return = require_contiguous_successor(
        successors, interrupt_mask, "shutdown helper mask"
    )
    require_all_paths_reach(
        successors, mask_return, sync_call, "the sync call"
    )
    delay_calls = relocation_addresses(disassembly, "mdelay")
    busy_calls = [
        int(match.group(1), 16)
        for match in re.finditer(
            r"^[ \t]*([0-9a-f]+):[ \t]+[0-9a-f ]+[ \t]+"
            r"bl(?:\.[nw])?[ \t]+[^\n]*<shutdown_busy_buffers>",
            disassembly,
            re.IGNORECASE | re.MULTILINE,
        )
    ]
    if len(delay_calls) != 1 or len(busy_calls) != 2:
        raise VerificationError(
            "shutdown drain must contain one delay and two buffer recounts"
        )
    delay_call = delay_calls[0]
    if delay_call not in address_to_index:
        raise VerificationError("delay relocation has no instruction")
    delay_return = require_contiguous_successor(
        successors, delay_call, "shutdown delay call"
    )
    if not busy_calls[0] < delay_call < busy_calls[1]:
        raise VerificationError(
            "shutdown drain must recount buffers after its delay"
        )
    require_all_paths_reach(
        successors,
        delay_return,
        busy_calls[1],
        "the final buffer recount",
    )


def disassemble(objdump_command, object_path, symbol_name):
    result = subprocess.run(
        [
            objdump_command,
            "-dr",
            f"--disassemble={symbol_name}",
            str(object_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise VerificationError(
            f"{objdump_command} failed for {object_path}: {result.stderr}"
        )
    return result.stdout


def run_selftest():
    valid_forward_boot = (
        "00000000 <boot>:\n"
        "  0:\t2b00\t\tcmp\tr3, #0\n"
        "  2:\td003\t\tbeq.n\tc <boot+0xc>\n"
        "  4:\tf7ff fffe\tbl\t0 <rp2040_shutdown_sync>\n"
        "     4: R_ARM_THM_CALL rp2040_shutdown_sync\n"
        "  8:\te000\t\tb.n\tc <boot+0xc>\n"
        "  c:\tb672\t\tcpsid\ti\n"
        "  e:\tbd10\t\tpop\t{r4, pc}\n"
    )
    valid_backward_boot = (
        "00000000 <boot>:\n"
        "  0:\t2b00\t\tcmp\tr3, #0\n"
        "  2:\td405\t\tbmi.n\t10 <boot+0x10>\n"
        "  4:\t2b00\t\tcmp\tr3, #0\n"
        "  6:\tda03\t\tbge.n\t10 <boot+0x10>\n"
        "  8:\t2b00\t\tcmp\tr3, #0\n"
        "  a:\td001\t\tbeq.n\t10 <boot+0x10>\n"
        "  c:\te008\t\tb.n\t20 <boot+0x20>\n"
        "  e:\t46c0\t\tnop\n"
        " 10:\tb672\t\tcpsid\ti\n"
        " 12:\tbd10\t\tpop\t{r4, pc}\n"
        " 20:\tf7ff fffe\tbl\t0 <rp2040_shutdown_sync>\n"
        "    20: R_ARM_THM_CALL rp2040_shutdown_sync\n"
        " 24:\te7f4\t\tb.n\t10 <boot+0x10>\n"
    )
    helper_return_gap_boot = (
        "00000000 <boot>:\n"
        "  0:\tf7ff fffe\tbl\t0 <rp2040_shutdown_sync>\n"
        "     0: R_ARM_THM_CALL rp2040_shutdown_sync\n"
        "  4:\tffffffff\t.word\t0xffffffff\n"
        "  8:\tb672\t\tcpsid\ti\n"
        "  a:\t4770\t\tbx\tlr\n"
    )
    valid_helper = (
        "00000000 <rp2040_shutdown_sync>:\n"
        "  0:\tb672\t\tcpsid\ti\n"
        "  2:\tf7ff fffe\tbl\t0 <sync>\n"
        "     2: R_ARM_THM_CALL sync\n"
        "  6:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        "  a:\tf7ff fffe\tbl\t0 <mdelay>\n"
        "     a: R_ARM_THM_CALL mdelay\n"
        "  e:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
    )
    mask_bypass_helper = (
        "00000000 <rp2040_shutdown_sync>:\n"
        "  0:\t2800\t\tcmp\tr0, #0\n"
        "  2:\td000\t\tbeq.n\t6 <rp2040_shutdown_sync+0x6>\n"
        "  4:\tb672\t\tcpsid\ti\n"
        "  6:\tf7ff fffe\tbl\t0 <sync>\n"
        "     6: R_ARM_THM_CALL sync\n"
        "  a:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        "  e:\tf7ff fffe\tbl\t0 <mdelay>\n"
        "     e: R_ARM_THM_CALL mdelay\n"
        " 12:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
    )
    recount_bypass_helper = (
        "00000000 <rp2040_shutdown_sync>:\n"
        "  0:\tb672\t\tcpsid\ti\n"
        "  2:\tf7ff fffe\tbl\t0 <sync>\n"
        "     2: R_ARM_THM_CALL sync\n"
        "  6:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        "  a:\tf7ff fffe\tbl\t0 <mdelay>\n"
        "     a: R_ARM_THM_CALL mdelay\n"
        "  e:\t2800\t\tcmp\tr0, #0\n"
        " 10:\td001\t\tbeq.n\t16 <rp2040_shutdown_sync+0x16>\n"
        " 12:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        " 16:\tbd10\t\tpop\t{r4, pc}\n"
    )
    mask_successor_gap_helper = (
        "00000000 <rp2040_shutdown_sync>:\n"
        "  0:\tb672\t\tcpsid\ti\n"
        "  2:\tffff\t\t.short\t0xffff\n"
        "  4:\tf7ff fffe\tbl\t0 <sync>\n"
        "     4: R_ARM_THM_CALL sync\n"
        "  8:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        "  c:\tf7ff fffe\tbl\t0 <mdelay>\n"
        "     c: R_ARM_THM_CALL mdelay\n"
        " 10:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
    )
    delay_return_gap_helper = (
        "00000000 <rp2040_shutdown_sync>:\n"
        "  0:\tb672\t\tcpsid\ti\n"
        "  2:\tf7ff fffe\tbl\t0 <sync>\n"
        "     2: R_ARM_THM_CALL sync\n"
        "  6:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
        "  a:\tf7ff fffe\tbl\t0 <mdelay>\n"
        "     a: R_ARM_THM_CALL mdelay\n"
        "  e:\tffff\t\t.short\t0xffff\n"
        " 10:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n"
    )
    verify_boot(valid_forward_boot)
    verify_boot(valid_backward_boot)
    verify_helper(valid_helper)
    rejected_cases = (
        (
            verify_boot,
            valid_backward_boot.replace(
                "  c:\te008\t\tb.n\t20 <boot+0x20>\n",
                "  c:\te000\t\tb.n\t10 <boot+0x10>\n",
            ),
            "unreachable-helper",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                " 24:\te7f4\t\tb.n\t10 <boot+0x10>\n",
                " 24:\tbd10\t\tpop\t{r4, pc}\n",
            ),
            "return-before-mask",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                " 24:\te7f4\t\tb.n\t10 <boot+0x10>\n",
                " 24:\t4687\t\tmov\tpc, r0\n",
            ),
            "pc-write-before-mask",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                " 24:\te7f4\t\tb.n\t10 <boot+0x10>\n",
                " 24:\te7fe\t\tb.n\t24 <boot+0x24>\n",
            ),
            "cycle-before-mask",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                " 24:\te7f4\t\tb.n\t10 <boot+0x10>\n",
                " 24:\te7fe\t\tb.n\t99 <boot+0x99>\n",
            ),
            "invalid-branch-target",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                "    20: R_ARM_THM_CALL rp2040_shutdown_sync\n", ""
            ),
            "missing-helper-relocation",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                "    20: R_ARM_THM_CALL rp2040_shutdown_sync\n",
                "    20: R_ARM_THM_CALL rp2040_shutdown_sync\n"
                "    20: R_ARM_THM_CALL rp2040_shutdown_sync\n",
            ),
            "duplicate-helper-relocation",
        ),
        (
            verify_boot,
            valid_backward_boot.replace(
                " 12:\tbd10\t\tpop\t{r4, pc}\n",
                " 12:\te005\t\tb.n\t20 <boot+0x20>\n",
            ),
            "helper-after-final-mask",
        ),
        (verify_boot, helper_return_gap_boot, "helper-return-gap"),
        (
            verify_helper,
            valid_helper.replace(
                "  0:\tb672\t\tcpsid\ti\n"
                "  2:\tf7ff fffe\tbl\t0 <sync>\n"
                "     2: R_ARM_THM_CALL sync\n",
                "  0:\tf7ff fffe\tbl\t0 <sync>\n"
                "     0: R_ARM_THM_CALL sync\n"
                "  4:\tb672\t\tcpsid\ti\n",
            ),
            "mask-after-sync",
        ),
        (verify_helper, mask_bypass_helper, "mask-bypass"),
        (
            verify_helper,
            mask_successor_gap_helper,
            "mask-successor-gap",
        ),
        (
            verify_helper,
            valid_helper.replace(
                "  e:\tf7ff fffe\tbl\t0 <shutdown_busy_buffers>\n", ""
            ),
            "missing-final-recount",
        ),
        (verify_helper, recount_bypass_helper, "recount-bypass"),
        (
            verify_helper,
            delay_return_gap_helper,
            "delay-return-gap",
        ),
    )
    for verifier, fixture, case_name in rejected_cases:
        try:
            verifier(fixture)
        except VerificationError:
            continue
        raise VerificationError(f"selftest accepted {case_name}")
    print("PASS RP2040 shutdown-order verifier selftest")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--objdump")
    parser.add_argument("--machdep-object", type=Path)
    parser.add_argument("--helper-object", type=Path)
    return parser.parse_args()


def main():
    arguments = parse_args()
    if arguments.selftest:
        run_selftest()
        return
    if not all(
        (
            arguments.objdump,
            arguments.machdep_object,
            arguments.helper_object,
        )
    ):
        raise SystemExit(
            "--objdump, --machdep-object and --helper-object are required"
        )
    verify_boot(
        disassemble(arguments.objdump, arguments.machdep_object, "boot")
    )
    verify_helper(
        disassemble(
            arguments.objdump,
            arguments.helper_object,
            "rp2040_shutdown_sync",
        )
    )
    print(
        "PASS RP2040 shutdown synchronization precedes final reset handling "
        f"in {arguments.machdep_object.parent.name}"
    )


if __name__ == "__main__":
    main()
