TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

check-qemu: nm-arm
.if empty(QEMU_ARM)
	@echo "nm reader: qemu-arm is required for ARM execution" >&2; exit 1
.else
	${PYTHON} ${TOPSRC}/tools/check_nm_reader.py ./nm-arm --runner '${QEMU_ARM_RUN}'
.endif

nm-arm: ${TOPSRC}/usr.bin/nm/nm.c qemu_support.c ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -DCROSS \
	    -I${TOPSRC}/tools/aoututils/include -idirafter ${TOPSRC}/include \
	    ${TOPSRC}/usr.bin/nm/nm.c qemu_support.c ${QEMU_ARM_LIBS} -o $@

.PHONY: check-qemu
