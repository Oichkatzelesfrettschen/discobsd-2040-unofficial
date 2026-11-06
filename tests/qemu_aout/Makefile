# Prototype: run a DiscoBSD OMAGIC a.out under qemu-arm user mode.
#
# aoutrun.c loads the a.out the tree links (lib/crt0.o, lib/libc.a, the
# lib/elf32-arm.ld layout at 0x20000000, elf2aout) into the 144 KB user window,
# serves its "svc #N" system calls from the Linux kernel and, when the
# emulator gives the guest address 0, models the boot ROM float tables. This
# directory is outside the root check-qemu until a gate adopts it; "check"
# builds and runs the probe program and compares its output and exit status.
#
# It needs the tree's libraries from "bmake MACHINE=rp2040 build" (lib/libc.a,
# lib/crt0.o) and tools/bin/rp2040/elf2aout. REQUIRE_QEMU makes a missing
# qemu-arm an error; otherwise the target prints SKIP.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

# qemu-arm accepts a guest at address 0 only with a guest base (-B).
GUEST_BASE?=	0x100000
LOADER_FLAGS=	-std=gnu17 -Wall -Wextra -Werror -Os ${QEMU_ARM_CPUFLAGS} \
		-ffreestanding -fno-delete-null-pointer-checks -nostdlib \
		-nostartfiles -static -Wl,--build-id=none -Wl,-Ttext=0x40000000

all: check

aoutrun: aoutrun.c
	${QEMU_ARM_CC} ${LOADER_FLAGS} -o $@ aoutrun.c -lgcc

probe.o: probe.c
	${CC} ${CFLAGS} -c -o $@ probe.c

probe.elf: probe.o ${TOPSRC}/lib/libc.a ${TOPSRC}/lib/crt0.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ probe.o ${LIBS}

probe: probe.elf
	${ELF2AOUT} probe.elf $@

# Every "svc" in the linked image, one address per line in ascending order.
probe.sites: probe.elf
	${OBJDUMP} -d probe.elf | awk '/\tsvc\t/ { sub(":", "", $$1); print $$1 }' | \
	    sort > $@

# Fixture for the seek cases in probe.c.
seek.dat:
	printf '0123456789' > $@

check: aoutrun probe probe.sites seek.dat
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "qemu a.out: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP qemu a.out: qemu-arm unavailable; no ARM execution occurred"
.else
	@status=0; ${QEMU_ARM} -B ${GUEST_BASE} -cpu ${QEMU_ARM_CPU} ./aoutrun \
	    probe.sites probe alpha beta > probe.out || status=$$?; \
	if [ "$$status" -ne 3 ]; then \
		echo "qemu a.out: probe exited $$status, expected 3" >&2; exit 1; \
	fi
	diff probe.expected probe.out
	@echo "qemu a.out: ok"
.endif

clean:
	rm -f aoutrun probe probe.o probe.elf probe.sites probe.out seek.dat

.PHONY: all check clean
