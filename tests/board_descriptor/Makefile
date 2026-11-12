HOSTCC?=	cc
TOPSRC!=	cd ../..; pwd
KERNEL_UF2?=	${TOPSRC}/sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2

.if empty(PYTHON)
.error PYTHON must name the intended interpreter
.endif

check:
		${PYTHON} -B board_descriptor_test.py --cc=${HOSTCC} \
		    --top=${TOPSRC} --kernel=${KERNEL_UF2}

all:		check

.PHONY: all check
