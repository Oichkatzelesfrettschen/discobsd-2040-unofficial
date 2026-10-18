# DPUCP-2024 physical-board acceptance

The attached RP2040 executed the seven DPUCP-2024 first-release surfaces from
source commit `e528c1a1ad499ecb0d6983c45c678c39c03b5241`. The later profile
contract merge `f1dc15845f7cc67361c52c694c45fe5135b2a3ef` changes only profile
documentation and verification. The flashed kernel and root identities remain
the bytes built from the earlier source commit.

The owner approved one flash execution. The kernel UF2 SHA-256 is
`6f60e4962e3d871fe87d04aa3e8e372c697974334bb9ef36bcc6029d6ee3223f`;
the root UF2 SHA-256 is
`370a5a882999fdd58b608936e894a498317957d769d822df6c0184570bcc05a0`.
The checked-in command replaces the host checkout prefix with `${CHECKOUT}`;
the SHA-256 of the exact executed command is retained in the JSON record. The
board resolved through
`/dev/serial/by-id/usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00` to
`/dev/ttyACM0`. Both image transfers reached 100 percent, the flash helper
reported reboot into the kernel, and the command exited zero.

The acceptance runner emitted five-second heartbeats, stopped after 20 idle
seconds or 90 total seconds, and completed in 30.464 seconds with exit status
zero. The terminal queue dropped zero bytes. The receipt, raw transcript and
temporary harness remain external retained artifacts with SHA-256 identities
in `dpucp-2024-board-acceptance.json`; the repository record omits host paths
and prompt-by-prompt editor echo noise.

The board returned the expected results for shell readiness, the mounted root,
`command -v`, `command -V`, `command -p`, reset-state `getopts`, utility
`printf`, `readlink`, `realpath`, binary-clean `uudecode -o -`, timeout child
status 37, timeout expiry status 124 and cleanup. The mounted root reported 979
one-KiB blocks, 695 used and 284 available before cleanup. The UF2 validator
records 410 kernel blocks in `[0x10000000, 0x10019a00)` and 6,144 root blocks
in `[0x10020000, 0x101a0000)`.

The observation establishes the thirteen named cases on one board, one image
pair and one USB console session. Host, cross, Qemu-user and Renode remain
separate evidence classes. Runtime stack and heap peaks remain unmeasured. The
seven Issue 8 rows remain partial and open because these cases do not establish
their complete standards behavior. DPUCP-2024 makes no POSIX or UNIX
certification claim.
