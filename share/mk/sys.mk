# Override the default port with:
# $ make MACHINE=pic32 MACHINE_ARCH=mips
#
MACHINE=	stm32
MACHINE_ARCH=	arm

unix=		We run DiscoBSD.
OSMAJOR=	2
OSMINOR=	7
OSREV=		${OSMAJOR}.${OSMINOR}
OSRev=		${OSMAJOR}_${OSMINOR}
OSrev=		${OSMAJOR}${OSMINOR}

DESTDIR?=	${TOPSRC}/distrib/obj/destdir.${MACHINE}
RELEASE=	${OSREV}
# The -- keeps HEAD a revision in a directory that holds a file named head
# (usr.bin/head after a build) on a case-insensitive filesystem.
BUILD!=		git rev-list --count HEAD --
VERSION=	${RELEASE}-${BUILD}

TOOLDIR?=	${TOPSRC}/tools
TOOLBINDIR?=	${TOOLDIR}/bin

HOST_CC?=	cc -Werror

# The interpreter for every verifier and harness, declared once and passed
# to each sub-make; a Makefile names it as ${PYTHON} and never by a literal.
PYTHON?=	python3
.export PYTHON

_HOST_OSNAME!=	uname -s

# libbsd-dev package on Linux in overlay mode.
_LIBBSD_CFLAGS!=if [ x"${_HOST_OSNAME}" = x"Linux" ] ; then \
			pkg-config libbsd-overlay --cflags ; \
		else \
			echo "" ; \
		fi

_LIBBSD_LIBS!=	if [ x"${_HOST_OSNAME}" = x"Linux" ] ; then \
			pkg-config libbsd-overlay --libs ; \
		else \
			echo "" ; \
		fi

include ${TOPSRC}/share/mk/mips-toolchain.mk

