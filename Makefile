# Copyright (c) 1986 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
# This makefile is designed to be run as:
#	make
#
# The `make' will compile everything, including a kernel, utilities
# and a root filesystem image.

TOPSRC!=	pwd

# Override the default port with:
# $ make MACHINE=pic32 MACHINE_ARCH=mips
#
MACHINE=	stm32
MACHINE_ARCH=	arm

DESTDIR?=	${TOPSRC}/distrib/obj/destdir.${MACHINE}
RELEASEDIR?=	${TOPSRC}/distrib/obj/releasedir

# Filesystem and swap sizes, in kbytes. A machine whose disk is smaller
# than an SD card overrides these from distrib/${MACHINE}/Makefile.inc,
# and U_KBYTES=0 omits the /home partition.
FS_KBYTES?=	204800
U_KBYTES?=	204800
SWAP_KBYTES?=	2048
# The machine-independent manifest; a machine with a small disk names a
# shorter one.
MI_MANIFEST?=	distrib/base/mi
# Inodes in the root filesystem; 0 leaves fsutil's one per 16 kbytes.
FS_INODES?=	0
-include	distrib/${MACHINE}/Makefile.inc

FS_INODES_ARG!=	if [ ${FS_INODES} -gt 0 ] ; then \
			echo "--inodes=${FS_INODES}" ; \
		fi

PARTITIONS!=	if [ ${U_KBYTES} -gt 0 ] ; then \
			echo "fs=${FS_KBYTES}k:swap=${SWAP_KBYTES}k:fs=${U_KBYTES}k" ; \
		elif [ ${SWAP_KBYTES} -gt 0 ] ; then \
			echo "fs=${FS_KBYTES}k:swap=${SWAP_KBYTES}k" ; \
		else \
			echo "fs=${FS_KBYTES}k" ; \
		fi

# SD card filesystem image for ${MACHINE}.
FSIMG=		${TOPSRC}/distrib/${MACHINE}/sdcard.img

# Set this to the device name for your SD card.  With this
# enabled you can use "make installfs" to copy the sdcard.img
# to the SD card.

#SDCARD          = /dev/sdb

#
# C library options: passed to libc makefile.
# See lib/libc/Makefile for explanation.
#
DEFS		=

FSUTIL=		${TOPSRC}/tools/bin/fsutil

# The interpreter for every verifier and harness, passed to each sub-make.
PYTHON?=	python3
.export PYTHON

-include Makefile.user

#
# usr.bin builds before sbin: sbin/textbox's TOOLS loop, like
# sbin/box's and sbin/sysbox's, links each tool's objects out of the
# directory that already built them (see sbin/textbox/Makefile), and
# textbox's tools live under usr.bin.
#
SUBDIR=		share lib bin usr.bin sbin libexec usr.sbin games benchmarks

all:		build

build:		symlinks tools
		$(MAKE) kernel
		$(MAKE) -C etc DESTDIR=${DESTDIR} distrib-dirs
		$(MAKE) -C include includes
		for dir in ${SUBDIR} ; do \
			${MAKE} -C $$dir || exit 1; done
		for dir in ${SUBDIR} ; do \
			${MAKE} -C $$dir DESTDIR=${DESTDIR} install || exit 1; done

distribution:	build
		$(MAKE) fs

tools:
		${MAKE} -C tools MACHINE=${MACHINE} install

kernel:		tools
		${MAKE} -C sys/arch/${MACHINE}/compile all

check-divider:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-divider requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-divider

check-swapram:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-swapram requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-swapram

check-cache-footprint:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-cache-footprint requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-cache-footprint

check-exec-spool:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-exec-spool requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-exec-spool

check-ufs-prototypes:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-ufs-prototypes requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-ufs-prototypes

check-aout:
		${MAKE} -C tests/aout_header check

# The host filesystem library every root image is built with, exercised
# through the allocation patterns that break one and checked with the
# tree's own consistency checker after each round.
check-fs-stress:
		${MAKE} -C tests/fs_stress check

# Machine-independent kernel sources compiled from sys/kern and linked
# against a host harness, so the gate measures the code the board runs.
check-kernel:
		${MAKE} -C tests/kernel check

