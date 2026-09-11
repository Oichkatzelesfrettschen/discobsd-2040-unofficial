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
		else \
			echo "fs=${FS_KBYTES}k:swap=${SWAP_KBYTES}k" ; \
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

SUBDIR=		share lib bin sbin libexec usr.bin usr.sbin games

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
		${MAKE} -C etc DESTDIR=${DESTDIR} distribution
		$(MAKE) fs

tools:
		${MAKE} -C tools MACHINE=${MACHINE} install

kernel:		tools
		${MAKE} -C sys/arch/${MACHINE}/compile all

fs:		$(FSIMG)

${FSIMG}:	distrib/${MACHINE}/md.${MACHINE} ${MI_MANIFEST} distrib/base/mi.home
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

.PHONY:		all build distribution release tools kernel symlinks \
		${FSIMG} fs installfs \
		clean cleantools cleanfs cleanall

# Architecture-specific debugging and loading.
-include sys/arch/${MACHINE}/conf/Makefile.inc
