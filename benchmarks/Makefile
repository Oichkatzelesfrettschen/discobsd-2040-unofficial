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

FRC:

install:
		-for i in $(SUBDIR); do \
			${MAKE} -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install; done

clean:
		-for i in $(SUBDIR); do ${MAKE} -C $$i $(MFLAGS) clean; done