check-libc-environment:
		${MAKE} -C tests/libc_environment check

check-libc-tempfiles:
		${MAKE} -C tests/libc_tempfiles check

check-libc-ctime:
		${MAKE} -C tests/libc_contracts check-ctime

check-libc-ctime-cross:
		${MAKE} -C tests/libc_contracts check-ctime-cross

check-libc-runtime-limits:
		${MAKE} -C tests/libc_contracts check-runtime-limits

check-libc-runtime-limits-cross:
		${MAKE} -C tests/libc_contracts check-runtime-limits-cross

check-libc-contracts:
		${MAKE} -C tests/libc_contracts check

check-libc-host-contracts:
		${MAKE} -C tests/libc_contracts check-host

check-libc-aout-contracts:	${BOARDLIBC_WORK}/libc.a
		${MAKE} -C tests/libc_contracts check-aout

check-libc-malloc:
		${MAKE} -C tests/libc_contracts check-malloc

check-libc-qsort:
		${MAKE} -C tests/libc_contracts check-qsort

check-libc-random:
		${MAKE} -C tests/libc_contracts check-random

check-libc-random-cross:
		${MAKE} -C tests/libc_contracts check-random-cross

check-libc-strtox:
		${MAKE} -C tests/libc_contracts check-strtox

check-libc-printf:
		${MAKE} -C tests/libc_contracts check-printf

# The formatter with doprnt_float linked: %a and the decimal conversions.
check-libc-printf-float:
		${MAKE} -C tests/libc_contracts check-printf-float

# The formatted input scanner at host width, at ILP32 and under the address
# sanitizer. The sanitizer tier is the one that sees a scratch overrun
# inside the scanner's own frame.
check-libc-scanf:
		${MAKE} -C tests/libc_contracts check-scanf

# The r+ read/write mode switch in _filbuf and _flsbuf, over a host file,
# at host width and ILP32.
check-libc-rwmode:
		${MAKE} -C tests/libc_contracts check-rwmode

check-analysis:
		${MAKE} -C tools/analysis check

check-libc-syslog:
		${MAKE} -C tests/libc_contracts check-syslog

check-libc-vis:
		${MAKE} -C tests/libc_contracts check-vis

check-dirent-contracts:
		${MAKE} -C tests/dirent_contracts check

check-dirent-contracts-cross:
		${MAKE} -C tests/dirent_contracts check-cross

check-cat-contracts:
		${MAKE} -C tests/cat_contracts check

# getty's tty mode derivation, setflags() in libexec/getty/subr.c, over the
# flag tables it reads, on the host.
check-getty-contracts:
		${MAKE} -C tests/getty_contracts check

check-cat-contracts-cross:
		${MAKE} -C tests/cat_contracts check-cross

check-rmdir-contracts:
		${MAKE} -C tests/rmdir_contracts check

check-rmdir-contracts-cross:
		${MAKE} -C tests/rmdir_contracts check-cross

check-tee-contracts:
		${MAKE} -C tests/tee_contracts check

check-tee-contracts-cross:
		${MAKE} -C tests/tee_contracts check-cross

check-du-contracts:
		${MAKE} -C tests/du_contracts check

check-du-contracts-cross:
		${MAKE} -C tests/du_contracts check-cross

check-resize-contracts:
		${MAKE} -C tests/resize_contracts check

check-resize-contracts-cross:
		${MAKE} -C tests/resize_contracts check-cross

check-libc-string-security:
		${MAKE} -C tests/libc_contracts check-string-security

check-libc-string-security-cross:
		${MAKE} -C tests/libc_contracts check-string-security-cross

check-id-aliases:
		${MAKE} -C tests/id_aliases check

check-tiny-utility-multicall:
		${MAKE} -C tests/tiny_utility_multicall check

check-portable-utilities:
		${MAKE} -C tests/portable_utilities check

check-pdp11-reference:
		${MAKE} -C tests/pdp11_reference check

check-pdp11-v7:
		${MAKE} -C tests/pdp11_reference reference

check-fgrep-capacity:
		${MAKE} -C usr.bin/fgrep test

# The packed a.out container: the loader's stream loop and the packer
# against header and stream corruption, truncation and forged lengths.
check-hsaout:
		sh tools/verify_packed_root_configs.sh
		${MAKE} -C tests/hsaout check

