# Stdio EOF fixed-point resource exception

The repeated-EOF repair adds eight bytes to the installed native libc archive.
Twenty-five shipped programs that select `_filbuf` each add eight loaded and raw
bytes. Twenty-four of those programs add 256 packed bytes in total; `fstat`
retains its packed size. Every packed allocation-block delta is zero. The root
retains 752 allocated data blocks, 227 free blocks and 29 free inodes. Both
kernel resource vectors remain unchanged. Approval of this additional vector
remains pending.

The [compatibility profile](dpucp-2024.md) requires exact deltas, examined
alternatives, remaining margins, a user benefit, a rollback point and reviewer
approval. The callable character-I/O approval supplies no allowance for this
later growth.

## Source and artifact correspondence

The baseline is main `cc71239c77e1a4181c892360770ca089deb10644`.
The complete measured candidate is
`8f9c4d913e11b420034adb4fc24f333a2a645674`. The
[parent](stdio-eof-fixed-point-resources-parent.json),
[candidate](stdio-eof-fixed-point-resources-candidate.json) and
[comparison](stdio-eof-fixed-point-resources-comparison.json) retain identical
collector, measurement-tool and toolchain identities across clean
distributions. Their SHA-256 values are, respectively,
`e0967e57ba12a46277fe44d8f56fcf95155e1c940857ed14d1f46af1d123cccf`,
`3bc5428438343da7d803731540181762c154f18f96aee24ba0f3954ccc2ec1d6`
and `517a0d30d867d18fd4e715be8e93b6a9bd5c4d5b2c9118f9eae1426f4de98810`.
The comparator reports 75 positive and 87 unchanged numeric surfaces in its
162-surface denominator, then rejects the candidate because the exception lacks
approval.

| Shipped surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 51,190 | 51,198 | +8 |
| Selected native members | 174 | 174 | 0 |
| Total changed-program loaded bytes | -- | -- | +200 |
| Total changed-program raw bytes | -- | -- | +200 |
| Total changed-program packed bytes | -- | -- | +256 |
| Total changed-program packed blocks | -- | -- | 0 |
| Root allocated data blocks | 752 | 752 | 0 |
| Root free blocks | 227 | 227 | 0 |
| Root free inodes | 29 | 29 | 0 |

The root image hashes differ because the changed program bytes enter the image.
The filesystem allocation stays constant. The archive occupies the same number
of data blocks in both images. All 34 shipped program records remain in the
denominator; nine programs retain every numeric measurement.

## Program propagation

Every program below adds eight loaded and eight raw bytes. Packed-file
compression turns the same linked-text change into program-specific byte costs.

| Program | Packed-byte delta | Packed-block delta |
| --- | ---: | ---: |
| `/bin/box` | +13 | 0 |
| `/bin/ps` | +11 | 0 |
| `/bin/sysbox` | +4 | 0 |
| `/bin/tar` | +6 | 0 |
| `/sbin/adminbox` | +6 | 0 |
| `/sbin/fsck` | +3 | 0 |
| `/sbin/init` | +20 | 0 |
| `/usr/bin/as` | +16 | 0 |
| `/usr/bin/awk` | +7 | 0 |
| `/usr/bin/compress` | +12 | 0 |
| `/usr/bin/cpio` | +8 | 0 |
| `/usr/bin/find` | +8 | 0 |
| `/usr/bin/fstat` | 0 | 0 |
| `/usr/bin/grepbox` | +10 | 0 |
| `/usr/bin/ld` | +6 | 0 |
| `/usr/bin/login` | +16 | 0 |
| `/usr/bin/menu` | +10 | 0 |
| `/usr/bin/passwd` | +7 | 0 |
| `/usr/bin/sed` | +6 | 0 |
| `/usr/bin/stevie` | +25 | 0 |
| `/usr/bin/su` | +6 | 0 |
| `/usr/bin/textbox` | +24 | 0 |
| `/usr/bin/utilbox` | +19 | 0 |
| `/usr/games/puzzlebox` | +6 | 0 |
| `/usr/libexec/smlrc` | +7 | 0 |

The archive-selection comparison reports no added, removed or newly selected
member. Only `filbuf.o` changes. ARM GCC reports `_filbuf` text growth from 276
to 284 bytes. `-fstack-usage` reports a 72-byte static frame for both objects.
Runtime peak stack, heap, timing and swap traffic remain unmeasured.

## Mechanism, benefit and alternatives

The public `getc` mechanism decrements `FILE._cnt` before it calls `_filbuf`.
An established EOF therefore enters `_filbuf` with a negative counter. The old
EOF return preserved that value, so each later read decremented it again. A
sufficiently long sequence eventually crossed the signed representable range.

The candidate stores zero in `_cnt` before the established-EOF return. Repeated
EOF reads form the bounded transition

```text
(_IOEOF, 0) -> getc decrement -> (_IOEOF, -1) -> _filbuf -> (_IOEOF, 0)
```

The store preserves the existing flag, return value, stream identity and public
macro shape. Buffered and unbuffered `fgetc`, `getc` and `getchar` witnesses
require `_cnt == 0` after repeated EOF. A compiled mutant that removes only the
normalization reaches the declared failure status. Host, ILP32, sanitizer and
Cortex-M0 QEMU executions each cover 3,963 character operations and reject all
six provider mutations.

Changing the public `getc` macro would alter every call site and its fast path.
Changing `_cnt` to an unsigned type would alter the internal FILE contract.
Adding a conditional saturation branch carries more code and state transitions
than the unconditional established-EOF normalization. Leaving the counter
negative preserves the unbounded transition and fails the calibrated mutant.
The single store therefore closes the defect with the smallest measured local
mechanism among these candidates.

The requested exception admits exactly eight native archive bytes, eight
loaded and raw bytes in each named program, and the named packed-byte vector.
It admits no allocation-block, archive-selection, root-capacity or kernel
growth. Main `cc71239c` supplies the rollback point. Additional growth requires
its own matched measurement and decision. The record supplies no complete
POSIX, runtime-peak or physical-board acceptance claim.
