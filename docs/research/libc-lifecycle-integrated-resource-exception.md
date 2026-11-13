# Integrated libc lifecycle resource review

The combined bounded-string, normal-exit and quick-exit candidate adds 624 native
archive bytes and one root data block relative to the bounded-string baseline.
The exit repair adds 28 positive packed bytes across sixteen executables. The
same sixteen packed-byte deltas appear in the retained integrated exit review.
The root has 234 free blocks and 32 free inodes after the combined change.
The owner approves this exact combined resource vector and directs continued
qualification and review. The approval includes the additional adminbox byte;
the original 27-byte exit and 692-byte string approvals retain their identities.

The [profile rule](dpucp-2024.md) states: "An exception records exact deltas,
examined alternatives, retained margins, user benefit, rollback point and
reviewer approval." Earlier approvals retain their isolated candidate identities.
The new root block makes their combination a distinct admission decision.
The default comparison remains a rejection; reductions on other surfaces
supply no offset for any positive entry.

## Source and receipt identity

| Artifact | Source revision | Retained evidence |
| --- | --- | --- |
| Bounded-string baseline | `0ca3930f933037c897e96102eae026cad1e044c8` | [Parent resources](libc-lifecycle-integrated-resource-parent.json) |
| Combined candidate | `a3c7e651258bb008bebc5c491a70818674ed656e` | [Candidate resources](libc-lifecycle-integrated-resource-candidate.json) |
| Default comparison | Both revisions above | [Rejected comparison](libc-lifecycle-integrated-resource-comparison.json) |

Both clean distributions pass complete dependency attribution for all 37
shipped artifact groups. Their collector, measurement tools and toolchain
identities match: ARM GCC 16.2.0, GNU ld 2.47.20260726 and bmake 20260912.
The receipts bind source, completion, command logs, closure and artifact hashes.
The [bounded-string review](libc-bounded-string-resource-exception.md) owns its
separate 692-byte addition and fresh comparison against the syscall baseline.

## Requested limits and retained margins

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 43786 | 44410 | +624 |
| Native archive members | 155 | 158 | +3 |
| Root allocated data blocks | 744 | 745 | +1 |
| Root free blocks | 235 | 234 | -1 |
| Root free inodes | 32 | 32 | 0 |

The native archive adds `_Exit.o`, `at_quick_exit.o` and `quick_exit.o`.
The full library already supplies `_Exit`; the reduced native archive requires
its explicit selection to complete the quick-exit dependency. Each positive
packed-byte entry has its own limit:

| Executable | Parent packed bytes | Candidate packed bytes | Increase |
| --- | ---: | ---: | ---: |
| `/bin/box` | 27895 | 27896 | +1 |
| `/bin/ed` | 7237 | 7239 | +2 |
| `/bin/sysbox` | 26129 | 26130 | +1 |
| `/bin/tar` | 23792 | 23793 | +1 |
| `/sbin/adminbox` | 23872 | 23873 | +1 |
| `/usr/bin/as` | 23300 | 23302 | +2 |
| `/usr/bin/du` | 6956 | 6959 | +3 |
| `/usr/bin/sed` | 12311 | 12312 | +1 |
| `/usr/bin/su` | 11795 | 11796 | +1 |
| `/usr/bin/tee` | 954 | 958 | +4 |
| `/usr/bin/textbox` | 32176 | 32177 | +1 |
| `/usr/bin/utilbox` | 44977 | 44980 | +3 |
| `/usr/games/gamebox` | 21657 | 21658 | +1 |
| `/usr/libexec/getty` | 13971 | 13975 | +4 |
| `/usr/libexec/smlrc` | 41767 | 41768 | +1 |
| `/usr/sbin/update` | 1213 | 1214 | +1 |

