TOP= ../../..
FLASH_SWAP= ${TOP}/sys/arch/rp2040/dev/flash_swap.c

CC?= cc
CFLAGS+= -std=c11 -O2 -Wall -Wextra -Werror -I${TOP}/sys/arch

check: flash_swap_test
	./flash_swap_test

flash_swap_test: flash_swap_test.c ${FLASH_SWAP}
	${CC} ${CFLAGS} -o $@ flash_swap_test.c ${FLASH_SWAP}

clean:
	rm -f flash_swap_test

.PHONY: check clean
