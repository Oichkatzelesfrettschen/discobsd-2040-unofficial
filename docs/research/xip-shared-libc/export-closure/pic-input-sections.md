# Full-archive PIC compilation and state references

All 441 members of the ARM libc archive at source revision
`1b132129e9f7462e8cea8fee52e849e46579523b` compile with the shared-library PIC
flags without diagnostics. The archive identity and section measurements live
in [the inspection receipt](pic-input-sections.json). Compilation establishes
instruction generation; shared linking and runtime remain open.

## Inputs and scope

The experiment matches each archive member's name and SHA-256 to a successful
captured compile record from the matched image-clock resource build. The 442
captured libc compilation records supply 441 final archive members. Tool,
source, dependency and retained standard-input hashes match before compilation.
Generated syscall assembly uses its captured standard input. Replay uses the
original compiler arguments, directs every object into an isolated output
folder, and adds these flags:

```text
-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative
```

The retained local experiment lives under
`.build-logs/shared-libc-pic-replay/`: `replay.py` performs compilation,
`receipt.json` binds each result to its capture, and `measure_sections.py`
inspects the objects with pyelftools. These exploratory scripts are retained
experiment inputs, not calibrated acceptance gates. Production archive and
program outputs remain unchanged.

## Input-section costs

| Storage category | Static bytes | PIC bytes |
| --- | ---: | ---: |
| Executable sections | 51,030 | 54,738 |
| Read-only sections | 3,507 | 3,431 |
| Initialized writable sections | 795 | 867 |
| Uninitialized writable sections | 3,749 | 3,749 |

PIC executable and read-only input sections total 58,169 bytes. This exceeds
the retained 49,152-byte flash reservation by 9,017 bytes before final linking.
The sum is neither a final image measurement nor a lower bound: section garbage
collection can remove unused sections, while GOT allocation, alignment, stable
slots, veneers and external compiler helpers can add storage. The public API
contract and a complete link decide feasibility. The reservation stays fixed.

## Process-local state references

The ELF relocation inspection identifies 165 `R_ARM_ABS32` references from
read-only allocated sections to known mutable providers: 124 references to
`errno` and 41 to ROM floating-point dispatch cells. The receipt identifies
each member, section, offset and symbol. The filter covers defined writable
symbols and named startup objects; it does not prove that other relocations
are safe.

`lib/libc/arm/sys/SYS.h` expands its error path to `ldr r1, =errno` followed by
`str r0, [r1]`. `arm-none-eabi-readelf -r` independently confirms that PIC
`__sysctl.o` retains `R_ARM_ABS32 errno` at text offset `0x10`. Compiler PIC
flags preserve that handwritten absolute literal. A flash-resident shared
text image therefore still needs an assembly mechanism that addresses each
process's state through its PIC base. ROM dispatch assembly needs the same
ownership treatment while preserving argument registers and descriptor
alignment.

The next deciding experiment links an explicit API closure with generated
process-local state access, then inspects every allocated relocation and
measures flash and writable state separately. Automatic native veneers,
startup initialization, ABI slots and kernel admission remain required before
shared executables qualify.

## Complete-link envelope

GCC 16.2.0 and GNU binutils 2.47.20260726 link the complete PIC archive with
its target-selected libgcc. The first link rejects unresolved `environ` and
`__progname`, identifying startup ownership. An isolated C fixture supplies
exactly the two definitions from `lib/startup-arm/crt0.c`: `char **environ;`
and `const char *__progname = "";`. This fixture establishes storage ownership
for measurement; executable startup still needs to initialize that storage.
The linker script uses the object-ABI fixture's flash/state placement, selects
`exit` as its entry, and omits the fixture's unrelated `absolute_zero` symbol.

[The complete-link receipt](pic-full-link.json) records the resulting ELF and
compiler-helper identities. The whole-archive link uses `--emit-relocs`,
`--fatal-warnings` and `-z noexecstack`, and emits no diagnostics.

| Linked storage | Bytes |
| --- | ---: |
| Text | 54,928 |
| Read-only data | 3,312 |
| Initialized writable data | 876 |
| GOT | 844 |
| GOT PLT reservation | 12 |
| Uninitialized writable data, including alignment | 3,772 |
| Flash, including initialization image | 59,972 |
| Per-process state, including initialized storage | 5,504 |

The initialized storage totals 1,732 bytes. Its flash initialization image and
writable runtime copy occupy distinct resource surfaces. The complete link
exceeds the 48 KiB flash reservation by 10,820 bytes. Runtime state relocation,
stable ABI slots, assembly state access and startup are still absent, so these
values describe a linked experiment rather than a qualified shared image.

A second link enables `--gc-sections` and roots all 633 existing global libc
definitions with `--undefined`. Inspection verifies that every root remains
defined. [Its receipt](pic-all-exports.json) records the exact root set and
identical 59,972-byte flash and 5,504-byte state totals. Section collection
therefore supplies no reduction for this mechanical export superset. A smaller
explicit public contract or implementation changes need their own closure and
measurements; removing unused shipped-program dependencies alone cannot
justify dropping callable APIs.

The local evidence directory retains `full-link-command.json`, both link
maps, the initial unresolved-owner diagnostics, the startup-state source and
object, and both ELF images. The two links establish symbol resolution and
storage layout only. The 165 absolute references identified above still prevent
process-local shared execution. No runtime or physical-board claim follows
from either link.
