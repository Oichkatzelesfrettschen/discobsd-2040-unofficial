# Fatal diagnostics ride on CC rather than CFLAGS, because leaf Makefiles
# replace CFLAGS outright and would drop them. WARNLEVEL selects the set a
# directory is held to, and a leaf Makefile assigns it before it includes
# sys.mk, since sys.mk composes CC at include time and a later assignment
# reaches nothing:
#   full    -Wall -Wextra -Werror, the default, for a directory that
#           compiles clean under both groups
#   legacy  -Werror alone, so only the groups the Makefile's own CFLAGS
#           name are fatal, for a directory whose sites are still open;
#           the Makefile states the count tools/warning-census.sh
#           measured, and the count goes when the sites do
# tools/warning-census.sh overrides WARNERR on the command line, which
# wins over both, so the census measures every directory at one level.
WARNLEVEL?=	full
.if ${WARNLEVEL} == "legacy"
WARNERR=	-Werror
.elif ${WARNLEVEL} == "full"
WARNERR=	-Wall -Wextra -Werror
.else
.error WARNLEVEL must be full or legacy, not ${WARNLEVEL}
.endif
