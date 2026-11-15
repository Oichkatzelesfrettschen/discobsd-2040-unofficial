# Program source coverage for the libc export namespace

The namespace checker omits CoreMark because its source search includes six
program families and excludes `benchmarks`. The executed RP2040 manifest
selects `/usr/bin/coremark`. Direct `arm-none-eabi-nm -g --defined-only`
inspection of its six target objects finds no intersection with the defined
globals of `lib/libc.a`. That observation covers the inspected objects;
the checker still requires an executable source-resolution witness.

Source inspection uses the parent loop in
`tools/verify_libc_export_namespace.sh`, the CoreMark entry in
`distrib/rp2040/mi.rp2040`, and `benchmarks/coremark/Makefile`.
The byte-search qualification preserves the omitted-owner disposition as
historical evidence. Shared libc admission requires a complete selected
program namespace before importing addresses through `ld --just-symbols`.

The repair adds `benchmarks` to the existing source search and extends the
checker self-test through its public command line. A fixture under that
family supplies a clean target object, a colliding target object and a
missing required program. The clean fixture passes; the collision and missing
source fail through their named diagnostics. Existing collision allow-list
and unreadable-host-object controls retain their separate scope.

The pre-change prediction is that the clean benchmark fixture fails with
`no built objects for packed program`; the added search parent changes that
fixture to pass while the collision and missing-source controls reject.
The existing `check-libc-export-namespace` target owns calibration and the
built-tree run. ShellCheck, changed-text/comment gates and the profile gate
cover the host-only changes. Target code, images and resource limits retain
their existing identities. Full ABI selection, mutable state, final PIC
sizing, kernel admission and physical timing remain separate obligations.

The accepting fixture fails before the source-search repair through the
predicted diagnostic. After the repair, the same fixture passes and both
rejecting controls produce their required diagnostics. The complete built
tree reports `40 programs, no program global collides with libc.a`, including
CoreMark; its earlier result covers 39 programs. The checker also examines
built optional closure programs, so this count differs from the 34 executable
records in the selected root resource receipt.

Validation runs `bmake MACHINE=rp2040 logged-build`, then
`bmake MACHINE=rp2040 check-libc-export-namespace check-dpucp-profile check-test-execution`.
The build, namespace calibration, profile calibration and 47 execution-inventory
tests pass. `shellcheck -S error tools/verify_libc_export_namespace.sh` passes.
The repair closes the missing source-family lookup; selected object ownership,
stale build products and complete shared-library acceptance retain their
separate contracts.
