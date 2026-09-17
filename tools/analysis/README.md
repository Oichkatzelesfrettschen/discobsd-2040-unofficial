# RP2040 audit analyzers

These scripts came out of a read-only audit of the RP2040 port's compiled
kernel and userland images. They inspect linked binaries (ELF or a.out) and
their disassemblies; none of them write to the source tree. The audit's
narrative findings live in `sys/arch/rp2040/doc/research/audit-findings.md`;
four handback notes with the audit's more detailed working -- a libgcc
`__aeabi_idiv`/`__aeabi_idivmod` size correction, an `NSTATIC` file-table
cross-check, an OMAGIC section-retention cross-check against Ghidra/cscope/
GNU Global, and a `PRINTF_FLOAT` linker-root classification -- live under
`docs/research/audit-handbacks/`.

Every script takes the ELF or a.out image (and, where relevant, a tree root)
as command-line arguments; none hard-code a local path. Run `make check` in
this directory to byte-compile every script.

## Scripts

| script | measures | inputs | third-party |
| --- | --- | --- | --- |
| `reconcile_disassembly.py` | cross-checks Capstone against GNU objdump, LLVM objdump, and LIEF on one ELF's Thumb ranges | linked ELF + boot2-wrapper ELF | capstone, lief, pyelftools |
| `instruction_inventory.py` | counts barrier/interrupt-mask instructions (isb/dsb/dmb/cpsid/cpsie/mrs/msr) by source location | linked ELF | capstone, lief, pyelftools; needs arm-none-eabi-addr2line |
| `thumb_peepholes.py` | finds collapsible long-conditional branches, ADR-compatible literal loads, stack reload/add/pop triples | linked ELF | capstone, pyelftools |
| `branch_relaxation_fixed_point.py` | fixed-point count of long-conditional-branch pairs collapsible after iterated 2-byte address shifts | linked ELF | capstone, lief, pyelftools |
| `dead_primask_saves.py` | finds `mrs primask` saves whose paired cpsid/cpsie transition discards the saved value at its call site | linked ELF + repository tree root | capstone, lief, pyelftools; needs arm-none-eabi-addr2line |
| `primask_liveness.py` | forward-traces each PRIMASK read to its consuming, overwriting, or escaping instruction | linked ELF | capstone, lief, pyelftools; needs arm-none-eabi-addr2line |
| `crosscheck_named_paths.py` | cross-checks Capstone against cstool/radare2/rizin on a fixed list of security-relevant functions | linked ELF + scratch directory | capstone, pyelftools; needs cstool, r2, rizin |
| `section_gc_reachability.py` | reachability from root symbols computed directly from one ELF's symbol table and relocations | one ELF (.o or linked) + `--root` symbols | capstone, pyelftools |
| `aout_reachability.py` | reachability from the entry point, stored function pointers, and explicit linker roots, for one or more a.out/disassembly pairs | a.out binary + `objdump -d` text listing, per program | stdlib only |
| `aout_program_inventory.py` | inventories every a.out/`.dis` pair under a tree: sizes, entry point, PRINTF_FLOAT and `__doprnt_cvt` linker-root status | repository or built-distribution tree root | stdlib only |
| `aout_archive_section_filter.py` | reclassifies `aout_reachability.py` candidates as co-retained/discardable/unmapped against a static archive's section-level call graph | an `aout_reachability.py --output` JSON + a `.a` archive | pyelftools |
| `linked_heatshrink_measure.py` | measures man-page compression ratio and corruption behavior through the exact heatshrink encoder/decoder linked into the kernel, run under emulation | kernel ELF + man1 page directory | pyelftools, unicorn |

## Verified runs

Each command below ran against the pre-built tree at
`rp2040-ondevice-backports` (sibling worktree, read-only), with
`ELF=<tree>/sys/arch/rp2040/compile/PICO/unix`.

