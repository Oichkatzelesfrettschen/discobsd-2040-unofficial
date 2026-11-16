TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the gate takes the
# tree's default from share/mk/sys.mk; a MACHINE on the command line still
# wins over both.
include ${TOPSRC}/share/mk/sys.mk

PYTHON?=
HSAOUT=		${TOPSRC}/tools/hsaout

# check.sh compiles tools/fsutil itself, so a mutant needs no second tree;
# the pack library fsutil links is the only product built by make.
check:
		${MAKE} -C ${HSAOUT} MACHINE=${MACHINE} libhsaout.a
		PYTHON=${PYTHON:Q} sh ${.CURDIR}/check.sh ${HSAOUT}/libhsaout.a

.PHONY: check
