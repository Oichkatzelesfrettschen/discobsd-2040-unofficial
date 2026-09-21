TOPSRC!=cd ../..; pwd

include ${TOPSRC}/share/mk/architecture.mk

.if ${BUILD_PDP11_V6} != "yes"
.error legacy/pdp11-v6 requires BUILD_PDP11_V6=yes
.endif

EMULATOR_DIR=	${.CURDIR}/usr.bin/pdp11
REFERENCE_DIR=	${.CURDIR}/tests/pdp11_reference
MAP=		${TOPSRC}/tools/architecture-isolation/legacy-path-map.tsv
VERIFY=		${TOPSRC}/tools/architecture-isolation/verify-legacy-path-map.sh

all: emulator

emulator:
	${MAKE} -C ${EMULATOR_DIR} TOPSRC=${TOPSRC} MACHINE=${MACHINE} \
	    BUILD_PDP11_V6=yes all

install:
	${MAKE} -C ${EMULATOR_DIR} TOPSRC=${TOPSRC} MACHINE=${MACHINE} \
	    BUILD_PDP11_V6=yes DESTDIR=${DESTDIR} install

check-v6: verify-map
	${MAKE} -C ${EMULATOR_DIR} TOPSRC=${TOPSRC} MACHINE=${MACHINE} \
	    BUILD_PDP11_V6=yes test

check-v7-runner: verify-map
	${MAKE} -C ${REFERENCE_DIR} PYTHON=${PYTHON} check

check-v7-reference: verify-map
	${MAKE} -C ${REFERENCE_DIR} PYTHON=${PYTHON} \
	    PDP11_V7_IMAGE=${PDP11_V7_IMAGE} \
	    PDP11_EVIDENCE_DIR=${PDP11_EVIDENCE_DIR} reference

verify-map:
	sh ${VERIFY} ${TOPSRC} ${MAP} archive

clean:
	${MAKE} -C ${EMULATOR_DIR} TOPSRC=${TOPSRC} MACHINE=${MACHINE} \
	    BUILD_PDP11_V6=yes clean

.PHONY: all emulator install check-v6 check-v7-runner check-v7-reference \
	verify-map clean
