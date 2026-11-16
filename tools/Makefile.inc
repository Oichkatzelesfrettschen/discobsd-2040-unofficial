# Common defines and tools to support cross-hosting platforms.

_HOST_OSNAME!=	uname -s

include ${TOPSRC}/share/mk/architecture.mk

TOOLDIR?=	${TOPSRC}/tools
TOOLBINDIR?=	${TOOLDIR}/bin/${MACHINE}

HOST_CC?=	cc

include ${TOPSRC}/share/mk/warnings.mk

# glibc checks the destination size of the string and memory calls it can
# bound when _FORTIFY_SOURCE is set, and requires optimization to do it. -O2
# rides on CC beside it because a tool Makefile that replaces CFLAGS without
# an -O option would otherwise fail on the header's #warning; a later -O in
# CFLAGS still selects that tool's level.
CC=		${HOST_CC} ${WARNERR} -O2 -pipe -D_FORTIFY_SOURCE=3

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

INSTALL=	${TOOLBINDIR}/binstall -U

# byacc where present (Linux ships it as such; Homebrew too), else the
# system yacc; the yacc in Apple's command line tools is a shim that
# insists on a full Xcode.
YACC!=		if command -v byacc >/dev/null 2>&1 ; then \
			echo "byacc" ; \
		else \
			echo "yacc" ; \
		fi

YFLAGS=		-d

# mandoc where present, else groff, matching share/mk/sys.mk; a hard
# mandoc dependency silently drops config from tools/bin on a host that
# ships man-db rather than mandoc, and the kernel build then fails to
# reconfigure.
MANROFF!=	if command -v mandoc >/dev/null 2>&1 ; then \
			echo 'mandoc -Tascii' ; \
		else \
			echo 'groff -mandoc -Tascii' ; \
		fi
