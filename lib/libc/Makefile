#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
# Machine dependent routines are located in a subtree which parallels
# the top directories.  This subtree is identified by the machine name.
#
# Compatibility routines are kept in directories with a prefixing
# ``compat'' (so they all sort together).
#
# The C run-time startup code is always machine dependent and expected
# to be located in ../startup-${MACHINE_ARCH}.
#
# All files contain sccsid strings, but these are not compiled into
# library objects by default, as a space-saving measure.  To produce
# a library that contains these strings in every object except
# system call stubs, add -DLIBC_SCCS to DEFS below; to put these
# strings into system call stubs, use -DSYSLIBC_SCCS.
#
# To compile a non-floating point versions of some standard library
# routines add -DNONFP.  This will speed up some operations if you don't
# have hardware floating point.  To compile a non-separate I&D version add
# -DNONSEPARATE.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

DEFS		= -Wall -ffreestanding

# Every member is compiled with the 32-bit view of time (sys/_time_t.h).
# The members that define the existing names (stat, time, ctime and the
# rest) keep the kernel's layouts, which a program compiled by Smaller C
# on the board declares, and a member whose interface carries no time
# calls the kernel's entry points directly rather than through a wrapper.
# The 64-bit members name their own view: each time64_*.c member replaces
# this definition, and gen/Makefile and compat/Makefile build NAME64.o
# from NAME.c with _TIME_BITS redefined as 64.
DEFS		+= -D_TIME_BITS=32

# The zone in force comes from one kernel field unless the installation
# supplies zone files. No manifest in this tree ships /etc/localtime or
# /usr/share/zoneinfo, and share/zoneinfo builds the files an installation
# would have to add, so the file-backed reader is an option rather than the
# default: defining TZ_ZONEINFO restores it, at 809 bytes of text and 1960
# of zero-initialized data in every program that calls localtime(3), plus
# the two allocations tzload() makes on the stack while it parses. Build
# with TZ_ZONEINFO=1 for a system that installs zone files.
TZ_ZONEINFO?=
.if !empty(TZ_ZONEINFO)
DEFS		+= -DTZ_ZONEINFO
.endif
ALL		= gen stdio stdlib string inet compat ${MACHINE_ARCH}

all:            ../libc.a

../libc.a:      ${ALL}
		rm -rf tmp
		mkdir tmp
		(cd tmp; set -e; for i in ${ALL}; do $(AR) x ../$$i/$$i.a; done; \
			rm -f ../$@; \
			$(AR) cr ../$@ *.o)
#			$(AR) cr ../$@ `sh ../../cmd/lorder.sh *.o | tsort`
		rm -rf tmp

${ALL}:
		cd $@; ${MAKE} ${MFLAGS} DEFS="${DEFS}"

.PHONY:		${ALL}

install:	../libc.a
#		${INSTALL} -d ${DESTDIR}/usr/lib
#		${INSTALL} ../libc.a ${DESTDIR}/usr/lib/libc.a
#		$(RANLIB) ${DESTDIR}/usr/lib/libc.a

clean:
		for i in ${ALL}; \
			do (cd $$i; ${MAKE} ${MFLAGS} clean); done
		rm -rf tmp *.a *~

depend:
		for i in ${ALL}; \
			do (cd $$i; ${MAKE} ${MFLAGS} DEFS="${DEFS}" depend); done
