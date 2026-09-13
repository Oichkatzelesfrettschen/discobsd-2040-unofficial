# Host test for the compressed swap tier. It links the shipped allocator and
# the vendored codec, so a pass covers the code the kernel runs, not a copy.
#
#   make            build and run with the default parameters
#   make sweep      the window and lookahead grid the research note records
#   make valgrind   the same run under memcheck
#
# IMAGE names an a.out built for the board; the default comes out of the
# distribution tree a "bmake MACHINE=rp2040 build" leaves behind.

HS=	../../heatshrink
INC=	../../include
TOP=	../../../../..
IMAGE?=	$(TOP)/distrib/obj/destdir.rp2040/bin/sh
STACK?=	4096

CC?=	cc
# machine/ mirrors what compile/PICO/Makefile links, so swapram_pool.c
# reaches <machine/swapram.h> by the same path the kernel build gives it.
CFLAGS=	-std=gnu11 -g -O2 -Wall -Wextra -Werror -I. -I$(HS) $(HSOPTS)

# The codec is vendored verbatim, and upstream falls through a switch case
# without the attribute GCC 7 and later want, so that one warning is waived
# for those two files alone. The kernel build never sees it: compile/PICO
# sets CWARNFLAGS to -Wall, and -Wimplicit-fallthrough rides with -Wextra.
HSFLAGS= $(CFLAGS) -Wno-implicit-fallthrough

OBJS=	swapram_test.o swapram_pool.o heatshrink_encoder.o \
	heatshrink_decoder.o

# The evacuation test compiles the kernel's swapram.c and subr_rmap.c
# against the stand-in headers in shim/, which come first on the include
# path so <sys/param.h> and its neighbors resolve there. Their printf
# formats are written for the kernel's 32-bit size_t, so -Wformat is off
# for those two objects alone.
EVACOBJS= swapram_evac_test.o swapram_kern.o subr_rmap.o swapram_pool_k.o \
	heatshrink_encoder.o heatshrink_decoder.o
KCFLAGS= -std=gnu11 -g -O2 -Wall -Wextra -Werror -DSWAPRAM_KB=64 -Ishim -I. -I$(HS) $(HSOPTS)

all: run evac

machine:
	ln -sf $(INC) machine

swapram_test.o: swapram_test.c machine $(HS)/heatshrink_config.h
	$(CC) $(CFLAGS) -c -o $@ swapram_test.c

swapram_pool.o: ../../rp2040/swapram_pool.c machine $(INC)/swapram.h
	$(CC) $(CFLAGS) -c -o $@ ../../rp2040/swapram_pool.c

heatshrink_encoder.o: $(HS)/heatshrink_encoder.c $(HS)/heatshrink_config.h
	$(CC) $(HSFLAGS) -c -o $@ $(HS)/heatshrink_encoder.c

heatshrink_decoder.o: $(HS)/heatshrink_decoder.c $(HS)/heatshrink_config.h
	$(CC) $(HSFLAGS) -c -o $@ $(HS)/heatshrink_decoder.c

swapram_test: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

run: swapram_test
	./swapram_test $(IMAGE) $(STACK)

swapram_evac_test.o: swapram_evac_test.c machine $(INC)/swapram.h
	$(CC) $(KCFLAGS) -c -o $@ swapram_evac_test.c

swapram_kern.o: ../../rp2040/swapram.c machine $(INC)/swapram.h
	$(CC) $(KCFLAGS) -Wno-format -c -o $@ ../../rp2040/swapram.c

swapram_pool_k.o: ../../rp2040/swapram_pool.c machine $(INC)/swapram.h
	$(CC) $(KCFLAGS) -c -o $@ ../../rp2040/swapram_pool.c

subr_rmap.o: $(TOP)/sys/kern/subr_rmap.c
	$(CC) $(KCFLAGS) -Wno-format -c -o $@ $(TOP)/sys/kern/subr_rmap.c

swapram_evac_test: $(EVACOBJS)
	$(CC) $(KCFLAGS) -o $@ $(EVACOBJS)

evac: swapram_evac_test
	./swapram_evac_test

valgrind: swapram_test
	valgrind -q --error-exitcode=9 --leak-check=full \
	    ./swapram_test $(IMAGE) $(STACK)

# Window and lookahead cost nothing but encoder bss and match length; the
# sweep is what picks the pair the kernel ships.
sweep:
	@for w in 8 9 10 11 12; do for l in 4 6 8 10; do \
	  if [ $$l -lt $$w ]; then \
	    $(MAKE) -s clean >/dev/null; \
	    $(MAKE) -s swapram_test HSOPTS="-DHEATSHRINK_STATIC_WINDOW_BITS=$$w -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=$$l" >/dev/null; \
	    ./swapram_test $(IMAGE) $(STACK) | grep -E '^heatshrink|^encoder|^image'; \
	    echo; \
	  fi; done; done

clean:
	rm -f swapram_test swapram_evac_test $(OBJS) $(EVACOBJS) machine

.PHONY: all run valgrind sweep clean
