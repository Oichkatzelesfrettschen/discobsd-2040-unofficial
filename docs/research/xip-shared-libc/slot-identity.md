# Shared-libc callable identity

The shared ABI requires one public function address across application imports,
initialized pointers and pointers returned by libc. Each slot also owns its
Thumb function type and exact ELF extent. The fixed flash reservation remains
49,152 bytes; a successful identity fixture supplies no image-fit claim.

## Mechanism and deciding experiment

GNU ld assignment of a public function symbol to a slot preserves the original
provider's function size. The assignment rejection control reproduces that
extent mismatch for a 12-byte slot. The fixture separates those owners:
the provider has a private implementation alias, its public definition becomes
an unresolved import, and the slot supplies the public definition and extent.
The rewrite changes symbol metadata while preserving section bytes and relocation
indexes. A weak public alias shares its canonical slot with its owning function.

GCC's `-fno-semantic-interposition` can preserve a direct local implementation
call while public address expressions retain their public symbol. The fixture
checks observable pointer and call behavior with and without that option and
after LTO lowers to native relocatable ELF. ELF inspection rejects unsupported
objects and conflicting implementation ownership before writing the separated
object. Every declared alias group has one implementation section, Thumb entry
and extent; individually consistent public/private pairs cannot override that
group contract.

The experiment lives in `tests/libc_xip_slots`. Its symbol separation remains a
test mechanism until full API selection and production build closure establish
all provider and alias ownership. The generated production ABI, native imports,
kernel admission, lifecycle, signal/fork/swap qualification and workload timing
retain their independent requirements.

For flash origin F, slot index i and stride 12, the public Thumb address is
`F + 12*i + 1`. Every name in an alias group has that address. The implementation
alias retains the provider's separate Thumb entry. Public address expressions
resolve through the slot symbol; compiler-local implementation references can
retain their direct call. Only symbol-table entries change during separation.
Section contents, string tables and relocation indexes retain their identities.

Each 12-byte slot saves r3 and LR, loads its implementation address, replaces
the saved return-address word with that target, and restores r3 while branching
through PC. LR retains the original caller return address. Eight temporary
stack bytes expire before the implementation starts. The fixture chooses this
transparent encoding to test ownership; the compact eight-byte dispatcher
retains its independent size, stack and timing decision.

## Replay and evidence boundaries

The root gate supplies four compiled images and seventeen required rejection
controls. A selected Python needs `pyelftools` and Unicorn 2 or later; ARM GNU
GCC, binutils and its LTO plugin supply compilation and linking. Run:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-libc-xip-slots
```

Each successful image executes 222 calls. The 888-call matrix covers two
independent library-state areas, three aligned stack placements, zero values,
ordinary values and unsigned overflow boundaries. An application linked at a
separate SRAM address imports a symbol-only ELF retaining the public function
type, Thumb entry and extent. Direct calls, pointers, weak aliases and callbacks
exercise the same slots. The linker supplies any application far-call veneers;
native-linker veneer generation remains a separate work package.
Every imported public address also equals its inspected library definition.
The C-locale linker collision oracle requires the intended public symbol; an
unrelated private-symbol error supplies no public-interposition evidence.

Fresh UUID evidence directories under `.build-logs/xip-callable-identity-*`
retain commands, diagnostics, native/LTO objects, separated objects, both linked
ELFs, source hashes and a terminal receipt. The separation receipt records the
original and rewritten object hashes and each reviewed implementation owner.
The source hash set includes every direct fixture input. The Git revision and
installed compiler/module versions identify the environment; a dirty worktree
requires direct source-hash comparison before using its receipt.

The [testing specification](../../../sys/arch/rp2040/doc/TESTING.md) records
the root gate and Linux `check-qemu` owner. Production source and executable
formats stay unchanged. The fixtures use explicit GNU alias attributes under
strict C17 compiler diagnostics; they supply no portable alias-language or
Smaller C claim. Unicorn executes ARM instructions with modeled memory. Kernel
admission, process scheduling, swap, MPU protection, actual runtime resource
peaks and physical-board timing remain unmeasured.

## Implementation plan

The approved C17/POSIX roadmap owns the program. This plan expands its callable
identity prerequisite into executable behavior and rejection controls.

- [x] Define C and assembly providers, weak aliases and cross-object consumers.
- [x] Calibrate the public symbol/type/extent oracle against the retained failing
  linker-assignment mechanism and a separately named implementation control.
- [x] Implement checked symbol separation with unchanged section/relocation bytes.
- [x] Exercise canonical pointers and six-argument calls across two independent
  process-state areas, stack placements, compiler visibility choices and LTO.
- [x] Reject omitted separation, mismatched provider ownership, public-definition
  collisions, split aliases, incorrect extents, bypassed pointers and wrong calls.
- [x] Register the gate with the root Makefile, testing specification and Linux
  `check-qemu` CI owner; run focused qualification and existing adjacent gates.
- [ ] Record exact source/tool identities and bounded review before PR integration.

The [source review record](slot-identity-review.md) binds the approved immutable
review and the parent's executed counterexamples and repairs. The initial
fourteen-control result leaves three validation gaps. Their reproductions and
added controls establish the seventeen-control qualification boundary.

An unexpected pointer or extent falsifies the proposed ABI mechanism. An emulator
setup error remains a failed execution; it cannot count as mutation rejection.
Instruction execution establishes functional behavior for the exercised fixtures,
with no kernel, cycle, MPU or physical-board claim. The mechanism uses established
ELF aliases, relocation and Thumb calls; novelty stays limited to their verified
composition under the DiscoBSD ABI.
