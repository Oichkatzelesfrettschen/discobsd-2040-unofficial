# Fatal diagnostics ride on CC rather than CFLAGS, because leaf Makefiles
# replace CFLAGS outright and would drop them. WARNLEVEL selects the set a
# directory is held to, and a leaf Makefile assigns it before it includes
# sys.mk, since sys.mk composes CC at include time and a later assignment
# reaches nothing:
#   full    -Wall -Wextra -Werror, the default, for a directory that
#           compiles clean under both groups; -Wformat-security joins
#           the -Wformat that -Wall enables, so a format argument that
#           is a variable with no arguments after it is fatal
#   legacy  -Wformat -Wformat-security -Werror, so the format group and
#           the groups the Makefile's own CFLAGS name are fatal, for a
#           directory whose other -Wall -Wextra sites are still open;
#           the Makefile states the count tools/warning-census.sh
#           measured, and the count goes when the sites do
# tools/warning-census.sh overrides WARNERR on the command line, which
# wins over both, so the census measures every directory at one level.
WARNLEVEL?=	full
.if ${WARNLEVEL} == "legacy"
WARNERR=	-Wformat -Wformat-security -Werror
.elif ${WARNLEVEL} == "full"
WARNERR=	-Wall -Wextra -Wundef -Wformat-security -Werror
.else
.error WARNLEVEL must be full or legacy, not ${WARNLEVEL}
.endif
