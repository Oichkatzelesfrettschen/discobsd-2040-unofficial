TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# v7fs(8): the NetBSD v7fs core and newfs library, imported verbatim under
# tools/netbsd-imports.manifest, behind a DiscoBSD descriptor backend and
# command front end. The program installs to /usr/sbin and stays out of
# distrib/base/mi and distrib/rp2040/mi.rp2040, so neither image carries it.

BINDIR=		/usr/sbin
PROG=		v7fs

V7FS=		${TOPSRC}/sys/fs/v7fs
NEWFS=		${TOPSRC}/sbin/newfs_v7fs
.PATH:		${V7FS} ${NEWFS}

# The ten vnode-free core files and newfs's library; NetBSD's own backends
# (v7fs_io_kern.c, v7fs_io_user.c) are replaced by v7fs_io_fd.c.
NETBSD_SRCS=	v7fs_endian.c v7fs_superblock.c v7fs_superblock_util.c \
		v7fs_inode.c v7fs_inode_util.c v7fs_datablock.c \
		v7fs_dirent.c v7fs_io.c v7fs_file.c v7fs_file_util.c main.c
LOCAL_SRCS=	v7fs.c v7fs_io_fd.c
OBJS=		${LOCAL_SRCS:.c=.o} ${NETBSD_SRCS:.c=.o}

# Both halves: every byte order (V7FS_EI) and the core's static scratch
# blocks (STATIC_BUFFER, three 512-byte blocks in .bss for core scratch I/O).
V7FS_DEFS=	-DV7FS_EI -DSTATIC_BUFFER \
		-include ${.CURDIR}/netbsd_compat.h -I${.CURDIR}/compat \
		-I${V7FS} -I${NEWFS}
# DiscoBSD's libc has <stdint.h> and no <inttypes.h>; see the header.
DISCOBSD_DEFS=	-I${.CURDIR}/compat/discobsd

# The imported files keep NetBSD's dialect. Two warnings stay off: the
# packed-member pointer whose alignment tests/v7fs_core asserts, and an
# argument v7fs_io.c reads only without STATIC_BUFFER. NDEBUG keeps the
# selected target's scratch exhaustion path as a NULL return.
NETBSD_FLAGS=	-Wno-address-of-packed-member -Wno-unused-parameter -DNDEBUG
# The DiscoBSD files are strict C17. They see the imported headers as
# system headers, so -Wpedantic judges this directory's code and leaves the
# verbatim NetBSD declarations as NetBSD wrote them.
LOCAL_FLAGS=	-std=c17 -Wpedantic -isystem ${V7FS} -isystem ${NEWFS}

all:		${PROG}

# The imported files compile only once tools/netbsd_imports.py verify has
# matched each one to tools/netbsd-imports.manifest and reversed each local
# patch under tools/netbsd-patches to NetBSD's recorded bytes, so the build
# carries NetBSD's revision plus exactly the recorded corrections. The stamp
# reruns the check whenever the manifest, a patch or an imported file
# changes.
IMPORT_MANIFEST=	${TOPSRC}/tools/netbsd-imports.manifest
IMPORT_PATCHES!=	ls ${TOPSRC}/tools/netbsd-patches/*.patch
IMPORT_FILES=	${NETBSD_SRCS:Nmain.c:S|^|${V7FS}/|} ${NEWFS}/main.c \
		${V7FS}/*.h ${NEWFS}/newfs_v7fs.h

netbsd-imports.verified: ${IMPORT_MANIFEST} ${IMPORT_PATCHES} ${IMPORT_FILES}
.if empty(PYTHON)
		@echo "usr.sbin/v7fs: set PYTHON to verify the NetBSD imports" >&2; exit 2
.endif
		${PYTHON} ${TOPSRC}/tools/netbsd_imports.py verify
		touch ${.TARGET}

.for src in ${NETBSD_SRCS}
${src:.c=.o}:	${src} netbsd-imports.verified
		${CC} ${CFLAGS} ${V7FS_DEFS} ${DISCOBSD_DEFS} ${NETBSD_FLAGS} \
		    -c -o ${.TARGET} ${.ALLSRC:M*.c}
.endfor
.for src in ${LOCAL_SRCS}
${src:.c=.o}:	${src}
		${CC} ${CFLAGS} ${V7FS_DEFS} ${DISCOBSD_DEFS} ${LOCAL_FLAGS} \
		    -c -o ${.TARGET} ${.ALLSRC:M*.c}
.endfor

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# The same program for the host compiler, which tests/v7fs_core runs.
# HOSTCFLAGS adds the sanitizers there, and HOST_NETBSD_CFLAGS the flags
# only the imported files take (tests/v7fs_core/check.sh explains its
# -fno-sanitize=alignment).
HOSTCC?=	cc
HOSTCFLAGS?=	-O1 -g
HOST_NETBSD_CFLAGS?=
HOSTBIN=	${PROG}-host
HOST_SRCS=	${LOCAL_SRCS:S|^|${.CURDIR}/|} \
		${NETBSD_SRCS:Nmain.c:S|^|${V7FS}/|} ${NEWFS}/main.c

# host rebuilds every time: a caller may change HOSTCC or HOSTCFLAGS
# between runs, which no file date records.
host:		${HOST_SRCS} netbsd-imports.verified
		rm -rf host.obj && mkdir host.obj
.for src in ${NETBSD_SRCS:Nmain.c}
		${HOSTCC} ${HOSTCFLAGS} -std=gnu17 -Wall -Wextra -Werror \
		    ${V7FS_DEFS} -Wno-address-of-packed-member \
		    -Wno-unused-parameter -DNDEBUG ${HOST_NETBSD_CFLAGS} \
		    -c -o host.obj/${src:.c=.o} ${V7FS}/${src}
.endfor
		${HOSTCC} ${HOSTCFLAGS} -std=gnu17 -Wall -Wextra -Werror \
		    ${V7FS_DEFS} -DNDEBUG ${HOST_NETBSD_CFLAGS} \
		    -c -o host.obj/newfs_main.o ${NEWFS}/main.c
.for src in ${LOCAL_SRCS}
		${HOSTCC} ${HOSTCFLAGS} -Wall -Wextra -Werror ${LOCAL_FLAGS} \
		    -D_DEFAULT_SOURCE ${V7FS_DEFS} \
		    -c -o host.obj/${src:.c=.o} ${.CURDIR}/${src}
.endfor
		${HOSTCC} ${HOSTCFLAGS} -o ${HOSTBIN} host.obj/*.o

# The gate that drives this program: tests/v7fs_core builds the host
# program, runs every command in both byte orders and checks its images.
test:
		${MAKE} -C ${TOPSRC}/tests/v7fs_core check

clean:
		rm -rf *.o *.elf ${PROG} ${HOSTBIN} host.obj *.dis tags *~ \
		    netbsd-imports.verified

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		host test