check-config-makefile:
		${MAKE} -C tools/config check

# Clean controls must compile; deliberate warnings must fail using the actual
# evaluated commands, including leaves that replace CFLAGS.
check-warning-policy-host:
		${MAKE} -C tests/warning_policy check-host

check-warning-policy-cross:
		${MAKE} -C tests/warning_policy check-cross

# The inherited sources once passed bare identifiers through macros that
# quoted their parameter names.  Compile the exact consumers so their C17
# assertions bind each terminal action to its intended control byte.
check-control-char-contracts:
		${MAKE} -C tests/control_char_contracts check
		rm -f games/cribbage/io.o usr.bin/tip/cmds.o \
		    usr.bin/tip/cmdtab.o usr.bin/tip/vars.o
		${MAKE} -C games/cribbage io.o
		${MAKE} -C usr.bin/tip cmds.o cmdtab.o vars.o

# A failure injected into each step of lib/Makefile's install and clean
# loops and of the kernel link recipe, with the steps after it asserted
# never to run and the parent make asserted to fail.
check-build-failure:
		${MAKE} -C tests/build_failure check

# The evacuation test compiles the kernel's swapram.c and subr_rmap.c on
# the host; its Makefile is written for GNU make. bmake under -j advertises
# its jobserver to children as "-j N -J fd,fd" in MAKEFLAGS, and GNU make
# rejects -J and prints its usage, so the environment is cleared for the one
# recipe in this tree that runs a foreign make.
check-swapram-evac:
		env -u MAKEFLAGS -u MFLAGS \
		    make -C sys/arch/rp2040/test/swapram evac

check-flash-swap:
		${MAKE} -C tests/rp2040/flash_swap check
		${MAKE} -C sys/arch/rp2040/compile check-flash-swap

check-elf2aout:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-elf2aout requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C tests/rp2040/elf2aout_layout check

# Test tiers. Each gate above is its own target; the tiers group them by
# what the host needs, so a machine without the arm cross toolchain,
# qemu-arm or the MIPS compiler runs the tiers it has and names the rest.
# The cross, qemu, mips and board-build tiers run after
# "bmake MACHINE=rp2040 build". sys/arch/rp2040/doc/TESTING.md carries
# the matrix, and .github/workflows/firmware.yml runs "check" on Linux.
HOST_GATES=	check-warning-policy-host check-build-failure check-analysis \
		check-libc-host-contracts \
		check-dirent-contracts check-getty-contracts \
		check-cat-contracts check-rmdir-contracts check-tee-contracts \
		check-du-contracts check-resize-contracts \
		check-aout check-kernel check-fs-stress \
		check-libc-environment \
		check-libc-tempfiles \
		check-id-aliases check-tiny-utility-multicall \
		check-portable-utilities check-pdp11-reference \
		check-fgrep-capacity check-config-makefile check-swapram-evac
HOST_PROGRAM_GATES=	check-pdp11-v6 check-stevie-host check-kilo-host \
		check-menu-host check-tail-host check-sort-host check-keen-host \
		check-bubble-host check-fifteen-host check-sh-editor \
		check-tar-host check-textbox-host check-cpio-host
CROSS_CONTRACT_GATES=	check-warning-policy-cross check-control-char-contracts \
		check-libc-aout-contracts \
		check-libc-ctime-cross \
		check-libc-random-cross \
		check-libc-runtime-limits-cross \
		check-dirent-contracts-cross \
		check-cat-contracts-cross \
		check-rmdir-contracts-cross check-tee-contracts-cross \
		check-du-contracts-cross check-resize-contracts-cross \
		check-libc-string-security-cross

# Shell scripts under shellcheck at error severity and Python under ruff.
check-lint:
		sh tools/check-lint.sh

# Host C compiler and ${PYTHON}: the libc, utility and kernel-model gates,
# the pdp11 V6 boot, and every program with a host build and a suite. Each
# program owns a target so the jobserver can schedule independent directories.
check-pdp11-v6:
		${MAKE} -C usr.bin/pdp11 test

check-stevie-host:
		${MAKE} -C usr.bin/stevie test

