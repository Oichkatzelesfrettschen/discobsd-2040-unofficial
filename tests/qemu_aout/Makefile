# Prototype: run a DiscoBSD OMAGIC a.out under qemu-arm user mode.
#
# aoutrun.c loads the a.out the tree links (lib/crt0.o, lib/libc.a, the
# lib/elf32-arm.ld layout at 0x20000000, elf2aout) into the 144 KB user window,
# serves its "svc #N" system calls from the Linux kernel and, when the
# emulator gives the guest address 0, models the boot ROM float tables. This
# directory is part of the root check-qemu; "check" builds and runs the probe
# program and compares its output and exit status. The SIO hardware divider
# the libc division helpers drive is served by the loader's SIGSEGV handler.
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

# Fixture for the symbolic-link-loop errno case in probe.c. A self-link never
# stats as up to date, so the recipe replaces it on every run.
loop.lnk:
	ln -sf loop.lnk $@

# The run line shared by the probe modes; the mode is its last argument.
RUN=		${QEMU_ARM} -B ${GUEST_BASE} -cpu ${QEMU_ARM_CPU} ./aoutrun \
		    probe.sites probe

check: aoutrun probe probe.sites seek.dat loop.lnk
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
	@# Linux errno values reach the program as the DiscoBSD numbers.
	@status=0; ${RUN} errno > errno.out || status=$$?; \
	if [ "$$status" -ne 6 ]; then \
		echo "qemu a.out: errno probe exited $$status, expected 6" >&2; exit 1; \
	fi
	diff errno.expected errno.out
	@# Integer division reaches the loader's SIO divider model.
	@status=0; ${RUN} div > div.out || status=$$?; \
	if [ "$$status" -ne 9 ]; then \
		echo "qemu a.out: div probe exited $$status, expected 9" >&2; exit 1; \
	fi
	diff div.expected div.out
	@# brk() refuses a request outside the window, one near 2^32 included.
	@status=0; ${RUN} brk > brk.out || status=$$?; \
	if [ "$$status" -ne 7 ]; then \
		echo "qemu a.out: brk probe exited $$status, expected 7" >&2; exit 1; \
	fi
	diff brk.expected brk.out
	@# An environment far above the old 4096-byte stack reserve still arrives;
	@# env -i fixes the size of everything else the guest inherits.
	@big=$$(head -c 50000 /dev/zero | tr '\0' x); status=0; \
	env -i AOUT_BIG=$$big AOUT_BIG2=$$big ${RUN} env > env.out || status=$$?; \
	if [ "$$status" -ne 8 ]; then \
		echo "qemu a.out: env probe exited $$status, expected 8" >&2; exit 1; \
	fi
	diff env.expected env.out
	@# Strings that fit the window but not above the image (about 7.8 KB of
	@# text, data and bss) end the run with the setup status instead of
	@# overwriting the image.
	@big=$$(head -c 71500 /dev/zero | tr '\0' x); status=0; \
	env -i AOUT_BIG=$$big AOUT_BIG2=$$big ${RUN} env \
	    > /dev/null 2> envfull.err || status=$$?; \
	if [ "$$status" -ne 71 ]; then \
		echo "qemu a.out: oversized environment exited $$status, expected 71" >&2; exit 1; \
	fi
	grep -q 'overlap the image' envfull.err
	@echo "qemu a.out: ok"
.endif

clean:
	rm -f aoutrun probe probe.o probe.elf probe.sites probe.out seek.dat \
	    loop.lnk errno.out brk.out div.out env.out envfull.err

.PHONY: all check clean
