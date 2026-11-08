TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the tier takes the
# tree's default from share/mk/sys.mk; a MACHINE on the command line still
# wins over both.
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
FSCK=		${TOPSRC}/sbin/fsck
FSUTIL=		${TOPSRC}/tools/fsutil
DF=		${TOPSRC}/bin/df
MKFS=		${TOPSRC}/sbin/mkfs

# sbin/fsck reads the tree's struct dinode, struct direct and struct fs, whose
# ino_t, daddr_t, off_t and time_t are four bytes each. The tier therefore
# builds fsck against the tree's own headers at ILP32, as tests/umount_contracts
# does, and answers the few host services whose layouts differ through
# fsck_glue.c (see fsck_glue.h). A host that cannot run ILP32 binaries records
# a skip receipt, which REQUIRE_ILP32=yes turns into a failure.
ILP32=		-m32
ILP32_CC=	${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

# include/sys/param.h reaches the port's headers as <machine/*.h>, a name the
# root Makefile spells as a symlink inside its build targets. A tier that has
# to run in an unconfigured tree cannot rely on that, so it generates one
# forwarding header per port header, as tests/umount_contracts does.
MACHINE_STAMP=	machine/.stamp
PORT_INCLUDE=	${TOPSRC}/include/${MACHINE}

# The program is held to the warning set its own Makefile builds it with
# (-Wall). sbin/fsck still defines functions the old way, which clang rejects
# by default and the arm-none-eabi gcc the program ships from does not; the
# gate excuses it rather than rewriting the program for a host compiler alone.
FSCK_WARN=	-Wall -Werror -Wno-deprecated-non-prototype
FSCK_CFLAGS=	-std=gnu17 ${ILP32} -fno-builtin ${FSCK_WARN} \
		-I. -I${TOPSRC}/include -I${FSCK} -I${.CURDIR} \
		-include fsck_rename.h
GATE_WARN=	-Wall -Wextra -Werror

# main.c is the entry the boot runs; cross.c is a second entry point the
# program's own Makefile does not link.
UNITS=		main dir inode pass1 pass1b pass2 pass3 pass4 pass5 setup \
		utilities
# setup() asks reply() whether to check a file that is not a device, and in
# preen mode reply() ends fsck; fsck_glue.c answers that one prompt.
CFLAGS.setup=	-Dreply=fsck_accept_image

# One entry per site of the inode range test, each restored by
# fsck_mutate.py. The scenarios listed for a mutant must fail on it.
MUTANTS=	pass1-loop direntry makeentry-ino makeentry-parent \
		lfdir-lookup alloc-test alloc-range all-old \
		window-flush window-write inode-window dirhead allocdir-zero \
		adjust-noop touch-superblock pass3-cycle-unbound \
		pass3-cycle-open pass3-cycle-once pass2-dotdot-split
SCENARIOS.pass1-loop=		file-last dir-last
SCENARIOS.direntry=		file-last
SCENARIOS.makeentry-ino=	orphan-last
SCENARIOS.makeentry-parent=	lf-last
SCENARIOS.lfdir-lookup=		lf-last
SCENARIOS.alloc-test=		free-last
SCENARIOS.alloc-range=		free-last
SCENARIOS.all-old=		file-last dir-last orphan-last free-last lf-last
SCENARIOS.window-flush=		orphan-dir free-last
SCENARIOS.window-write=		tail-evict
SCENARIOS.adjust-noop=		orphan-dir tail-evict
SCENARIOS.touch-superblock=	orphan-dir
SCENARIOS.inode-window=		orphan-dir free-last tail-evict
SCENARIOS.dirhead=		free-last
SCENARIOS.allocdir-zero=	full-table
SCENARIOS.pass3-cycle-unbound=	fsck-lf-chain
SCENARIOS.pass3-cycle-open=	fsck-lf-cycle fsck-lf-cycle-gap
SCENARIOS.pass2-dotdot-split=	fsck-dotdot-range
SCENARIOS.pass3-cycle-once=	fsck-lf-cycle fsck-lf-cycle-gap fsck-lf-chain

# A mutant of tools/fsutil is the image generator linked with a rewritten
# check.c, which run.sh uses beside the unmutated fsck.
FSUTIL_MUTANTS=	fsutil-find-inode fsutil-lost-entry-old fsutil-namlen-empty \
		fsutil-namlen-split fsutil-split-off fsutil-split-trim \
		fsutil-reclen-bound
SCENARIOS.fsutil-find-inode=	fsutil-lf-last fsutil-lf-hole
SCENARIOS.fsutil-lost-entry-old=	fsutil-lf-last fsutil-lf-hole \
				fsutil-lf-pristine
SCENARIOS.fsutil-namlen-empty=	fsutil-lf-hole
SCENARIOS.fsutil-namlen-split=	fsutil-lf-last fsutil-lf-pristine
SCENARIOS.fsutil-split-off=	fsutil-lf-last fsutil-lf-pristine
SCENARIOS.fsutil-split-trim=	fsutil-lf-last fsutil-lf-pristine
SCENARIOS.fsutil-reclen-bound=	fsutil-lf-overrun

# bin/df and sbin/mkfs are built from their own sources the same way; each
# mutant is the program with one rewritten source file.
DF_MUTANTS=	df-isize
SCENARIOS.df-isize=		df-empty df-full df-tail df-mkfs
MKFS_MUTANTS=	mkfs-ratio
SCENARIOS.mkfs-ratio=		mkfs-ratio

MUTANT_SPECS=
.for mutant in ${MUTANTS} ${FSUTIL_MUTANTS} ${DF_MUTANTS} ${MKFS_MUTANTS}
MUTANT_SPECS+=	${mutant}=${SCENARIOS.${mutant}:ts,}
.endfor

# check.sh decides the gate: the program passes every scenario, and each
# mutant is rejected for each scenario named for it by run.sh exiting 1 with
# that scenario's own verdict. A run that breaks (status 2) is an error, not
# a rejection, so a mutant that does not build or execute cannot pass
# calibration. The execution receipt binds the ILP32 fsck_host to the run;
# an unavailable ILP32 runtime records a skip, which REQUIRE_ILP32=yes and
# the detached aggregate turn into a failure.
check:
.if ${ILP32_OK} == "yes"
	@${MAKE} fsck_host image_gen df_host mkfs_host \
	    ${MUTANTS:S/^/fsck_mutant_/} \
	    ${FSUTIL_MUTANTS:S/^/image_gen_mutant_/} \
	    ${DF_MUTANTS:S/^/df_mutant_/} ${MKFS_MUTANTS:S/^/mkfs_mutant_/}
	${TEST_EXECUTION} run fsck.inode-range.ilp32 -- \
	    sh ${.CURDIR}/check.sh ${.CURDIR}/run.sh ./fsck_host ./image_gen \
	    ./df_host ./mkfs_host work . ${MUTANT_SPECS}
.else
	${TEST_EXECUTION} skip fsck.inode-range.ilp32 "fsck_inode_range: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

${MACHINE_STAMP}:
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/$$name>" > machine/$$name; \
	done
	@touch $@

FSCK_HEADERS=	${FSCK}/fsck.h fsck_rename.h ${MACHINE_STAMP}

.for unit in ${UNITS}
objs/${unit}.o: ${FSCK}/${unit}.c ${FSCK_HEADERS}
	@mkdir -p objs
	${HOSTCC} ${FSCK_CFLAGS} ${CFLAGS.${unit}} -c -o $@ ${FSCK}/${unit}.c
.endfor

# The tree's stdio.h redeclares fopen, fread, fprintf and the other names the
# host compiler knows as builtins over the target's FILE; clang rejects that
# under -Wbuiltin-requires-header unless the builtins are off, as they are for
# every other unit that includes the tree's headers.
objs/glue.o: fsck_glue.c fsck_glue.h ${MACHINE_STAMP}
	@mkdir -p objs
	${HOSTCC} -std=gnu17 ${ILP32} -fno-builtin ${GATE_WARN} -I. \
	    -I${TOPSRC}/include \
	    -c -o $@ fsck_glue.c

# The host half carries no tree header and reaches the host's <stdio.h>.
objs/host.o: fsck_host.c fsck_glue.h
	@mkdir -p objs
	${HOSTCC} -std=gnu17 ${ILP32} ${GATE_WARN} -c -o $@ fsck_host.c

BASE_OBJECTS=	${UNITS:S/^/objs\//:S/$/.o/} objs/glue.o objs/host.o

fsck_host: ${BASE_OBJECTS}
	${HOSTCC} ${ILP32} -o $@ ${BASE_OBJECTS}

# A mutant is the program with the mutated files rebuilt from a rewritten
# copy of the source and every other object shared. The mutated units are
# pass1.c, pass2.c, pass3.c, dir.c, inode.c and utilities.c, none of which
# takes a per-unit definition.
.for mutant in ${MUTANTS}
fsck_mutant_${mutant}: fsck_mutate.py ${BASE_OBJECTS}
	@mkdir -p mutant/${mutant}
	${PYTHON} ${.CURDIR}/fsck_mutate.py --source-dir ${FSCK} \
	    --output-dir mutant/${mutant} --mutation ${mutant}
	@objects="${BASE_OBJECTS}"; \
	for file in `${PYTHON} ${.CURDIR}/fsck_mutate.py --source-dir ${FSCK} \
	    --output-dir mutant/${mutant} --mutation ${mutant} --list-files`; do \
		unit=$${file%.c}; \
		${HOSTCC} ${FSCK_CFLAGS} -c -o mutant/${mutant}/$$unit.o \
		    mutant/${mutant}/$$file || exit 1; \
		objects=`echo $$objects | sed "s|objs/$$unit.o|mutant/${mutant}/$$unit.o|"`; \
	done; \
	${HOSTCC} ${ILP32} -o $@ $$objects
.endfor

FS_SRCS=	superblock block inode create check file
FS_OBJECTS=	${FS_SRCS:S/^/objs\/fs_/:S/$/.o/}

# The image library every root filesystem is built with, minus the command
# line front end and the FUSE mount. It is a host program that never meets
# the tree's headers, so it builds at the host's native width.
.for src in ${FS_SRCS}
objs/fs_${src}.o: ${FSUTIL}/${src}.c
	@mkdir -p objs
	${HOSTCC} -O -Wall -I${FSUTIL} -c -o $@ ${FSUTIL}/${src}.c
.endfor

objs/image_gen.o: image_gen.c
	@mkdir -p objs
	${HOSTCC} -O ${GATE_WARN} -I${FSUTIL} -c -o $@ image_gen.c

image_gen: objs/image_gen.o ${FS_OBJECTS}
	${HOSTCC} -o $@ objs/image_gen.o ${FS_OBJECTS}

# The mutated check.c replaces the library's own object in the link.
.for mutant in ${FSUTIL_MUTANTS}
image_gen_mutant_${mutant}: fsck_mutate.py objs/image_gen.o ${FS_OBJECTS}
	@mkdir -p mutant/${mutant}
	${PYTHON} ${.CURDIR}/fsck_mutate.py --source-dir ${FSUTIL} \
	    --output-dir mutant/${mutant} --mutation ${mutant}
	${HOSTCC} -O -Wall -I${FSUTIL} -c -o mutant/${mutant}/check.o \
	    mutant/${mutant}/check.c
	${HOSTCC} -o $@ objs/image_gen.o \
	    ${FS_OBJECTS:Nobjs/fs_check.o} mutant/${mutant}/check.o
.endfor

# bin/df and sbin/mkfs are held to the warning set their own Makefiles build
# them with. Both still define functions the old way, and df.c keeps a status
# it never reads. Each is compiled at ILP32 against the tree's headers; the
# renames move the host-visible effects (main, sync, errno and fprintf, whose
# stderr is a FILE the host's libc does not own) onto df_driver.c, which
# answers them, and sbin/mkfs links as the program it is.
PROG_WARN=	-Wall -Werror -Wno-deprecated-non-prototype \
		-Wno-unused-but-set-variable
PROG_CFLAGS=	-std=gnu17 ${ILP32} -fno-builtin ${PROG_WARN} \
		-I. -I${TOPSRC}/include
DF_RENAMES=	-Dmain=df_main -Dsync=df_sync -Dfprintf=df_fprintf \
		-Derrno=df_errno

objs/df.o: ${DF}/df.c ${MACHINE_STAMP}
	@mkdir -p objs
	${HOSTCC} ${PROG_CFLAGS} ${DF_RENAMES} -c -o $@ ${DF}/df.c

objs/df_driver.o: df_driver.c ${MACHINE_STAMP}
	@mkdir -p objs
	${HOSTCC} -std=gnu17 ${ILP32} -fno-builtin ${GATE_WARN} -I. \
	    -I${TOPSRC}/include -c -o $@ df_driver.c

df_host: objs/df.o objs/df_driver.o
	${HOSTCC} ${ILP32} -o $@ objs/df.o objs/df_driver.o

objs/mkfs.o: ${MKFS}/mkfs.c ${MACHINE_STAMP}
	@mkdir -p objs
	${HOSTCC} ${PROG_CFLAGS} -c -o $@ ${MKFS}/mkfs.c

mkfs_host: objs/mkfs.o
	${HOSTCC} ${ILP32} -o $@ objs/mkfs.o

.for mutant in ${DF_MUTANTS}
df_mutant_${mutant}: fsck_mutate.py ${DF}/df.c objs/df_driver.o \
	    ${MACHINE_STAMP}
	@mkdir -p mutant/${mutant}
	${PYTHON} ${.CURDIR}/fsck_mutate.py --source-dir ${DF} \
	    --output-dir mutant/${mutant} --mutation ${mutant}
	${HOSTCC} ${PROG_CFLAGS} ${DF_RENAMES} -c -o mutant/${mutant}/df.o \
	    mutant/${mutant}/df.c
	${HOSTCC} ${ILP32} -o $@ mutant/${mutant}/df.o objs/df_driver.o
.endfor

.for mutant in ${MKFS_MUTANTS}
mkfs_mutant_${mutant}: fsck_mutate.py ${MKFS}/mkfs.c ${MACHINE_STAMP}
	@mkdir -p mutant/${mutant}
	${PYTHON} ${.CURDIR}/fsck_mutate.py --source-dir ${MKFS} \
	    --output-dir mutant/${mutant} --mutation ${mutant}
	${HOSTCC} ${PROG_CFLAGS} -c -o mutant/${mutant}/mkfs.o \
	    mutant/${mutant}/mkfs.c
	${HOSTCC} ${ILP32} -o $@ mutant/${mutant}/mkfs.o
.endfor

clean:
	rm -f fsck_host image_gen df_host mkfs_host fsck_mutant_* \
	    image_gen_mutant_* df_mutant_* mkfs_mutant_*
	rm -f objs/*.o mutant/*/*.c mutant/*/*.o work/*/*.img work/*/*.img.repaired work/*/fsck.out \
	    work/check.log
	rm -f machine/*.h ${MACHINE_STAMP}
	@for directory in objs machine mutant/* mutant work/* work; do \
		if [ -d $$directory ]; then rmdir $$directory; fi; \
	done

all: check

.PHONY: all check clean
