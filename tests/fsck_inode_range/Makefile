TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the tier takes the
# tree's default from share/mk/sys.mk; a MACHINE on the command line still
# wins over both.
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
FSCK=		${TOPSRC}/sbin/fsck
FSUTIL=		${TOPSRC}/tools/fsutil

# sbin/fsck reads the tree's struct dinode, struct direct and struct fs, whose
# ino_t, daddr_t, off_t and time_t are four bytes each. The tier therefore
# builds fsck against the tree's own headers at ILP32, as tests/umount_contracts
# does, and answers the few host services whose layouts differ through
# fsck_glue.c (see fsck_glue.h). A host that cannot run ILP32 binaries skips,
# and REQUIRE_ILP32=yes turns that skip into a failure.
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
		lfdir-lookup alloc-test alloc-range all-old
SCENARIOS.pass1-loop=		file-last dir-last
SCENARIOS.direntry=		file-last
SCENARIOS.makeentry-ino=	orphan-last
SCENARIOS.makeentry-parent=	lf-last
SCENARIOS.lfdir-lookup=		lf-last
SCENARIOS.alloc-test=		free-last
SCENARIOS.alloc-range=		free-last
SCENARIOS.all-old=		file-last dir-last orphan-last free-last lf-last
ALL_SCENARIOS=	file-last dir-last orphan-last free-last lf-last

.if ${ILP32_OK} == "yes"
check: check-program check-mutants
.else
check:
	@echo "SKIP fsck_inode_range: ${HOSTCC} cannot compile and execute ILP32 binaries" >&2
	@test "${REQUIRE_ILP32:Uno}" != yes
.endif

check-program: fsck_host image_gen
	sh ${.CURDIR}/run.sh ./fsck_host ./image_gen work/program ${ALL_SCENARIOS}

# Every mutant must fail each scenario named for it; a scenario that passes
# on a mutant means the gate no longer decides that site. Running one
# scenario at a time keeps a pass of an unrelated scenario from masking the
# verdict.
check-mutants: ${MUTANTS:S/^/fsck_mutant_/} image_gen
.for mutant in ${MUTANTS}
.for scenario in ${SCENARIOS.${mutant}}
	@if sh ${.CURDIR}/run.sh ./fsck_mutant_${mutant} ./image_gen \
	    work/${mutant}-${scenario} ${scenario} >/dev/null 2>&1; then \
		echo "mutant ${mutant} passes ${scenario}"; exit 1; \
	fi
	@echo "mutant ${mutant} fails ${scenario}"
.endfor
.endfor

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

objs/glue.o: fsck_glue.c fsck_glue.h ${MACHINE_STAMP}
	@mkdir -p objs
	${HOSTCC} -std=gnu17 ${ILP32} ${GATE_WARN} -I. -I${TOPSRC}/include \
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
# pass1.c, dir.c and inode.c, none of which takes a per-unit definition.
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

clean:
	rm -f fsck_host image_gen fsck_mutant_*
	rm -f objs/*.o mutant/*/*.c mutant/*/*.o work/*/*.img work/*/fsck.out
	rm -f machine/*.h ${MACHINE_STAMP}
	@for directory in objs machine mutant/* mutant work/* work; do \
		if [ -d $$directory ]; then rmdir $$directory; fi; \
	done

all: check

.PHONY: all check check-program check-mutants clean
