#
# Host gate for the PicoCalc loader's flash limit. loader_stamp_test.c runs
# the stamp decode and the erase/program bound of
# sys/arch/rp2040/dev/loader_stamp.h, unchanged, through exhaustive sweeps of
# offsets and lengths at the 4 KB erase and 256-byte program granularities,
# the edges at the limit, every limit the decode accepts, the limits it must
# refuse, the boot layout loader_layout_decode() hands flash_limit_init(),
# and the raw swap writer of flash_swap.c over a whole-chip NOR
# model. check_choke.sh requires flash.c's erase and program primitives to
# be the only boot ROM writers and to apply the bound first, flash_limit_init()
# to store the decoded end and the swap extent from it and to restore the XIP
# divider under the stamp magic, flash_enter_xip() to restore it after the
# loader's second stage, and that divider and sample delay to equal the ones
# boot2/boot2_w25q080.S programs.
#
# Once the shipped sources pass, each mutant below must fail: the bound
# with <= in place of <, a bound on the offset alone, the stamp magic
# ignored, an invalid limit accepted, an unaligned limit accepted, a limit
# that cuts the root accepted, a limit inside the loader's 16 KB accepted, a
# fail-open fallback, a swap extent that ignores the limit, a swap extent
# without its cap, either primitive without its bound, a writable end set to
# the whole chip at init, a swap extent not taken from the decoded layout,
# the layout never decoded at init, a layout that keeps the full swap region,
# restores the loader's second stage only for a valid limit or reports every
# stamp valid, the divider restore dropped at init or after an XIP re-entry, and a restored
# divider or sample delay that differs from boot2's. A gate that passed them
# would be checking nothing.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
ARCH=		${TOPSRC}/sys/arch
CFLAGS=		-std=gnu17 -O1 -Wall -Wextra -Werror

all: check

check: loader_stamp_test
	./loader_stamp_test
	sh ./check_choke.sh ${DEV}/flash.c ${ARCH}/rp2040/boot2/boot2_w25q080.S
	${MAKE} mutants

loader_stamp_test: loader_stamp_test.c ${DEV}/loader_stamp.h ${DEV}/flash.h \
    ${DEV}/flash_swap.h ${DEV}/flash_swap.c
	${HOST_CC} ${CFLAGS} -I${ARCH} -o $@ loader_stamp_test.c \
	    ${DEV}/flash_swap.c

MUT=		@sh ./mutant.sh "${HOST_CC}" "${DEV}"

mutants: mutant-bound-le mutant-offset-only mutant-magic-ignored \
	mutant-limit-accepted mutant-limit-unaligned mutant-limit-cuts-root \
	mutant-limit-in-loader mutant-fail-open mutant-swap-extent \
	mutant-swap-cap mutant-erase-unguarded mutant-program-unguarded \
	mutant-init-whole-chip mutant-init-swap mutant-init-no-decode \
	mutant-layout-swap-whole mutant-layout-boot2-valid-only \
	mutant-layout-status-valid mutant-init-no-restore \
	mutant-enter-no-restore mutant-clkdiv mutant-sample-dly

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
	    's/if (limit >= FLASH_XIP_BASE \&\& offset >= FLASH_SWAP_OFFSET \&\&/if (1 || limit >= FLASH_XIP_BASE \&\& offset >= FLASH_SWAP_OFFSET \&\&/'

# A limit inside an erase sector is accepted.
mutant-limit-unaligned:
	${MUT} loader_stamp.h \
	    's/(offset \& (FLASH_SECTOR_BYTES - 1)) == 0)/1)/'

# A limit below the swap region, which cuts the root, is accepted.
mutant-limit-cuts-root:
	${MUT} loader_stamp.h \
	    's/offset >= FLASH_SWAP_OFFSET \&\&/offset > FLASH_FS_OFFSET \&\&/'

# A limit inside the loader's 16 KB stage is accepted.
mutant-limit-in-loader:
	${MUT} loader_stamp.h \
	    's/offset <= FLASH_TOTAL_BYTES - LOADER_RESERVED_BYTES \&\&/offset <= FLASH_TOTAL_BYTES \&\&/'