# The arm toolchain on PATH first (Homebrew on macOS, or any prefix the
# developer chose), then the path each operating system's package uses.
GCCPREFIX!=if [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		if command -v arm-none-eabi-gcc >/dev/null 2>&1 ; then \
			echo "$$(command -v arm-none-eabi-gcc | sed 's/-gcc$$//')" ; \
		elif [ x"${_HOST_OSNAME}" = x"OpenBSD" ] ; then \
			echo "/usr/local/bin/arm-none-eabi" ; \
		elif [ x"${_HOST_OSNAME}" = x"FreeBSD" ] ; then \
			echo "/usr/local/gcc-arm-embedded/bin/arm-none-eabi" ; \
		elif [ x"${_HOST_OSNAME}" = x"Linux" ] ; then \
			echo "/usr/bin/arm-none-eabi" ; \
		else \
			echo "/does/not/exist" ; \
		fi \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "${MIPS_GCCPREFIX}" ; \
	else \
		echo "/does/not/exist" ; \
	fi

# The RP2040's Cortex-M0+ implements ARMv6-M, a strict subset of the
# Cortex-M4's Thumb-2, so a userland built for cortex-m4 faults on it.
MACHINE_CPU!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "cortex-m0plus" ; \
	else \
		echo "cortex-m4" ; \
	fi

# RP2040 diagnostics read kernel structures through /dev/kmem and sysctl.
# Keep every target program on the same fixed-table layout as the kernel.
KERNEL_LAYOUT_CFLAGS!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "-DLINEAR_INODE_CACHE -DCOMPACT_INODE_FIELDS -DCOMPACT_SWAPMAP -DSINGLE_UFS_ROOT -DNMOUNT=1" ; \
	else \
		echo "" ; \
	fi

# The sources predate C23, where an empty parameter list means (void) and
# old-style definitions are gone. GCC 15 and later default to C23. The dialect
# and tentative-definition policy ride on CC because several Makefiles replace
# CFLAGS outright.
CC!=	if [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		echo "${GCCPREFIX}-gcc -std=gnu17 -fno-common -mcpu=${MACHINE_CPU} -mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft ${KERNEL_LAYOUT_CFLAGS} -nostdinc -I${TOPSRC}/include ${INCLUDES}" ; \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "${GCCPREFIX}-gcc -mips32r2 -EL -msoft-float -nostdinc -I${TOPSRC}/include ${INCLUDES}" ; \
	else \
		echo "/does/not/exist" ; \
	fi

# Enable mips16e instruction set by default
COPTS!=if [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		echo "-Os -fno-common" ; \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "-Os -fcommon -mips16" ; \
	else \
		echo "" ; \
	fi

LDWARN!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "-Wl,--warn-rwx-segments -Wl,--fatal-warnings" ; \
	elif [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		if [ x"${_HOST_OSNAME}" = x"FreeBSD" ] ; then \
			echo "" ; \
		else \
			echo "-Wl,--no-warn-rwx-segments" ; \
		fi \
	else \
		echo "-Wl,--no-warn-rwx-segments" ; \
	fi

LDTEXT!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		printf '%s\n' '-n' ; \
	else \
		echo "-N" ; \
	fi

# Keep severity on CC: leaf Makefiles replace CFLAGS for the native
# a.out compiler/assembler path as well as ordinary cross-built programs.
CC+=	-Werror

CFLAGS=	${COPTS}

AFLAGS=	${ASFLAGS}

# Floating point printf conversion is a separate libc member, doprnt_float.o,
# about 10 kbytes with the software double arithmetic it carries. Every
# machine links it into every program as before, except rp2040, whose root
# is small enough that only a program declaring PRINTF_FLOAT=yes gets it.
PRINTF_FLOAT?=	no
_PRINTF_FLOAT!=	if [ x"${MACHINE}" != x"rp2040" -o x"${PRINTF_FLOAT}" = x"yes" ] ; then \
			echo "-Wl,-u,__doprnt_cvt" ; \
		fi

LDFLAGS=${LDTEXT} -nostartfiles -fno-dwarf2-cfi-asm \
	${LDWARN} ${_PRINTF_FLOAT} \
	-T${TOPSRC}/lib/elf32-${MACHINE_ARCH}.ld \
	${TOPSRC}/lib/crt0.o -L${TOPSRC}/lib

LIBS=	-lc
LDLIBS=	${LIBS}

OBJDUMP!=if [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		echo "${GCCPREFIX}-objdump -marm -M force-thumb" ; \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "${GCCPREFIX}-objdump -mmips:isa32r2" ; \
	else \
		echo "/does/not/exist" ; \
	fi

# byacc where present (Linux ships it as such; Homebrew too), else the
# system yacc; the yacc in Apple's command line tools is a shim that
# insists on a full Xcode.
YACC!=	if command -v byacc >/dev/null 2>&1 ; then \
		echo "byacc" ; \
	else \
		echo "yacc" ; \
	fi

YFLAGS=	-d

LD=		${GCCPREFIX}-ld
AR=		${GCCPREFIX}-ar
RANLIB=		${GCCPREFIX}-ranlib
SIZE=		${GCCPREFIX}-size
AS=		${CC} -x assembler-with-cpp -c

LEX=		flex
INSTALL=	${TOOLBINDIR}/binstall -U

TAGSFILE=	tags

# groff formats the same pages where mandoc is absent, which on Linux
# distributions that ship man-db is the common case.
MANROFF!=	if command -v mandoc >/dev/null 2>&1 ; then \
			echo 'mandoc -Tascii -Ios="DiscoBSD ${OSREV}"' ; \
		else \
			echo 'groff -mandoc -Tascii' ; \
		fi

# The termcap reorder script is an ex script; vim runs it in ex mode where
# no ex is installed.
EX!=		if command -v ex >/dev/null 2>&1 ; then \
			echo 'ex -' ; \
		else \
			echo 'vim -es -u NONE' ; \
		fi

ELF2AOUT=	${TOOLBINDIR}/elf2aout

AOUT_AOUT=	${TOOLBINDIR}/aout
AOUT_AR=	${TOOLBINDIR}/ar
AOUT_AS=	${TOOLBINDIR}/as
AOUT_LD=	${TOOLBINDIR}/ld
AOUT_NM=	${TOOLBINDIR}/nm
AOUT_RANLIB=	${TOOLBINDIR}/ranlib
AOUT_SIZE=	${TOOLBINDIR}/size
AOUT_STRIP=	${TOOLBINDIR}/strip
