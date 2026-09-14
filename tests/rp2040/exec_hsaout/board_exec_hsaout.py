#!/usr/bin/env python3
"""On-device checks of the packed a.out loader
(sys/arch/rp2040/rp2040/exec_hsaout.c, sys/kern/exec_aout.c, sys/kern/vm_swap.c).

The image installs textcrc raw and packed, and bigtest packed, through a
scratch copy of the root manifest whose "pack" entries the host packer
tools/hsaout produces. The board sh aborts a script on a failed
redirection, so the write-refused case is issued as its own command line,
and a background command takes no trailing marker because "cmd &;echo" is
a syntax error. Run with the kernel and filesystem UF2s:

    board_exec_hsaout.py compile/PICO/unix.uf2,distrib/rp2040/flash.uf2

Expected: TEXTCRC OK from every restoration, the text CRC unchanged across
a swap, a write to a running executable refused and allowed after it exits,
an appended or truncated container refused, and BIGTEST OK under LARGE.
"""