# A bad limit leaves the whole chip writable.
mutant-fail-open:
	${MUT} loader_stamp.h \
	    's/\*write_end = FLASH_FS_OFFSET;/*write_end = FLASH_TOTAL_BYTES;/'

# fl1 keeps its full extent under the stamp.
mutant-swap-extent:
	${MUT} loader_stamp.h \
	    's/return write_end - FLASH_SWAP_OFFSET;/return FLASH_SWAP_BYTES;/'

# The swap extent is not capped at the region's full size.
mutant-swap-cap:
	${MUT} loader_stamp.h \
	    's/if (write_end - FLASH_SWAP_OFFSET > FLASH_SWAP_BYTES)/if (0)/'

# flash_erase() reaches the boot ROM without the bound.
mutant-erase-unguarded:
	${MUT} flash.c \
	    '/^flash_erase(/,/^}/s/if (!flash_write_admitted(offset, len, flash_write_end))/if (0)/'

# flash_program() reaches the boot ROM without the bound.
mutant-program-unguarded:
	${MUT} flash.c \
	    '/^flash_program(/,/^}/s/if (!flash_write_admitted(offset, len, flash_write_end))/if (0)/'

# flash_limit_init() leaves the whole chip writable whatever the stamp.
mutant-init-whole-chip:
	${MUT} flash.c \
	    '/^flash_limit_init(/,/^}/s/flash_write_end = ll.write_end;/flash_write_end = FLASH_TOTAL_BYTES;/'

# fl1 keeps its full size whatever the stamp.
mutant-init-swap:
	${MUT} flash.c \
	    's/flswap_bytes = ll.swap_bytes;/flswap_bytes = FLASH_SWAP_BYTES;/'

# The stamp words are never decoded at init.
mutant-init-no-decode:
	${MUT} flash.c '/^flash_limit_init(/,/^}/s/loader_layout_decode(magic, limit, &ll);//'

# The layout keeps the full swap region under the stamp.
mutant-layout-swap-whole:
	${MUT} loader_stamp.h \
	    's/ll->swap_bytes = flash_swap_extent(ll->write_end);/ll->swap_bytes = FLASH_SWAP_BYTES;/'

# A stamp with a bad limit leaves the loader's second stage unrestored.
mutant-layout-boot2-valid-only:
	${MUT} loader_stamp.h \
	    's/ll->loader_boot2 = magic == LOADER_STAMP_MAGIC;/ll->loader_boot2 = ll->status == LOADER_STAMP_VALID;/'

# The layout reports every stamp as valid, so a bad limit prints nothing.
mutant-layout-status-valid:
	${MUT} loader_stamp.h \
	    's/ll->status = loader_stamp_decode(/ll->status = LOADER_STAMP_VALID + 0 * loader_stamp_decode(/'

# The boot-time divider restore under the stamp is dropped.
mutant-init-no-restore:
	${MUT} flash.c '/^flash_limit_init(/,/^}/s/flash_ssi_restore();//'

# The divider restore after an XIP re-entry is dropped.
mutant-enter-no-restore:
	${MUT} flash.c '/^flash_enter_xip(/,/^}/s/flash_ssi_restore();//'

# The restored divider is the loader's 4, not boot2's 2.
mutant-clkdiv:
	${MUT} flash.c 's/^\(#define[[:space:]]*SSI_XIP_CLKDIV[[:space:]]*\)2$$/\14/'

# The restored sample delay differs from boot2's.
mutant-sample-dly:
	${MUT} flash.c 's/^\(#define[[:space:]]*SSI_XIP_SAMPLE_DLY[[:space:]]*\)1$$/\10/'

clean:
	rm -f loader_stamp_test

.PHONY: all check mutants mutant-bound-le mutant-offset-only \
	mutant-magic-ignored mutant-limit-accepted mutant-limit-unaligned \
	mutant-limit-cuts-root mutant-limit-in-loader mutant-fail-open \
	mutant-swap-extent mutant-swap-cap mutant-erase-unguarded \
	mutant-program-unguarded mutant-init-whole-chip mutant-init-swap \
	mutant-init-no-decode mutant-layout-swap-whole \
	mutant-layout-boot2-valid-only mutant-layout-status-valid \
	mutant-init-no-restore mutant-enter-no-restore mutant-clkdiv \
	mutant-sample-dly clean
