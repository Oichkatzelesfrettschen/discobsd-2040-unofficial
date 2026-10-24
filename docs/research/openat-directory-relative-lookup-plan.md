# Directory-relative open implementation plan

**Goal:** Add a caller-complete `openat(2)` interface whose lookup can start at
an open directory without changing the process working directory, while
preserving the existing `open(2)` ABI and constrained RP2040 resource limits.

**Architecture:** `namei_at(struct nameidata *, int)` stages and classifies the
pathname before consulting the directory descriptor. `namei()` remains a
wrapper that selects `AT_FDCWD`. `vn_open_at()` owns final-component open flags,
and `vn_open()` preserves the current entry point. One shared `copen()` path
allocates and publishes descriptors for `open()` and `openat()`.

**Tech stack:** GNU17 RP2040 kernel, ARM syscall assembly stubs, strict-C17
host and ILP32 fixtures, Smaller C native-header consumers, bmake metadata
generation, and schema-2 shipped-resource receipts.

**Spec:** `docs/research/c17-semantic-migration-plan.md`, ordered queue item 9,
with the namespace residual in
`docs/research/tar-descriptor-timestamp-identity.md`.

## Global constraints

- Preserve Cortex-M0+, ARMv6-M, Thumb-1, software arithmetic, 264 KiB SRAM,
  2 MiB flash, the 128 KiB kernel region, and the 144 KiB process window.
- Preserve every existing syscall number, public layout, and calling
  convention. Add the new interface at the next free syscall number.
- Stage a user pathname before directory-descriptor lookup. An absolute path
  ignores the supplied descriptor and starts at the process root.
- Keep `struct nameidata` byte-identical. Lookup takes its starting-directory
  choice as an argument and acquires its own traversal reference.
- `AT_FDCWD` selects `u.u_cdir`. A relative path with an invalid descriptor
  returns `EBADF`; a valid non-directory descriptor returns `ENOTDIR`.
- `O_NOFOLLOW` rejects a final symbolic link with `ELOOP`; intermediate links
  and a final link followed by a trailing slash retain ordinary lookup rules.
- `O_DIRECTORY` rejects a non-directory result with `ENOTDIR`.
- `O_SEARCH` acquires a directory using search permission without granting
  file reads or writes. It rejects access-mode, create, truncate, and exclusive
  combinations that contradict search-only ownership.
- Existing `open()` behavior remains on `AT_FDCWD`. Empty `openat()` paths
  return `ENOENT` without changing legacy internal empty-path lookup behavior.
- Directory anchoring does not claim confinement beneath that directory.
  `..`, absolute paths and links, and concurrent ancestor rename remain named
  residuals for the later namespace-serialization mechanism.
- Physical-board, persistence, runtime stack, and runtime heap claims remain
  open without their separate measurements and authorization.

## Review focus

- A user-copy fault paired with an invalid descriptor must return the copy
  fault without consulting the descriptor table.
- An absolute path paired with an invalid descriptor must resolve from the
  process root.
- Descriptor allocation, name lookup, device open, lock acquisition, and
  interruption failures must release the new descriptor and inode references.
- A final symbolic link must reach neither truncation nor device-open
  publication under `O_NOFOLLOW`.
- `O_SEARCH` descriptors must support later directory-relative lookup and
  `fchdir()` while ordinary `read()` and `write()` reject them.

---

### Production lookup and open contract

**Files:**

- Create: `tests/kernel/openat_test.c`
- Create: `tests/kernel/openat_mutate.py`
- Modify: `tests/kernel/Makefile`
- Modify: `sys/sys/fcntl.h`
- Modify: `sys/sys/inode.h`
- Modify: `sys/sys/namei.h`
- Modify: `sys/kern/ufs_namei.c`
- Modify: `sys/kern/vfs_vnops.c`
- Modify: `sys/kern/ufs_syscalls.c`
- Modify: `tests/rp2040/namei_path/check_origin.py`

**Interfaces:**

- Consumes: existing `NAMEI_INIT_USER`, pathname-copy, name-cache, inode
  locking, `falloc()`, `getf()`, `vn_open()`, and `open()` contracts.
- Produces: `namei_at(struct nameidata *, int)`,
  `vn_open_at(struct nameidata *, int, int, int)`, kernel `openat()`,
  `AT_FDCWD`, `O_NOFOLLOW`, `O_DIRECTORY`, and `O_SEARCH`.

- [x] Write an ILP32 fixture that links the production lookup/open sources and
  asserts the descriptor, absolute-path, cwd, permission, symlink, flag,
  ownership, allocation, device-open, and interruption cases in the Global
  Constraints and Review Focus sections.
