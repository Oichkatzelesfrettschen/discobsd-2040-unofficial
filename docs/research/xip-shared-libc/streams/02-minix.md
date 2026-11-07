# Stream: MINIX

Read-only survey of a local MINIX 3 tree rebased on NetBSD userland, plus the
MINIX wiki. O = observed in source or fetched page; I = inferred.

1. Static is the base-system default. O: `share/mk/bsd.own.mk:13-15` sets, under
   `__MINIX`, `LDSTATIC?= -static` and `MKDYNAMICROOT?= no`, overriding the
   NetBSD default at `bsd.own.mk:858`. `bin/Makefile.inc:8-10` and
   `sbin/Makefile.inc:10` default to `-static` (`?=`, so a build can override).
   MKPIC stays yes (`bsd.own.mk:1258`), so shared libraries build while base
   links statically. The server and driver libc is never shared:
   `minix/lib/libminc/Makefile:22` `MKPIC= no`. The ramdisk ships ld.elf_so and
   libc.so only when `LDSTATIC == -dynamic`
   (`minix/drivers/storage/ramdisk/Makefile:18,99-103`).
2. The MINIX wiki (releases 3.2.0, using shared libraries) gives the reasons:
   dynamic binaries are "more fragile" (a missing ld.so or library file can
   leave the system unusable), "performance is currently lower than equivalent
   static binaries", and boot-time processes and drivers cannot be dynamic. It
   observes that "Building all binaries dynamically makes /bin and /usr/bin 10x
   smaller", without saying whether the shared libraries are counted; its VM
   section adds that "there is no logic yet to share the pages between the same
   instances of shared libraries and executables". No startup-time measurement
   is published.
3. Exec path. O: `minix/lib/libexec/exec_elf.c` maps PT_LOAD segments at
   `p_vaddr + load_offset` and relocates nothing (`:35`, `:213`, `:313`), with
   an allocate-and-copy fallback beside the mmap path (`:263-299`); an image
   that has its own interpreter is refused (`:143-147`). VFS swaps a PT_INTERP
   binary for its interpreter and places ld.so 10 MB below the stack
   (`minix/servers/vfs/exec.c:273-312`). All symbol and relocation work
   happens in the userspace NetBSD ld.elf_so. O: `exec_elf.c:35` accepts
   ET_DYN; `bsd.README:350-352` defaults MKPIE to no and `bsd.sys.mk:181-186`
   withholds PIE flags when `LDSTATIC` is `-static`, so the default build
   produces no static PIE; whether static PIE would run is untested.
4. MINIX carries NetBSD's crunchgen infrastructure
   (`distrib/common/Makefile.crunch`, `bsd.own.mk:499,625`,
   `distrib/sets/lists/comp/mi:22`), but the inspected ramdisk
   (`minix/drivers/storage/ramdisk/Makefile:30-103`) lists separate static
   programs and does not use it.
5. Pre-paging MINIX through 3.1.3 shared separate-I&D text between instances
   of one executable through `find_share` (inode, device and ctime match;
   3.1.3 `servers/pm/exec.c`, ST 1.5 `mm/exec.c`). The paging VM (commit
   c078ec0331, "Basic VM...") replaced it.

Bearing on DiscoBSD:
- MINIX is one MMU-based implementation of dynamic linking; it does not show
  that relocation needs an MMU (I). DiscoBSD's relocation costs require their
  own measurement.
- MINIX kept boot, server, driver and base binaries static by default, citing
  fragility and speed (O). The fragility it names is a missing loader or
  library file; flash storage does not create that dependency, so both designs
  need comparison under one packaging and corruption model (I).
- The 10x figure is a historical observation about program directories, not a
  measure of what sharing libc saves (O).
- MINIX's kernel-side loader stays trivial by pushing relocation to userspace
  (O).

## Resolution of the adversarial review

1. ACCEPT. The wiki states the 10x figure without saying whether libraries are
   counted, and says pages are not yet shared; the body keeps the figure as a
   historical observation only.
2. ACCEPT. `exec_elf.c:263-299` has an allocate-and-copy fallback; the body no
   longer treats MINIX as proof that relocation needs an MMU.
3. ACCEPT. The wiki's failure mode is a missing file; the body asks for one
   packaging and corruption model for both designs.
4. ACCEPT. `distrib/common/Makefile.crunch`, `bsd.own.mk:499,625` and
   `distrib/sets/lists/comp/mi:22` exist; the ramdisk does not use them.
5. Deferred to stream 01, which added an export-collision gate, fixed library
   ordering and made the harness self-contained.
6. ACCEPT. ET_DYN accepted at `exec_elf.c:35`; MKPIE defaults to no
   (`bsd.README:350-352`); PIE flags withheld under `-static`
   (`bsd.sys.mk:181-186`). "Unsupported" became "not produced by default,
   untested".
7. ACCEPT. The Makefiles use `?=`; "force" became "default to".
8. ACCEPT. 3.1.3 `servers/pm/exec.c` still has `find_share`; paging arrived
   with commit c078ec0331.

The resolutions above were verified by the stream's originating agent against
the cited evidence and applied by the orchestrator, because the agent's role
had no write access.
