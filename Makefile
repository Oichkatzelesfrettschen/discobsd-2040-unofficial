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
			${MAKE} -C $$dir ; done
		for dir in ${SUBDIR} ; do \
			${MAKE} -C $$dir DESTDIR=${DESTDIR} install ; done

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

check-aout:
		${MAKE} -C tests/aout_header check

check-libc-environment:
		${MAKE} -C tests/libc_environment check

check-libc-tempfiles:
		${MAKE} -C tests/libc_tempfiles check

check-libc-contracts:
		${MAKE} -C tests/libc_contracts check

check-id-aliases:
		${MAKE} -C tests/id_aliases check

# The packed a.out container: the loader's stream loop and the packer
# against header and stream corruption, truncation and forged lengths.
check-hsaout:
		sh tools/verify_packed_root_configs.sh
		${MAKE} -C tests/hsaout check

# The evacuation test compiles the kernel's swapram.c and subr_rmap.c on
# the host; its Makefile is written for GNU make.
check-swapram-evac:
		make -C sys/arch/rp2040/test/swapram evac

check-elf2aout:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-elf2aout requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C tests/rp2040/elf2aout_layout check

fs:		$(FSIMG)

# The image is staged from ${DESTDIR}; etc/passwd, etc/shadow and etc/group
# reach ${DESTDIR}/etc only through etc's own distribution target. Depending on
# it here refreshes them for every image path -- fs, flash, and distribution --
# rather than only the full distribution target, which is why a bare
# "bmake flash" used to reflash a stale account database.
etc-distribution:
		${MAKE} -C etc DESTDIR=${DESTDIR} distribution

${FSIMG}:	distrib/${MACHINE}/md.${MACHINE} ${MI_MANIFEST} distrib/base/mi.home \
		etc-distribution
		rm -f $@ distrib/$(MACHINE)/_manifest
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

.PHONY:		all build distribution release tools kernel check-divider \
		check-swapram check-elf2aout check-libc-environment \
		check-libc-tempfiles check-libc-contracts check-id-aliases \
		symlinks \
		etc-distribution \
		${FSIMG} fs installfs \
		clean cleantools cleanfs cleanall

# Architecture-specific debugging and loading.
-include sys/arch/${MACHINE}/conf/Makefile.inc