### reconcile_disassembly.py

    ${PYTHON} tools/analysis/reconcile_disassembly.py "$ELF" "$TREE/sys/arch/rp2040/compile/PICO/boot2.elf"

    {
      "boot_decoder_counts": {
        "gnu": 0,
        "llvm": 0
      },
      "capstone_undecoded_ranges": [],
      "classified_range_count": 792,
    ...

### instruction_inventory.py

    ${PYTHON} tools/analysis/instruction_inventory.py "$ELF"

    barrier-and-mask mnemonic counts
    cpsid: 91
    cpsie: 13
    dsb: 10
    isb: 216
    mrs: 109
    msr: 115

### thumb_peepholes.py

    ${PYTHON} tools/analysis/thumb_peepholes.py "$ELF"

    long-conditional pairs: 343

    collapsible conditional pairs: 46

### branch_relaxation_fixed_point.py

    ${PYTHON} tools/analysis/branch_relaxation_fixed_point.py "$ELF"

    round 1: 47 newly encodable
    long-conditional pairs: 343
    fixed-point encodable pairs: 47
    instruction-byte upper bound: 94

### dead_primask_saves.py

    ${PYTHON} tools/analysis/dead_primask_saves.py "$ELF" "$TREE"

    paired PRIMASK saves: 68
    discarded PRIMASK saves: 0
    candidate instruction bytes: 0

The default `--compiled-prefix` (`TREE/sys/arch/rp2040/compile/PICO`) matched
this tree's `DW_AT_comp_dir` exactly, so every debug location resolved.

### primask_liveness.py

    ${PYTHON} tools/analysis/primask_liveness.py "$ELF"

    PRIMASK reads: 101
    outcome groups
     52 | arm_intr_disable     | read
     29 | arm_intr_disable     | cycle,read

### crosscheck_named_paths.py

    ${PYTHON} tools/analysis/crosscheck_named_paths.py "$ELF" /path/to/scratch

    {
      "decoder_mismatches": [
        {
          "decoder": "rizin",
          "difference_count": 3,
    ...

(rizin decoded a `.boot2`-adjacent Thumb-2 32-bit encoding as two 16-bit
opcodes at one segment boundary; a genuine cross-decoder disagreement, not a
script fault.)

### section_gc_reachability.py

    ${PYTHON} tools/analysis/section_gc_reachability.py "$ELF" --root Reset_Handler

    {
      "binary": ".../sys/arch/rp2040/compile/PICO/unix",
      "candidates": [
        {
          "address": 268497848,
          "incoming_functions": ["rawrw"],
          "names": ["physio"],
    ...

### aout_program_inventory.py

    ${PYTHON} tools/analysis/aout_program_inventory.py --repo "$TREE" --output /tmp/inventory.json

    {
      "disassembly_count": 208,
      "excluded_count": 3,
      "exclusions": [
        {
          "program": "sys/arch/rp2040/compile/PICO/unix",
          "reason": "... has magic 0x464c457f, expected OMAGIC 0x107"
        },
    ...

(the kernel's own `unix.dis`/`unix` pair is correctly excluded: `unix` is an
ELF, not an a.out; the inventory scope is userland a.out programs.)

### aout_reachability.py

    ${PYTHON} tools/analysis/aout_reachability.py \
        --program cat "$TREE/bin/cat/cat" "$TREE/bin/cat/cat.dis" \
        --program chmod "$TREE/bin/chmod/chmod" "$TREE/bin/chmod/chmod.dis" \
        --output /tmp/reach.json

    {
      "program_count": 2,
      "programs": [
        {
          "aout": ".../bin/cat/cat",
          "candidates": [
            {"address": 536874172, "names": ["realloc"], "size": 104},
    ...

### aout_archive_section_filter.py

    ${PYTHON} tools/analysis/aout_archive_section_filter.py \
        --analysis /tmp/reach.json --archive "$TREE/lib/libc_aout/libc.a" \
        --output /tmp/archive-filter.json

    {
      "archive_member_count": 360,
      "co_retained_archive_bytes": 0,
      "discardable_archive_bytes": 0,
      "post_archive_filter_ceiling_bytes": 1116,
    ...

### linked_heatshrink_measure.py

    ${PYTHON} tools/analysis/linked_heatshrink_measure.py --tree-root "$TREE"

    {"kind": "page", "name": "sh", "raw_bytes": 23879, "encoded_bytes": 13666,
     "ratio": 1.747329138006732, "roundtrip": true, ...}
    {"kind": "page", "name": "ed", "raw_bytes": 20083, "encoded_bytes": 11043,
     "ratio": 1.8186181291315766, "roundtrip": true, ...}

All twelve scripts ran to completion (exit 0) against the pre-built tree;
none needed an input this repository cannot provide once
`bmake MACHINE=rp2040 kernel` has produced
`sys/arch/rp2040/compile/PICO/unix`.
