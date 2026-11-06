# Host and qemu-arm test resources shared by the gates that check one body
# of C on the build host, most of them again as a Thumb-1 (ARMv6-M)
# program: tests/libcalc, tests/libbn, usr.bin/calc and games/fourfall
# (sanitizers only). The including Makefile sets
# TOPSRC and HOSTCC and includes this file after its default target, since
# the first rule bmake reads becomes the default and this file defines the
# tl_*.o rules.
#
#   HOST_FORTIFY          -O1 with _FORTIFY_SOURCE=2, for host builds of
#                         programs that call write and read directly
#   SAN_FLAGS, SAN_OK     AddressSanitizer plus UndefinedBehaviorSanitizer,
#                         and "yes" when tools/compiler-probe.sh links them
#   OBJCOPY_OK            "yes" when objcopy is on the path
#   TREELIBM, TREELIBM_OBJS
#                         the tree's lib/libm routines compiled for the host
#                         (tl_NAME.o) plus tl_errno.o; a host program linked
#                         with them ahead of -lm calls the libm code the
#                         target links, on host IEEE arithmetic
#   QEMU_ARM              the qemu-arm path, empty when it is absent
#   QEMU_ARM_CC, QEMU_ARM_CFLAGS, QEMU_ARM_LIBS, QEMU_SUPPORT
#                         a newlib link of a test program for Cortex-M0+ with
#                         the Linux EABI system calls of
#                         tests/libbn/qemu_support.c, for qemu-arm user mode
#
# A consumer that finds qemu-arm absent prints a SKIP line naming the missing
# execution, and fails instead when REQUIRE_QEMU is set (root check-qemu).

# glibc's fortified headers mark write and read warn_unused_result and add
# object-size checks, but only with optimization on and _FORTIFY_SOURCE set;
# some distribution compilers set it by default and some do not. Host builds
# that add HOST_FORTIFY see the same diagnostics on every host. The -U first
# keeps a compiler's own definition from being redefined.
HOST_FORTIFY=	-O1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2

SAN_FLAGS=	-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer
SAN_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SAN_FLAGS}

OBJCOPY_OK!=	if command -v objcopy >/dev/null 2>&1; then echo yes; else echo no; fi
TREELIBM=	sin sinh log exp atan sqrt pow hypot tanh asin floor fmod tan
TREELIBM_OBJS=	${TREELIBM:S/^/tl_/:S/$/.o/} tl_errno.o

# The tree libm is K&R C that predates the host dialect; its diagnostics
# belong to lib/libm, not to the consumer. Builtins are off so the tree
# routines, not inlined host versions, serve the calls, and errno is renamed
# because the host errno is thread-local.
.for f in ${TREELIBM}
tl_${f}.o:	${TOPSRC}/lib/libm/${f}.c
		${HOSTCC} -std=gnu89 -w -O1 -fno-builtin -I${TOPSRC}/include -c \
		    ${TOPSRC}/lib/libm/${f}.c -o $@
		objcopy --redefine-sym errno=tl_errno $@
.endfor

tl_errno.o:	${TOPSRC}/tests/libcalc/tl_errno.c
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -c \
		    ${TOPSRC}/tests/libcalc/tl_errno.c -o $@

QEMU_ARM!=	command -v qemu-arm 2>/dev/null || true
QEMU_ARM_CC=	${GCCPREFIX}-gcc
QEMU_ARM_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -Os -mcpu=cortex-m0plus \
		-mthumb -mfloat-abi=soft -nostartfiles -Wl,-Ttext=0x20000
QEMU_ARM_LIBS=	-lc -lgcc
QEMU_SUPPORT=	${TOPSRC}/tests/libbn/qemu_support.c
