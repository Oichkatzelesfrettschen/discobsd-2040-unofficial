#
# Host gate for the PicoCalc loader's flash limit. loader_stamp_test.c runs
# the stamp decode and the erase/program bound of
# sys/arch/rp2040/dev/loader_stamp.h, unchanged, through exhaustive sweeps of
# offsets and lengths at the 4 KB erase and 256-byte program granularities,
# the edges at the limit, every limit the decode accepts, the limits it must
# refuse, and the raw swap writer of flash_swap.c over a whole-chip NOR
# model. check_choke.sh requires flash.c's erase and program primitives to
# be the only boot ROM writers and to apply the bound first.
#
# Once the shipped sources pass, each mutant below must fail: the bound
# with <= in place of <, a bound on the offset alone, the stamp magic
# ignored, an invalid limit accepted, an unaligned limit accepted, a
# fail-open fallback, a swap extent that ignores the limit, and either
# primitive without its bound. A gate that passed them would be checking
# nothing.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
ARCH=		${TOPSRC}/sys/arch
CFLAGS=		-std=gnu17 -O1 -Wall -Wextra -Werror

all: check

check: loader_stamp_test
	./loader_stamp_test
	sh ./check_choke.sh ${DEV}/flash.c
	${MAKE} mutants

loader_stamp_test: loader_stamp_test.c ${DEV}/loader_stamp.h ${DEV}/flash.h \
    ${DEV}/flash_swap.h ${DEV}/flash_swap.c
	${HOST_CC} ${CFLAGS} -I${ARCH} -o $@ loader_stamp_test.c \
	    ${DEV}/flash_swap.c

MUT=		@sh ./mutant.sh "${HOST_CC}" "${DEV}"

mutants: mutant-bound-le mutant-offset-only mutant-magic-ignored \
	mutant-limit-accepted mutant-limit-unaligned mutant-fail-open \
	mutant-swap-extent mutant-erase-unguarded mutant-program-unguarded

# The last byte of an operation may reach the limit itself.
mutant-bound-le:
	${MUT} loader_stamp.h \
	    's/return len - 1 < write_end - offset;/return len - 1 <= write_end - offset;/'

# Only the start of an operation is checked.
mutant-offset-only:
	${MUT} loader_stamp.h \
	    's/return len - 1 < write_end - offset;/return 1;/'

# A stamp is decoded whatever the magic word holds.
mutant-magic-ignored:
	${MUT} loader_stamp.h \
	    's/if (magic != LOADER_STAMP_MAGIC) {/if (0) {/'

# Any stamped limit is taken as the writable end.
mutant-limit-accepted:
	${MUT} loader_stamp.h \
	    's/if (limit >= FLASH_XIP_BASE \&\& offset > FLASH_FS_OFFSET \&\&/if (1 || limit >= FLASH_XIP_BASE \&\& offset > FLASH_FS_OFFSET \&\&/'

# A limit inside an erase sector is accepted.
mutant-limit-unaligned:
	${MUT} loader_stamp.h \
	    's/(offset \& (FLASH_SECTOR_BYTES - 1)) == 0)/1)/'

# A bad limit leaves the whole chip writable.
mutant-fail-open:
	${MUT} loader_stamp.h \
	    's/\*write_end = FLASH_FS_OFFSET;/*write_end = FLASH_TOTAL_BYTES;/'

# fl1 keeps its full extent under the stamp.
mutant-swap-extent:
	${MUT} loader_stamp.h \
	    's/return write_end - FLASH_SWAP_OFFSET;/return FLASH_SWAP_BYTES;/'

# flash_erase() reaches the boot ROM without the bound.
mutant-erase-unguarded:
	${MUT} flash.c \
	    '/^flash_erase(/,/^}/s/if (!flash_write_admitted(offset, len, flash_write_end))/if (0)/'

# flash_program() reaches the boot ROM without the bound.
mutant-program-unguarded:
	${MUT} flash.c \
	    '/^flash_program(/,/^}/s/if (!flash_write_admitted(offset, len, flash_write_end))/if (0)/'

clean:
	rm -f loader_stamp_test

.PHONY: all check mutants mutant-bound-le mutant-offset-only \
	mutant-magic-ignored mutant-limit-accepted mutant-limit-unaligned \
	mutant-fail-open mutant-swap-extent mutant-erase-unguarded \
	mutant-program-unguarded clean
