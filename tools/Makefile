TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/architecture.mk

.if ${MACHINE} == "rp2040"
SUBDIR=	binstall aoututils elf2aout hsaout fsutil config boot2sum flashimg
.else
SUBDIR=	binstall aoututils elf2aout hsaout fsutil config
.endif

all install depend: ${SUBDIR}
		set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i ${MFLAGS} $@; done

clean:
	status=0; rm -f *~ ${TOOLBINDIR}/* || status=1; \
	for i in ${SUBDIR}; do \
		${MAKE} -C $$i ${MFLAGS} clean || status=1; \
	done; exit $$status
