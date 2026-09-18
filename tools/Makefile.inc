# Common defines and tools to support cross-hosting platforms.

_HOST_OSNAME!=	uname -s

TOOLDIR?=	${TOPSRC}/tools
TOOLBINDIR?=	${TOOLDIR}/bin

HOST_CC?=	cc

# A tool may replace CFLAGS without opting out of fatal diagnostics.
CC=		${HOST_CC} -Werror

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