- [x] Add three warning-clean mutations: choose cwd instead of the retained
  descriptor, consult the descriptor for an absolute path, and follow a final
  link under `O_NOFOLLOW`.
- [x] Run `bmake -C tests/kernel check-openat-ilp32` and verify that the base
  compile fails because the interface is absent. Each later mutation must fail
  a named runtime assertion rather than compilation.
- [x] Implement the new lookup and open entry points without adding a field to
  `struct nameidata`. Preserve the live `setjmp(&u.u_qsave)` frame in the
  function that calls `openi()`.
- [x] Update the finite namei-origin inventory for the exact new initialization
  and reset calls.
- [x] Run `bmake -C tests/kernel check-openat-ilp32` and
  `bmake MACHINE=rp2040 check-namei-user-path`; require the production fixture
  and all mutations to pass their declared verdicts.
- [x] Commit the tested kernel mechanism with a component-and-mechanism subject.

### Public ABI, generated dispatch, and native consumers

**Files:**

- Modify: `sys/kern/syscalls.master`
- Regenerate: `include/syscall.h`
- Regenerate: `sys/sys/syscall_dispatch.h`
- Regenerate: `sys/kern/init_sysent.c`
- Regenerate: `sys/kern/syscalls.c`
- Modify: `include/fcntl.h`
- Modify: `lib/libc/arm/sys/Makefile`
- Modify: `lib/libc_aout/libc/Makefile`
- Modify: `distrib/rp2040/mkboardlibc.py`
- Modify: `tests/kernel_metadata/check.py`
- Modify: `tests/kernel_metadata/dispatch_contracts.py`
- Modify: `usr.bin/smlrc/tests/t18_stdio.c`
- Modify: `usr.bin/smlrc/tests/stdio_red_control.py`
- Modify: `share/man/man2/open.2`
- Modify: `share/man/man2/Makefile`

**Interfaces:**

- Consumes: the production kernel `openat()` and flag semantics.
- Produces: syscall 157, public `openat()` declaration, ARM and a.out libc
  wrappers, Smaller C accepted header/member selection, and installed manual
  alias `openat(2)`.

- [ ] Extend the metadata and Smaller C controls first. Run
  `bmake MACHINE=rp2040 check-kernel-metadata` and
  `bmake MACHINE=rp2040 check-c17-smaller-c`; require the missing syscall,
  declaration, and archive member to fail their named assertions.
- [ ] Add syscall 157 with four argument words, regenerate the owned dispatch
  outputs, review the numeric diff, and update the pinned public-header digest.
- [ ] Add the public declaration, both libc members, and board-libc
  classification. Update the manual with descriptor, absolute-path, empty-path,
  search-only, final-link, and residual-confinement behavior.
- [ ] Run the metadata, Smaller C, public-header, and exact target compiler
  gates; require warning-clean acceptance and calibrated rejection controls.
- [ ] Commit the caller-complete public ABI and generated outputs.

### Evidence ledgers and constrained-resource acceptance

**Files:**

- Create: `docs/research/openat-directory-relative-lookup.md`
- Modify: `docs/research/c17-semantic-migration-plan.md`
- Modify: `docs/research/security-c17-frontier.md`
- Modify: `sys/arch/rp2040/doc/TESTING.md`
- Modify: `tools/c17/source-denominator.json`
- Modify: `tools/test-execution-inventory.json`

**Interfaces:**

- Consumes: the production lookup/open mechanism, public ABI, clean parent
  `cae4d630`, and their exact gates.
- Produces: a revision-bound behavior/resource record and the next explicit
  namespace mechanism: rename-serialized beneath-root lookup plus
  identity-conditioned mutation.

- [ ] Add the changed and new source rows to the finite denominator, assigning
  this mechanism and claim-specific acceptance states without converting
  unmeasured runtime or hardware fields to passing states.
- [ ] Register the ILP32 execution and mutation controls in the test inventory
  and `TESTING.md`.
- [ ] Run focused compiler, metadata, ILP32, inventory, and source-denominator
  gates, then run the logged clean build and logged non-hardware check.
- [ ] Capture matched schema-2 parent and candidate distributions using the
  recorded toolchain. Compare both kernels, static SRAM, every installed a.out,
  packed-root bytes and blocks, native files, and selected archive members.
- [ ] Capture exact-command object sizes and `-fstack-usage` results for
  `namei`, `namei_at`, `vn_open`, `vn_open_at`, `copen`, `open`, and `openat`.
- [ ] Record measured deltas, hashes, commands, residuals, and any bounded
  positive exception. Keep descriptor pressure, runtime peaks, persistence,
  confinement, and physical-board execution explicit.
- [ ] Commit the evidence record and ledger updates.
- [ ] Review the complete branch against the spec, fix every important finding
  through a failing test, and run the full non-hardware gate again.
