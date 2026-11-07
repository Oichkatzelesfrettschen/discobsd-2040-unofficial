# Host gate for sys/kern/subr_crc32.c, the CRC-32 the packed executable
# loader, the packer in tools/hsaout and tests/hsaout share.
#
#   check    the kernel source built as a host shared object and compared
#            with zlib.crc32 over the check vector, empty input, every
#            length 0..300 at eight alignments, incremental updates split
#            at every offset of a 1 KB buffer and all 256 single bytes;
#            then nine mutants of the source, each of which must be rejected
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
HOSTCC?=	cc
TOPSRC!=	cd ../..; pwd
SOURCE=		${TOPSRC}/sys/kern/subr_crc32.c

.if empty(PYTHON)
.error PYTHON must name the intended interpreter
.endif

check:
		${PYTHON} -B crc32_test.py --cc ${HOSTCC} --source ${SOURCE} \
		    --include ${TOPSRC}/sys
		${PYTHON} -B crc32_test.py --cc ${HOSTCC} --source ${SOURCE} \
		    --include ${TOPSRC}/sys --mutants

all:		check

.PHONY: all check
