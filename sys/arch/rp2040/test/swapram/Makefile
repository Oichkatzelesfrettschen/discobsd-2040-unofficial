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

all: run

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
	rm -f swapram_test $(OBJS) machine

.PHONY: all run valgrind sweep clean
