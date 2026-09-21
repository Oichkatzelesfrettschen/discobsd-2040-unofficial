#
# Standard CPU benchmarks ported to DiscoBSD. Each subdirectory is a
# self-contained PROG that builds and installs like any userland binary;
# see sys/arch/rp2040/doc/research/benchmarks.md for what each measures.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR		= coremark

all:		$(SUBDIR)

$(SUBDIR):	FRC
		$(MAKE) -C $@ $(MFLAGS)
# A subdirectory is a directory that exists, so without this bmake judges
# it by mtime against FRC, which is stamped with the second the make began:
# a directory the preceding clean touched in that same second compares as
# up to date and is skipped, which is how lib/crt0.o went missing from a
# clean-then-build with lib/startup-arm never entered.
.PHONY:	$(SUBDIR)

FRC:

install:
		-for i in $(SUBDIR); do \
			${MAKE} -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install; done

clean:
		status=0; for i in ${SUBDIR}; do \
			${MAKE} -C $$i ${MFLAGS} clean || status=1; \
		done; exit $$status