check-kilo-host:
		${MAKE} -C usr.bin/kilo test

check-menu-host:
		${MAKE} -C usr.bin/menu test

check-tail-host:
		${MAKE} -C usr.bin/tail test

check-sort-host:
		${MAKE} -C usr.bin/sort test

check-keen-host:
		${MAKE} -C games/keen test

check-bubble-host:
		${MAKE} -C games/bubble test

check-fifteen-host:
		${MAKE} -C games/fifteen test

check-sh-editor:
		${MAKE} -C bin/sh/tests test

check-tar-host:
		sh bin/tar/tests/tartest.sh

check-textbox-host:
		sh usr.bin/textbox/tests/run.sh

check-cpio-host:
		sh usr.bin/cpio/tests/cpiotest.sh

check-host:	${HOST_GATES} ${HOST_PROGRAM_GATES}

# sys/kern/subr_prf.c walks its arguments as four-byte slots, so the kernel's
# own printf is faithful only at ILP32. The gate is an ordinary 32-bit
# userspace binary, not a virtual machine: what it needs is the width of a
# pointer. A host that cannot build 32-bit says so and skips.
check-kernel-ilp32:
		${MAKE} -C tests/kernel check-ilp32

# The POSIX conformance run of bin/sh builds the shell as a 32-bit host
# binary (it keeps pointers in int), so it needs a Linux x86-64 host with
# the 32-bit libraries and stands apart from the portable host tier.
check-posix-sh:
		sh bin/sh/tests/posix-sh.sh

# The arm cross toolchain and a built tree: isolated contract directories run
# concurrently. Kernel gates stay ordered because they rebuild and inspect the
# same PICO, PICO_UART, SwapRAM and exec-spool outputs. The assembler runs last
# because its link proof rebuilds the shared a.out libc.
check-cross-contracts:	${CROSS_CONTRACT_GATES}

check-cross-kernel:	check-divider .WAIT check-swapram .WAIT \
		check-cache-footprint .WAIT check-exec-spool .WAIT \
		check-ufs-prototypes .WAIT check-hsaout .WAIT check-flash-swap

check-cross-assembler:
		${MAKE} -C usr.bin/as/tests test
		${MAKE} -C tests/rp2040/divider_ownership check

check-cross:	check-cross-contracts .WAIT check-cross-kernel .WAIT \
		check-cross-assembler

# qemu-arm: the u-area exchange loop and the Smaller C suite executed
# rather than only linked.
check-qemu:
		${MAKE} -C tests/rp2040/uarea_exchange check
		REQUIRE_QEMU=1 ${MAKE} -C usr.bin/smlrc test

# Renode with the third-party RP2040 models: the kernel boots from the real
# boot ROM and the gate asserts the console lines through the device probe.
# Not in "check": it wants an emulator, a fetched model tree and a built
# image, and tools/renode/check-boot.sh names whichever is missing.
check-renode:
		sh tools/renode/check-boot.sh

# The SRAM-resident board probes, flash-id and flash-semantics, built
# against a Pico SDK that tools/pico-sdk/sdk-path.sh resolves. Outside
# check for the reason check-renode is: it wants cmake and a fetched SDK.
check-flash-id:
		sh tools/pico-sdk/check-flash-id.sh

# The MIPS cross compiler beside the arm one: elf2aout's layout on both.
check-mips:	check-elf2aout

# The discobsd-host Python package: its own lint and tests.
check-host-package:
		cd distrib/rp2040/host && ruff check . && ${PYTHON} -m pytest -q

# The on-device regression programs under tests/rp2040 build and convert
# to a.out; the board runs them.
check-board-build:
		${MAKE} -C tests/rp2040 all

check:		check-lint check-host check-posix-sh check-cross check-qemu \
		check-mips check-host-package check-board-build

fs:		$(FSIMG)

# The image is staged from ${DESTDIR}; etc/passwd, etc/shadow and etc/group
# reach ${DESTDIR}/etc only through etc's own distribution target. Depending on
# it here refreshes them for every image path -- fs, flash, and distribution --
# rather than only the full distribution target, which is why a bare
# "bmake flash" used to reflash a stale account database.
etc-distribution:
		${MAKE} -C etc DESTDIR=${DESTDIR} distribution

