# Kernel panic codes

A panic prints `panic: ` and its message on the console and halts with a
dump. Kernel panics that report a broken internal invariant print a short
code instead of a sentence, which keeps the strings out of the kernel image's
flash; this table gives each code its condition and its call site. Codes are
unique per call site: `K` for `sys/kern`, `D` for `sys/dev` and `R` for the
RP2040 port under `sys/arch/rp2040`. Panics a user can act on (no root
partition, an unusable card, swap missing or too small, init lost) keep their
sentence, listed after the table.

`tools/check_panic_codes.py` (`check-panic-codes`) holds this file and the
sources to each other: every code in the sources has exactly one row here
naming its file and function, every row's code appears at that site, and
every other panic literal is in the verbatim list.

<!-- panic-codes:begin -->
| Code | Condition | File | Function |
| --- | --- | --- | --- |
| `K01` | exec spool write | `sys/kern/exec_subr.c` | `exec_spool_write()` |
| `K02` | exec spool read | `sys/kern/exec_subr.c` | `exec_spool_read()` |
| `K03` | exec spool arg length | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K04` | exec spool arg terminator | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K05` | exec spool env length | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K06` | exec spool env terminator | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K07` | exec check | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K08` | exec spool trailing data | `sys/kern/exec_subr.c` | `exec_spool_discard()` |
| `K09` | timeout table overflow | `sys/kern/kern_clock.c` | `timeout()` |
| `K10` | exit | `sys/kern/kern_exit.c` | `proc_zombie()` |
| `K11` | no procs | `sys/kern/kern_fork.c` | `newproc()` |
| `K12` | no procs | `sys/kern/kern_fork.c` | `newproc()` |
| `K13` | newproc vfork | `sys/kern/kern_fork.c` | `newproc()` |
| `K14` | postsig action | `sys/kern/kern_sig.c` | `postsig()` |
| `K15` | ureadc | `sys/kern/kern_subr.c` | `ureadc()` |
| `K16` | uwritec | `sys/kern/kern_subr.c` | `uwritec()` |
| `K17` | tsleep | `sys/kern/kern_synch.c` | `tsleep()` |
| `K18` | wakeup | `sys/kern/kern_synch.c` | `wakeup()` |
| `K19` | setrun | `sys/kern/kern_synch.c` | `setrun()` |
| `K20` | swtch SRUN | `sys/kern/kern_synch.c` | `swtch()` |
| `K21` | setrq | `sys/kern/kern_synch.c` | `setrq()` |
| `K22` | remrq | `sys/kern/kern_synch.c` | `remrq()` |
| `K23` | malloc: size = 0 | `sys/kern/subr_rmap.c` | `malloc()` |
| `K24` | mfree: addr = 0 | `sys/kern/subr_rmap.c` | `mfree()` |
| `K25` | mfree overlap #1 | `sys/kern/subr_rmap.c` | `mfree()` |
| `K26` | mfree overlap #2 | `sys/kern/subr_rmap.c` | `mfree()` |
| `K27` | mfree overlap #3 | `sys/kern/subr_rmap.c` | `mfree()` |
| `K28` | malloc3_contiguous: bad alignment | `sys/kern/subr_rmap.c` | `malloc3_contiguous()` |
| `K29` | malloc3_contiguous: unaligned map | `sys/kern/subr_rmap.c` | `malloc3_contiguous()` |
| `K30` | ring_take: not free | `sys/kern/subr_rmap.c` | `ring_take()` |
| `K31` | ring_next: lost placement | `sys/kern/subr_rmap.c` | `ring_next()` |
| `K32` | malloc3_contiguous_next: bad alignment | `sys/kern/subr_rmap.c` | `malloc3_contiguous_next()` |
| `K33` | malloc3_contiguous_next: no cursor | `sys/kern/subr_rmap.c` | `malloc3_contiguous_next()` |
| `K34` | malloc3_contiguous_next: unaligned map | `sys/kern/subr_rmap.c` | `malloc3_contiguous_next()` |
| `K35` | malloc3_contiguous_next: corrupt map | `sys/kern/subr_rmap.c` | `malloc3_contiguous_next()` |
| `K36` | pipe_close !IPIPE | `sys/kern/sys_pipe.c` | `pipe_close()` |
| `K37` | ttyrub | `sys/kern/tty.c` | `ttyrub()` |
| `K38` | ialloc | `sys/kern/ufs_alloc.c` | `ialloc()` |
| `K39` | getnewbuf: RAMREMAP bp addr | `sys/kern/ufs_bio.c` | `getnewbuf()` |
| `K40` | blkdev | `sys/kern/ufs_bio.c` | `getblk()` |
| `K41` | dup biodone | `sys/kern/ufs_bio.c` | `biodone()` |
| `K42` | no imt | `sys/kern/ufs_inode.c` | `iget()` |
| `K43` | free inode isn't | `sys/kern/ufs_inode.c` | `iget()` |
| `K44` | iget: filesystem identity | `sys/kern/ufs_inode.c` | `iget()` |
| `K45` | iput | `sys/kern/ufs_inode.c` | `iput()` |
| `K46` | itrunc1 | `sys/kern/ufs_inode.c` | `itrunc()` |
| `K47` | itrunc2 | `sys/kern/ufs_inode.c` | `itrunc()` |
| `K48` | ufs namespace gate | `sys/kern/ufs_namei.c` | `ufs_namespace_exit()` |
| `K49` | namei: null cache ino | `sys/kern/ufs_namei.c` | `namei_at()` |
| `K50` | namei: duplicating cache | `sys/kern/ufs_namei.c` | `namei_at()` |
| `K51` | wdir: newblk | `sys/kern/ufs_namei.c` | `direnter()` |
| `K52` | wdir: compact1 | `sys/kern/ufs_namei.c` | `direnter()` |
| `K53` | wdir: compact2 | `sys/kern/ufs_namei.c` | `direnter()` |
| `K54` | rename: lnk dir | `sys/kern/ufs_syscalls.c` | `rename()` |
| `K55` | rename: lost dir entry | `sys/kern/ufs_syscalls.c` | `rename()` |
| `K56` | sync: rofs | `sys/kern/ufs_syscalls2.c` | `ufs_sync()` |
| `K57` | swap image: cancel state | `sys/kern/vm_swap.c` | `swap_image_cancel()` |
| `K58` | swap exchange: geometry | `sys/kern/vm_swap.c` | `swap_exchange_program_sector()` |
| `K59` | swap exchange: geometry | `sys/kern/vm_swap.c` | `swap_exchange_program_sector()` |
| `K60` | swap exchange: reservation | `sys/kern/vm_swap.c` | `swap_exchange_inplace()` |
| `K61` | swap exchange: geometry | `sys/kern/vm_swap.c` | `swap_exchange_inplace()` |
| `K62` | swap exchange: ordering | `sys/kern/vm_swap.c` | `swap_exchange_inplace()` |
| `K63` | swap exchange: unsupported | `sys/kern/vm_swap.c` | `swap_exchange_inplace()` |
| `K64` | swap image: copy owner | `sys/kern/vm_swap.c` | `swap_image_copy_out()` |
| `K65` | swap image: commit owner | `sys/kern/vm_swap.c` | `swap_image_commit()` |
| `K66` | swapout: no reservation | `sys/kern/vm_swap.c` | `swapout()` |
| `K67` | exit: swtch() returned to a zombie | `sys/kern/kern_exit.c` | `exit()` |
| `D01` | mmrw | `sys/dev/mem.c` | `mmrw()` |
| `R01` | flash: scratch buffer reentered | `sys/arch/rp2040/dev/flash.c` | `flscratch_take()` |
| `R02` | flash: page copy during a swap write | `sys/arch/rp2040/dev/flash.c` | `dhara_nand_copy()` |
| `R03` | kernel fault | `sys/arch/rp2040/rp2040/fault.c` | `arm_fault()` |
| `R04` | user window: linker and machparam.h disagree | `sys/arch/rp2040/rp2040/machdep.c` | `startup()` |
| `R05` | mpu: cannot disable | `sys/arch/rp2040/rp2040/mpu.c` | `mpu_disable()` |
| `R06` | swapram: codec workspace busy | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_codec_acquire()` |
| `R07` | swapram: codec workspace owner | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_codec_release()` |
| `R08` | swapram: spool write | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_spool_write()` |
| `R09` | swapram: spool read | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_spool_read()` |
| `R10` | swapram: spool free | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_spool_free()` |
| `R11` | swapram: proc slot | `sys/arch/rp2040/rp2040/swapram.c` | `sr_slot()` |
| `R12` | swapram: image already resident | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_reserve()` |
| `R13` | swapram: segment length changed | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_put()` |
| `R14` | swapram: reservation overflow | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_put()` |
| `R15` | swapram: count mismatch | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_put()` |
| `R16` | swapram: trim | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_commit()` |
| `R17` | swapram: cancel | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_cancel()` |
| `R18` | swapram: no image | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_in()` |
| `R19` | swapram: short expand | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_in()` |
| `R20` | swapram: free | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_in()` |
| `R21` | swapram: evacuate free | `sys/arch/rp2040/rp2040/swapram.c` | `sr_evacuated()` |
| `R22` | swapram: evacuate loaded | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_evacuate()` |
| `R23` | swapram: flash blocks already assigned | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_evacuate()` |
| `R24` | swapram: evacuate expand | `sys/arch/rp2040/rp2040/swapram.c` | `swapram_evacuate()` |
| `R25` | stack overflow | `sys/arch/rp2040/rp2040/syscall.c` | `user_entry()` |
<!-- panic-codes:end -->

## Verbatim panics

<!-- panic-verbatim:begin -->
| Message | File | Function |
| --- | --- | --- |
| No root filesystem found! | `sys/kern/init_main.c` | `main()` |
| swap size | `sys/kern/init_main.c` | `main()` |
| cannot reserve init swap | `sys/kern/init_main.c` | `main()` |
| init died | `sys/kern/kern_exit.c` | `exit()` |
| swap exchange: media fault | `sys/kern/vm_swap.c` | `swap_exchange_program_sector()` |
| swap exchange: media fault | `sys/kern/vm_swap.c` | `swap_exchange_inplace()` |
| init lost with the swap device | `sys/kern/vm_swap.c` | `swap_lost_retire()` |
| hard err: swap | `sys/kern/vm_swp.c` | `swap_with_buf()` |
| root: no partition of type b7 on sd0 | `sys/arch/rp2040/dev/sd.c` | `sdrootattach()` |
| root: sd0 unusable | `sys/arch/rp2040/dev/sd.c` | `sdrootattach()` |
<!-- panic-verbatim:end -->
