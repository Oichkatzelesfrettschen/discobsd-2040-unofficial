# Host test for sys/sys/exec_aout.h: the a_midmag field macros and the
# a.out layout checker exec runs before committing to an image.
HOSTCC?=	cc
PROG=		aout_header_test

check:		${PROG}
		./${PROG}

${PROG}:	${PROG}.c ../../sys/sys/exec_aout.h
		${HOSTCC} -std=c99 -Wall -Wextra -Werror -o $@ ${PROG}.c

all:		check

clean:
		rm -f ${PROG}

.PHONY:		all check clean