# /etc/release names the build the root came from: the commit (with
# -dirty when the tree had changes), the date, and the builder's host OS,
# so a board answers "which firmware is this?" with "cat /etc/release".
# A tree without git history says so rather than failing.
${FSIMG}:	distrib/${MACHINE}/md.${MACHINE} ${MI_MANIFEST} distrib/base/mi.home \
		etc-distribution
		rm -f $@ distrib/$(MACHINE)/_manifest
		commit=`git -C ${TOPSRC} describe --always --dirty --tags 2>/dev/null || echo unknown`; \
		printf 'DiscoBSD/${MACHINE} root built %s from %s on %s\n' \
		    "`date -u +%Y-%m-%dT%H:%M:%SZ`" "$$commit" "`uname -s`" \
		    > ${DESTDIR}/etc/release
		cat ${MI_MANIFEST} distrib/$(MACHINE)/md.$(MACHINE) > distrib/$(MACHINE)/_manifest
		$(FSUTIL) --repartition=${PARTITIONS} $@
		${FSUTIL} --new --partition=1 ${FS_INODES_ARG} --manifest=distrib/${MACHINE}/_manifest $@ ${DESTDIR}
		[ ${U_KBYTES} -eq 0 ] || \
		$(FSUTIL) --new --partition=3 --manifest=distrib/base/mi.home $@ distrib/home

release:
		${MAKE} -C etc MACHINE=${MACHINE} RELEASEDIR=${RELEASEDIR} release

clean:
		rm -f *~
		rm -f include/machine
		for dir in ${SUBDIR} ; do \
			$(MAKE) -C $$dir -k clean; done

cleantools:
		${MAKE} -C tools clean

cleankernel:
		${MAKE} -C sys/arch/${MACHINE}/compile -k clean

cleanfs:
		rm -f distrib/$(MACHINE)/_manifest
		rm -f $(FSIMG)

cleanall:	cleantools clean cleankernel

symlinks:
		rm -f include/machine
		ln -s $(MACHINE) include/machine

installfs:
		@[ -n "${SDCARD}" ] || (echo "SDCARD not defined." && exit 1)
		@[ -f $(FSIMG) ] || $(MAKE) $(FSIMG)
		sudo dd bs=1M if=${FSIMG} of=${SDCARD}

.PHONY:		check-warning-policy-host check-warning-policy-cross \
		check-control-char-contracts \
		check-build-failure check-analysis \
		all build distribution release tools kernel check-divider \
		check-swapram check-cache-footprint check-exec-spool \
		check-ufs-prototypes \
		check-elf2aout \
		check-kernel check-kernel-ilp32 check-fs-stress \
		check-libc-environment \
		check-libc-tempfiles check-libc-ctime check-libc-ctime-cross \
		check-libc-runtime-limits check-libc-runtime-limits-cross \
		check-libc-contracts \
		check-libc-host-contracts check-libc-aout-contracts \
		check-libc-malloc \
		check-libc-qsort check-libc-strtox check-libc-printf \
		check-libc-random check-libc-random-cross \
		check-libc-scanf check-libc-rwmode check-libc-syslog check-libc-vis \
		check-libc-printf-float \
		check-dirent-contracts check-dirent-contracts-cross \
		check-libc-string-security check-libc-string-security-cross \
		check-id-aliases \
		check-tiny-utility-multicall \
		check-fgrep-capacity check-hsaout check-config-makefile \
		check-portable-utilities check-pdp11-reference check-pdp11-v7 \
		check-pdp11-v6 check-stevie-host check-kilo-host check-menu-host \
		check-tail-host check-sort-host check-keen-host check-bubble-host \
		check-fifteen-host check-sh-editor check-tar-host \
		check-textbox-host check-cpio-host \
		check-lint check-host check-posix-sh check-cross-contracts \
		check-cross-kernel check-cross-assembler check-cross check-qemu \
		check-mips check-renode check-host-package check-board-build check \
		symlinks \
		etc-distribution \
		${FSIMG} fs installfs \
		clean cleantools cleanfs cleanall

# Architecture-specific debugging and loading.
-include sys/arch/${MACHINE}/conf/Makefile.inc
