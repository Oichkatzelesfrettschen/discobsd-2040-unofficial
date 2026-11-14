# Group, device and login-record header inclusion

## Requirement and falsifier

Each public header declares its own type prerequisites and tolerates repeated
inclusion. The existing ARM layouts and function signatures remain stable.
`grp.h` owns a signed `int` group identifier, `kmemdev` accepts zero arguments,
and `lastlog.h` follows the selected `time_t` view. Smaller C retains its
32-bit time view even when `_TIME_BITS=64` appears in its preprocessing input.
Full POSIX group type and namespace conformance remain separate work.

The baseline at `8016576b` fails standalone `grp.h` compilation because `gid_t`
and `size_t` lack declarations. The retained discovery also identifies missing
`dev_t` and `time_t` prerequisites and repeated group/login-record definitions.
The implementation supplies the owning type headers and inclusion guards.
The predicted movement is acceptance of standalone and repeated inputs while
layout mutations and argument-bearing `kmemdev` calls continue to reject.
A layout change, accepted malformed control or rejected valid include order
falsifies the repair.

## Source authority and caller closure

`rg -n 'kmemdev' sys/kern/syscalls.master sys/dev/mem.c
usr.sbin/devupdate/devupdate.c` locates the zero-argument syscall entry, its
kernel handler and both user calls. The generated libc wrapper returns
`dev_t` through the syscall result. The explicit `(void)` declaration describes
that established call boundary.

`include/sys/_time_t.h` owns both time views and the Smaller C exclusion from
the wide view. Including that owner avoids importing unrelated system types
into the login-record header. `include/sys/types.h` owns the group and device
types. The inherited provenance and all public fields remain intact.

The matched distribution's captured consumers identify 17 distinct group-header
sources and the login program's login-record include. `devupdate` remains
outside the maintained `usr.sbin` selection; its separate compilation belongs
in caller evidence, without treating it as a shipped program. The excluded
consumer fails compilation with identical diagnostics against
baseline and candidate headers: incomplete legacy types and warning-policy
violations remain. This comparison establishes no executable devupdate
qualification. The complete clean shipped build and dependency closure pass.
The generated
image manifest owns the installed aliases; the unfiltered source manifest also
names optional components and therefore supplies a different denominator.

## Calibrated gate

`check-record-header-contracts` evaluates the production compiler flags and
checks GNU17, strict GNU C17 and strict Clang C17 for Cortex-M0+. Its denominator
is three headers, four include orders, two time views and four selectors:
288 valid cases. Each compiler rejects eight header mutations and one
argument-bearing call, producing 27 negative controls. The mutations remove
type prerequisites and guards, change group signedness or login-record width,
and restore the non-prototype device declaration.

The Smaller C lane compiles 24 cross-preprocessed inputs and assembles their
Thumb-1 output. Its malformed-source control requires exit status 1 and the exact syntax
diagnostic. Four diagnostic fixtures reject a signal, an unrelated error,
false success and an unexpected diagnostic stream. These cases
establish compiler acceptance rather than native preprocessing or execution.
The gate records source/tool identities and each command and verdict in
`.build-logs/record-header-contracts.json`. Firmware CI reaches it through
`check-cross` on Linux and macOS. `sys/arch/rp2040/doc/TESTING.md` owns the
execution-class inventory.

```sh
: "${PYTHON:?select the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-record-header-contracts
```

The focused local gate passes 288 valid cases and 27 rejection controls; all
24 Smaller C inputs compile and assemble. The matched resource comparison,
Renode UART0 execution, aggregate qualification and separate ILP32 inventory
also pass. No physical-board execution forms part of this repair.

## Clean distribution and resource evidence

Candidate `b53cdce1` has a complete clean distribution and shipped dependency
closure. The [resource receipt](record-header-resources.json) and
[comparison](record-header-resource-comparison.json) bind it to the retained
baseline in [native-linker-calendar-resource-candidate.json](native-linker-calendar-resource-candidate.json).
That baseline's implementation at `66ce7c5b` matches main `8016576b`; the
`git diff --name-only 66ce7c5b..8016576b` range contains documentation only.
All 162 resource surfaces and every selected archive-member hash remain
unchanged. The root image retains 239 free blocks. No resource exception is
required for this candidate.

The [qualification record](record-header-qualification.json) binds local
receipts by hash. The installed top-level/arpa scan parses all 54 headers in
each of four selector modes, compared with 51 at the baseline. Its default
view discovers 578 external names. Seven newly discoverable names already
have libc definitions: endgrent, getgrent, getgrgid, getgrnam, kmemdev,
setgrent and setgroups. System/netinet/machine header subtrees remain outside
that exploratory denominator; successful parsing establishes neither full API
conformance nor an approved shared-library export set.

Renode boots the freshly built UART0 kernel and flash image and passes its
runtime probes with the pinned peripheral model. An initial invocation stops
on the missing model dependency before execution; the verified retained model
supplies that dependency for the successful run. Physical-board behavior
remains unmeasured.

The aggregate completes at `e36425ce` in 857.855 seconds with build and check
exit status zero. The separate ILP32 inventory at implementation `b53cdce1`
passes 207 entries in 548.374 seconds. The range between these commits changes
documentation only. The qualification record binds both terminal receipts;
the pull request owns reviewed integration state.
