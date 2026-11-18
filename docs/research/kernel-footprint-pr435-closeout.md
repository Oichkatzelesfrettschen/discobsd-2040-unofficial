# Kernel footprint and K&R migration: PR #435 closeout

PR #435 (branch `codex/c17-thumb-alignment`, archived as
`archive/codex-c17-thumb-alignment` at `7aed0038`) holds 21 commits over 115
files, is 1,311 commits behind `main` and conflicts in 48 files, so it cannot
merge as a branch. This record states, for every commit and every mechanism it
carries, whether `main` already has the mechanism, has a successor, received it
in this change, or lacks it, and what the evidence in the branch and on `main`
says about the lacking ones. With this record on `main` the PR can close; the
branch ref keeps every line for the open items.

Evidence classes. Source: `git grep`, `git log -S` and `nm` on `origin/main` at
`d81b1788`. Build: `bmake MACHINE=rp2040 logged-build` with GCC 16.2.0 and GNU ld
(all six kernels, warning-free). Host test: the gates named below. Board: none
ran. The branch measured with GCC 15.2.1 on its own tree; its numbers are
labeled as such and none is restated as a `main` measurement unless this
record re-measured it.

## Commit table

| Commit | Subject | Disposition on `main` |
| --- | --- | --- |
| `b8150c8a` | hot `struct user` fields in Thumb-1 reach | has: #632 (`b9e9c02a`), #638 (`a0c2b80f`); `user.h` asserts offsets of hot fields `<= 124`; handoff records it |
| `6fe8afd9` | organize the optimization program (docs) | ported: `alignment.md`, `bsd-comparisons.md`, `wear-recovery.md`, `README.md` here, STYLE-GUIDE layout rule; `footprint-candidates.md` replaced by the ledger below |
| `b18eaf76` | adversarial RAM map review | ported and re-measured: `memory-map.md` (the branch's four-row table predates PICO_DIAG and PICOCALC_LEAN) |
| `f019845e` | pack UART scratch, USB ring use | USB counters: has, #629 (`7d779094`), covered by `check-usb-console`; `tools/check_usb_ring.py` is superseded by it. UART packing: lacks, open (ledger 3) |
| `217b6786` | explicit kernel section collection mode | successor: #488 (`395804de`) always links with `-ffunction-sections -fdata-sections --gc-sections`; no mode switch exists or is needed |
| `f8183b86` | bound capacity metric indices | partly has: `capacity_note()` rejects `kind >= CAPACITY_KIND_COUNT` and `invalid-kind` is a mutant (`a3b2fb6b`, `f09ff43e`); the `capacity_refresh()` bound is declined (ledger 8) |
| `fb4fae87` | scope caller-complete K&R units (doc) | declined as a document (ledger 7) |
| `57aeaffb` | proc: prototype `inferior`, `pfind`, `pqinit` | has: prototyped in `0d22c927`; the branch also drops `register` |
| `e9995a8e` | resource: prototype u-area entries | has: `0d22c927` |
| `33e7c878` | resource: typed helpers, `ruadd` by name | has: #631 (`ae81be29`) with its resource test |
| `63352dd7` | tty: prototype clist operations | definitions has: `0d22c927`; the byte-boundary test is ported (`clist_test.c`, this change) |
| `be081d92` | time: prototype definitions | has, except `itimerdecr` and `timevalsub`, which stay K&R inside `#ifdef NOT_CURRENTLY_IN_USE` and compile to no code |
| `3867ff86` | libc: prototype the `err` v-forms | has: #630 (`4630f792`) and `check-libc-err` (`tests/libc_contracts/err_test.py`); the branch's `tests/libc_err/` is an earlier duplicate of that oracle |
| `6f7be434` | config: separate controller and device probe types | successor: `main` types the slots with `struct controller_driver` and `struct driver` (`0d22c927`); the branch's single `struct driver` with `d_ctlr_init` and `d_device_init` is a different spelling of the same contract; not adopted |
| `42b49f48` | enforce scoped prototypes, record migration evidence | enforcement flags lack (ledger 5); evidence JSON declined (ledger 7); `check-libc-err` has |
| `27881ece` | own assembly preprocessing, confine archive outputs | lacks (ledger 4) |
| `2b387882` | separate diagnostic builds | has: #637 (`fdd25393`) PICO and PICO_DIAG, plus PICOCALC_LEAN configurations |
| `494c3230`, `ac6afe1d` | merges of the above | no content of their own |
| `8c8fc442` | omit disabled trace metadata, verify linked tables | trace names: successor (ledger 9). Image dispatch verifier: lacks (ledger 2) |
| `7aed0038` | opt-in halfword syscall dispatch | lacks (ledger 1) |

## Twenty files the branch adds that `main` lacks

| File | Disposition |
| --- | --- |
| `docs/research/rp2040-optimization/{README,alignment,bsd-comparisons,memory-map,wear-recovery}.md` | ported, corrected |
| `footprint-candidates.md`, `kr-migration.md`, `kr-migration-admission.json`, `kr-migration-resources.json` | not imported; conclusions kept in the ledger, bytes in the archive ref |
| `compile/PICO_DIAG/*` (4 files) | superseded by `main`'s PICO_DIAG |
| `sys/kern/syscall_offsets.ld.S`, `syscall_roots.ld.S` | open with compact dispatch (ledger 1) |
| `tests/kernel_metadata/compact_dispatch.py`, `tools/verify_rp2040_dispatch.py` | open (ledger 1, 2) |
| `tests/libc_contracts/err_probe.c`, `err_test.py`, `tests/libc_err/*` | `err_probe.c` and `err_test.py` exist on `main` in an evolved form; `tests/libc_err` superseded |
| `tests/rp2040/dhara_amplification/{check-recovery.sh,recovery_test.c}` | ported, calibrated |
| `tools/check_usb_ring.py` | superseded by `check-usb-console` |
| `tools/rp2040_footprint_probe.py` | not carried (ledger 6); run unchanged from the archive ref |

## Ported in this change

- `tests/rp2040/dhara_amplification/check-recovery.sh` and `recovery_test.c`,
  wired into `check-dhara-amplification` and described in TESTING.md. Known
  good: the production journal passes all 464 complete-operation cuts. Known
  bad: early acknowledgement, a flipped programmed byte and a skipped metadata
  page each fail with their expected status. The partial-prefix run reports
  `LIMIT` on `main` (mixed sector at cut 26) and `PASS` against the journal of
  `rp2040/dhara-torn-checkpoint` at `30fed2a1`, so the oracle separates the
  broken and repaired journals. The branch's script required exit 1 from that
  run and would have failed on the repaired journal; it now accepts either
  outcome and prints which one occurred. The headers carry the GizmoBSD grant.
- `boundary_bytes` in `tests/kernel/clist_test.c`: `putc`, `getc` and `unputc`
  round-trip 0x00, 0x7f, 0x80 and 0xff and leave the pool whole. Calibration: a
  copy of `unputc` that widens through `unsigned char` fails it (2 of 340
  checks); unmodified, 340 of 340 pass.
- Documents: the four notes and the index above, a STYLE-GUIDE layout rule for
  research programs, and a corrected `-O2` section in the footprint handoff.
  Every number each note cites was re-read against `main`; the changes
  from the branch text are listed in the notes themselves (six-configuration
  RAM table, 1,024-byte `struct user`, 5,944-byte worst margin, oracle result).

## Ledger of mechanisms `main` lacks

Kernel text and RAM before and after: no kernel source changed, so every
delta is 0. Baseline at `d81b1788` (text / data / bss from `size`, bytes):
PICO 98,031 / 272 / 42,936; PICO_UART 89,985 / 216 / 18,208; PICO_DIAG
100,565 / 272 / 42,936; PICOCALC 123,095 / 448 / 56,072; PICOCALC_LEAN
117,101 / 448 / 51,472; PICOCALC_LEAN_DIAG 119,635 / 448 / 51,568.
`check-kernel-headroom` passes for all six (smallest headroom 7,496 bytes,
PICOCALC).

1. **Compact halfword syscall dispatch (`7aed0038`).** Needs re-implementation;
   owner decision. Branch measurement (GCC 15.2.1, branch tree): 308 to 320
   flash-span bytes saved in five kernels, no data or BSS change, 13 Cortex-M0
   instruction runs over all 163 selections and two decoder mutants. On `main`
   the pointer table `syscall_handlers` is 652 bytes (163 entries,
   `nm -S`), so the gross table saving is 326 bytes before decoder and
   literal cost. It adds two generated linker fragments (1,238 lines), changes
   the generator and the Makefiles of every configuration, and reads and
   edits the `.text` layout that PR #662 is reordering; it should follow
   #662. Boot and full syscall-path execution are unrun even on the branch.
   Estimate 1.5 to 2 days. Owner decision: opt-in flag or default, and whether
   310 bytes justify a second dispatch representation.
2. **Linked-image dispatch verifier (`tools/verify_rp2040_dispatch.py`,
   `check-dispatch-image`).** Needs re-targeting to the default tables;
   lacks. It checks the 163 handler addresses of each `unix.elf` against the
   selected master rows. `main` verifies the host-side generation and
   selection (`check-kernel-metadata`), not the linked table. Estimate half a
   day, independent of ledger 1. Owner decision: whether the image-level check
   earns a gate while the table is generated and host-tested.
3. **UART scratch packing (`f019845e`).** Re-implementation of 3 lines of
   `kern.ldscript` and the flash-swap link verifier. `main` still places
   `flscratch` at `0x20041f00` for PICO_UART (`nm`), so SRAM5 stays in use. The
   change moves placement only: 0 bytes of text, data and BSS; its benefit is
   an idle SRAM5, which only a supply-current measurement can value. Waits
   for #662, which edits `kern.ldscript`. Estimate 2 hours. Owner decision.
4. **Native assembly preprocessing (`27881ece`).** Lacks: no `.S.o` rule in
   `share/mk/sys.mk` and no `.NOPATH` in `lib/libc_aout/libc/Makefile`.
   It protects hosts whose make defaults to traditional CPP, where `##` in
   the ROM-float assembly stays unexpanded. No size effect; the branch's
   extension of `usr.bin/as/tests/thumb-compile.sh` is its calibration (a
   traditional-CPP control must fail). `main` builds on this Linux host
   without it, so the benefit is on other hosts and is unmeasured here.
   Estimate 2 to 3 hours. Owner decision (previously listed as waiting).
5. **Strict-prototype enforcement for four kernel units (`42b49f48`).**
   Lacks. Adding `-Wstrict-prototypes -Wold-style-definition` to
   `kern_proc`, `kern_resource`, `tty_subr` and `kern_time` needs no source
   change on `main`: measured, all four compile with `-Werror` under the PICO
   flags and produce the same object text (140, 894, 1,244 and 1,232 bytes).
   The change is `files.rp2040` plus the six generated Makefiles (kept in
   step by `check-config-generated-sync`) and the matching flags in
   `tests/kernel/check_correctness.py`. Zero bytes, class-retiring gate.
   Left to the owner because the brief for this change reserves kernel edits
   for measured savings. Estimate 1 to 2 hours.
6. **Linker alignment sort and the footprint probe (`2b387882`).** Re-implement
   as an opt-in `KERNEL_SORT_ALIGNMENT` flag. Re-measured on `main` with the
   production link line plus `--sort-section=alignment`:

   | Configuration | Text+data delta | BSS-extent delta | Flash-span delta (probe) |
   | --- | ---: | ---: | ---: |
   | PICO | -76 | -176 | -256 |
   | PICO_UART | -69 | -72 | 0 |
   | PICO_DIAG | -75 | -64 | -256 |
   | PICOCALC | -95 | -40 | 0 |
   | PICOCALC_LEAN | -96 | -48 | not probed |
   | PICOCALC_LEAN_DIAG | -95 | -32 | not probed |

   Each text+data delta is a real reduction in loaded bytes. The span deltas
   are multiples of 256 and appear only where the saving crosses the
   256-byte `.board` alignment boundary (inference from `BOARD_ALIGN = 256`;
   not isolated by a separate run). `--sort-common=descending`, `--relax`,
   `--no-relax` and linker `-O2` change nothing in PICO, PICO_UART and PICOCALC
   (direct relink). The flash, SwapRAM and divider checks pass on the PICO,
   PICO_UART, PICOCALC and PICO_DIAG sorted links. Boot is unrun. Estimate 1
   hour (flag, parameter stamp, generated-sync test). The probe itself is not
   carried: it is a measurement tool with no gate role, its
   `--hot-clock-bank` arm depends on text that has moved, and it runs
   unchanged from the archive ref (`git show
   archive/codex-c17-thumb-alignment:tools/rp2040_footprint_probe.py`; it needs
   `PYTHON`, Capstone and pyelftools and knows only PICO, PICO_UART, PICOCALC,
   PICOCALC_PSRAM and PICO_DIAG). Owner decision: opt-in or default.
7. **K&R inventory documents (`fb4fae87`, `kr-migration*`).** Declined. The
   lexical counts are bound to `2b387882`; `main` has since converted most of
   the listed units and the C17 program tracks the remainder in
   `tools/c17` and `c17-semantic-migration-plan.md`. The remaining K&R
   definitions in the four units above are the two dead ones in
   `kern_time.c`. Bytes stay in the archive ref.
8. **`capacity_refresh()` bound (`f8183b86`).** Declined. The diagnostic it
   answered no longer arises: with `capacity_note()` admitting the kind,
   `subr_capacity.c` compiles at `-O2` under the PICOCALC flags with
   `-Werror` (520 text bytes against 480 at `-Os`). The extra check adds code
   for a caller set that loops `kind < CAPACITY_KIND_COUNT`.
9. **Trace-name source closure (`8c8fc442`).** Successor, no work: `main`
   links `syscalls.o` into every kernel but collects it. `nm` finds no
   `syscall_name`, pool or span symbol in PICO, PICO_UART, PICOCALC_LEAN, and
   three symbols in PICO_DIAG, PICOCALC and PICOCALC_LEAN_DIAG (the
   SYSTRACE kernels). The branch measured 1,196 to 1,200 flash bytes; the
   collected link realizes the same saving. The superstring packing of the
   names is `c0195f13`.
10. **Lean PICO and PICO_DIAG.** Has, #637. On `main` PICO_DIAG costs 2,534
    text bytes over PICO with equal data and BSS (the branch measured a
    1,396-byte span difference before trace closure and 2,592 after).
11. **Anchored hot scalars, 32 KiB SwapRAM link bound.** Anchored scalars: the
    branch rejected its clock-bank prototype (+8 bytes everywhere); no
    action. SwapRAM 32 KiB: re-run on `main` (probe `--swapram-32`): PICO and
    PICO_DIAG link with 2,632 bytes of post-exchange margin (was 5,408 on the
    branch tree); PICOCALC fails the reserve assertion; PICO_UART has no
    pool. Default unchanged; the decision belongs to wear telemetry
    (`rp2040-optimization/wear-recovery.md`).

## Reimplementation budget

Five mechanisms need re-implementation rather than a port (ledger 1 to 4 and
6; item 5 is a one-line-per-file flag change counted with them): about 3
working days in total, 2 of them for compact dispatch. They are listed, not
done, because the brief caps re-implementation at three. None of them is
needed to close the PR.

## Owner decisions open

Compact dispatch default or opt-in (1); an image-level dispatch gate (2); UART
SRAM5 packing and its supply-current evidence (3); the `.S.o` rule (4);
strict-prototype flags on four kernel units (5); the alignment sort as opt-in
or default (6). Each decision names its measured cost above. Wear policy waits
for payload-integrity design (`wear-recovery.md`), whose repair for the half
page counterexample sits on `rp2040/dhara-torn-checkpoint`.

## Reproduction

```sh
: "${PYTHON:?set PYTHON}"; export PYTHON
bmake MACHINE=rp2040 -j"$(sh tools/online-cpus.sh)" logged-build
bmake MACHINE=rp2040 check-kernel-headroom
bmake MACHINE=rp2040 check-dhara-amplification
python3 -I <scratch>/occupancy.py   # nm -S over the six unix.elf files (memory-map.md)
```

The sorted links reuse each configuration's `LD`, `LDFLAGS`, `SYSTEM_OBJ`,
`swapunix.o` and `vers.o` from `bmake -V` with `--sort-section=alignment` added.

## Landed successors

`C17_PROTO_FLAGS` covers the four kernel units in the ledger. The linked
syscall gate checks handler pointers, argument counts, and trace names in
all six kernel configurations. The two ELF fixtures reverse the expected
trace selection and corrupt trace bounds and name bytes.

The shared assembly suffix rule owns ISO preprocessing.
`lib/libc_aout/libc/Makefile` confines object targets to its own directory
even when a sibling library already has ELF outputs. The cross assembler
tier calibrates the ROM cell against an inherited traditional CPP command
and builds both archive representations from the same source.
