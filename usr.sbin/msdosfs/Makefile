TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# msdosfs(8): the NetBSD msdosfs core, the makefs forks that create files
# and directories, and newfs_msdos's formatter, imported under
# tools/netbsd-imports.manifest, behind a DiscoBSD fixed-pool buffer layer
# and command front end. The program installs to /usr/sbin and stays out of
# distrib/base/mi and distrib/rp2040/mi.rp2040, so neither image carries it.

BINDIR=		/usr/sbin
PROG=		msdosfs

MSDOSFS=	${TOPSRC}/sys/fs/msdosfs
FORKS=		${TOPSRC}/usr.sbin/makefs/msdos
NEWFS=		${TOPSRC}/sbin/newfs_msdos
.PATH:		${MSDOSFS} ${FORKS} ${NEWFS}

# The four vnode-free core files, the three makefs forks and the formatter.
NETBSD_SRCS=	msdosfs_fat.c msdosfs_conv.c msdosfs_lookup.c \
		msdosfs_unicode.c msdosfs_denode.c msdosfs_vfsops.c \
		msdosfs_vnops.c mkfs_msdos.c
LOCAL_SRCS=	msdosfs.c msdosfs_buf.c msdosfs_env.c
OBJS=		${LOCAL_SRCS:.c=.o} ${NETBSD_SRCS:.c=.o}

# MAKEFS selects the forks' userland denode and mount; MSDOS_EI the
# little-endian accessors on every host; HAVE_NBTOOL_CONFIG_H NetBSD's
# portable host-tool spellings. compat/fs/msdosfs points at the imported
# headers, so <fs/msdosfs/...> resolves without exposing ${TOPSRC}/sys.
MSDOSFS_DEFS=	-DMAKEFS -DMSDOS_EI -DHAVE_NBTOOL_CONFIG_H=1 \
		-include ${.CURDIR}/netbsd_compat.h -I${.CURDIR}/compat \
		-I${TOPSRC}/usr.sbin/makefs -I${NEWFS}
# DiscoBSD's libc has no <dirent.h>; see the header.
DISCOBSD_DEFS=	-I${.CURDIR}/compat/discobsd

# The imported files keep NetBSD's dialect: unused parameters in the
# portable forks and the formatter, a SIGINFO handler DiscoBSD never
# installs, the "." and ".." templates in msdosfs_vnops.c, whose 8-byte
# names are not NUL-terminated strings, and msdosfs_wfile's file-size limit
# test, which a 32-bit off_t (DiscoBSD's) can never fail, and
# mkfs_msdos.c's comparisons of that signed off_t with unsigned sector
# counts, which a 64-bit off_t widens away.
NETBSD_FLAGS=	-Wno-unused-parameter -Wno-unused-function \
		-Wno-unterminated-string-initialization -Wno-type-limits \
		-Wno-sign-compare
# The DiscoBSD files are strict C17. They see the imported headers as
# system headers, so -Wpedantic judges this directory's code and leaves the
# verbatim NetBSD declarations as NetBSD wrote them.
LOCAL_FLAGS=	-std=c17 -Wpedantic -isystem ${.CURDIR}/compat \
		-isystem ${TOPSRC}/usr.sbin/makefs -isystem ${NEWFS}

all:		${PROG}

# The imported files compile only once tools/netbsd_imports.py verify has
# matched each one to tools/netbsd-imports.manifest and reversed each local
# patch under tools/netbsd-patches to NetBSD's recorded bytes.
IMPORT_MANIFEST=	${TOPSRC}/tools/netbsd-imports.manifest
IMPORT_PATCHES!=	ls ${TOPSRC}/tools/netbsd-patches/*.patch
IMPORT_FILES=	${MSDOSFS}/*.c ${MSDOSFS}/*.h ${FORKS}/*.c \
		${TOPSRC}/usr.sbin/makefs/msdos.h ${NEWFS}/*.c ${NEWFS}/*.h

netbsd-imports.verified: ${IMPORT_MANIFEST} ${IMPORT_PATCHES} ${IMPORT_FILES}
.if empty(PYTHON)
		@echo "usr.sbin/msdosfs: set PYTHON to verify the NetBSD imports" >&2; exit 2
.endif
		${PYTHON} ${TOPSRC}/tools/netbsd_imports.py verify
		touch ${.TARGET}

.for src in ${NETBSD_SRCS}
${src:.c=.o}:	${src} netbsd-imports.verified
		${CC} ${CFLAGS} ${MSDOSFS_DEFS} ${DISCOBSD_DEFS} ${NETBSD_FLAGS} \
		    -c -o ${.TARGET} ${.ALLSRC:M*.c}
.endfor
.for src in ${LOCAL_SRCS}
${src:.c=.o}:	${src}
		${CC} ${CFLAGS} ${MSDOSFS_DEFS} ${DISCOBSD_DEFS} ${LOCAL_FLAGS} \
		    -c -o ${.TARGET} ${.ALLSRC:M*.c}
.endfor

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# The same program for the host compiler, which tests/msdosfs_core runs.
# HOSTCFLAGS adds the sanitizers there; HOST_DEFS can shrink the buffer
# pool (-DMSDOSFS_NBUF=N) for the pool calibration.
HOSTCC?=	cc
HOSTCFLAGS?=	-O1 -g
HOST_DEFS?=
HOSTBIN=	${PROG}-host
HOST_NETBSD=	${NETBSD_SRCS:Mmsdosfs_[fclu]*:S|^|${MSDOSFS}/|} \
		${NETBSD_SRCS:Mmsdosfs_[dv]*:S|^|${FORKS}/|} ${NEWFS}/mkfs_msdos.c

# host rebuilds every time: a caller may change HOSTCC, HOSTCFLAGS or
# HOST_DEFS between runs, which no file date records.
host:		netbsd-imports.verified
		rm -rf host.obj && mkdir host.obj
.for path in ${HOST_NETBSD}
		${HOSTCC} ${HOSTCFLAGS} -std=gnu17 -Wall -Wextra -Werror \
		    ${MSDOSFS_DEFS} ${HOST_DEFS} ${NETBSD_FLAGS} \
		    -c -o host.obj/${path:T:R}.o ${path}
.endfor
.for src in ${LOCAL_SRCS}
		${HOSTCC} ${HOSTCFLAGS} -Wall -Wextra -Werror ${LOCAL_FLAGS} \
		    -D_DEFAULT_SOURCE ${MSDOSFS_DEFS} ${HOST_DEFS} \
		    -c -o host.obj/${src:.c=.o} ${.CURDIR}/${src}
.endfor
		${HOSTCC} ${HOSTCFLAGS} -o ${HOSTBIN} host.obj/*.o

test:
		${MAKE} -C ${TOPSRC}/tests/msdosfs_core check

clean:
		rm -rf *.o *.elf ${PROG} ${HOSTBIN} host.obj *.dis tags *~ \
		    netbsd-imports.verified

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		host test
