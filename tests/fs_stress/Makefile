TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc
FSUTIL=	${TOPSRC}/tools/fsutil

# The image library every root filesystem is built with, minus the command
# line front end and the FUSE mount that only fsutil(8) itself needs.
FS_SRCS=	${FSUTIL}/superblock.c ${FSUTIL}/block.c ${FSUTIL}/inode.c \
		${FSUTIL}/create.c ${FSUTIL}/check.c ${FSUTIL}/file.c
FS_OBJS=	superblock.o block.o inode.o create.o check.o file.o

# tools/fsutil builds with -O -Wall, and the library is held to that set
# rather than to the gate's: a gate that rejected code the tool build
# accepts would fail for something that already ships. The gate's own
# source takes the stricter set.
LIB_CFLAGS=	-O -Wall -I${FSUTIL}
GATE_CFLAGS=	-O -Wall -Wextra -Werror -I${FSUTIL}

.for src in ${FS_SRCS}
${src:T:R}.o: ${src}
	${HOST_CC} ${LIB_CFLAGS} -c -o $@ ${src}
.endfor

fs_stress_test: fs_stress_test.c ${FS_OBJS}
	${HOST_CC} ${GATE_CFLAGS} -o $@ fs_stress_test.c ${FS_OBJS}

check: fs_stress_test
	./fs_stress_test

clean:
	rm -f fs_stress_test fs_stress.img ${FS_OBJS}

.PHONY: check clean