The sixteen positive packed-byte entries total 28 bytes. Fifteen entries carry
the original exit repair's 27 positive bytes; the combined approval also covers
the additional adminbox byte. All executable raw sizes, loaded sizes and allocated
packed blocks remain unchanged. Both kernel resource vectors remain unchanged.
The complete comparison has 158 surfaces, with 125 unchanged; the remaining
nonzero entries are the listed positive costs and separately recorded packed
reductions. The largest static process occupancy remains 75,324 bytes in the
196,608-byte window, leaving 121,284 bytes before heap and stack admission.
Runtime peaks and storage traffic remain unmeasured.

## Block arithmetic explains the combined cost

The 43,094-byte native archive before either addition occupies 43 data blocks
of 1,024 bytes and leaves 938 bytes in its final block. Each isolated increase
fits that slack: 692 bytes for strings and 624 bytes for quick exit. Their
sum, 1,316 bytes, exceeds the slack by 378 bytes. The combined archive therefore
requires 44 data blocks. Its existing single-indirect block remains necessary
in both cases. A composition of hash-verified retained archive members produces
44,410 bytes; the clean combined distribution confirms the additional root
block and the free-block decrement. Separate successful block-fit checks
therefore do not establish successful admission of their union.

## Alternatives, benefit and rollback

Omitting native archive members prevents native programs from linking the
advertised interfaces. Separate archive members let existing static programs
retain their selected function sets. Sharing normal and quick registries
violates callback ownership and cleanup rules. Reverting normal exit's
live-head removal loses successfully registered callbacks. A fixed callback
array changes the minimum persistent-state cost; callback-node reuse changes
both object text and BSS and requires its own candidate measurement. These
alternatives do not establish a smaller qualified combined capability.

The benefit is caller-complete C17 quick termination, an independent callback
registry, and correct normal-exit registration order alongside the four string
interfaces. The [quick-exit contracts](quick-exit-contracts.md) and
[normal-exit witnesses](coherent-boundary-lifecycle-witnesses.md) own behavior.
The rollback point is the parent source, which retains bounded strings while
lacking quick exit and retaining the normal-exit callback defect.

The approved exception binds the exact pair, 624 archive bytes, three named
members, one root data block and sixteen positive packed-byte limits above.
The earlier bounded-string approval remains separate. Approval covers no future
growth and establishes no full POSIX conformance or physical-board result.
Combined aggregate, native-compiler, emulator and reviewed integration evidence
retain their own completion requirements.

## Combined behavior qualification

The combined candidate passes `logged-build` followed by
`check-libc-bounded-string`, `check-libc-exit-lifecycle`,
`check-libc-quick-exit`, `check-libc-quick-exit-cross` and
`check-libc-aout-contracts`, with caller-selected `PYTHON`. The archive gate
rejects fourteen missing-member controls across full and reduced libraries.
The focused host and ILP32 runs preserve the string, normal-exit and quick-exit
witnesses; the string sanitizer variant also passes.

`bmake -C tests/libc_contracts MACHINE=rp2040 REQUIRE_QEMU=1 check-qemu`
passes with ARMv6-M execution. Its string and quick-exit mutations reject the
specified broken implementations. This result establishes user-mode instruction
execution under QEMU; physical-board execution remains unrun. The combined
`logged-check` passes at `eacedc9f15733fabd6a71fd64846a3da0be0e3d6`;
its terminal receipt records exit zero at `2026-11-13T00:35:27Z`.
Hosted CI and reviewed integration retain their separate verdicts. The complete `logged-ilp32` inventory passes
193 of 193 variants at documentation commit
`eacedc9f15733fabd6a71fd64846a3da0be0e3d6`, whose production implementation
matches the captured candidate. The terminal receipt records exit zero at
`2026-11-13T00:01:46Z`.

The publication head `edd9bb440729eda0a992cc54015005d39ca6af30` also
passes all 193 ILP32 variants at `2026-11-13T00:14:12Z`. Its automated
review completes with zero findings. Those documentation revisions retain
the captured production implementation.
