#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "state.h"

extern unsigned char __libc_state_start[], __libc_state_end[], _end[];
extern const unsigned char fixture_flash_start[], fixture_flash_end[];
extern const unsigned char fixture_init_start[], fixture_init_end[];

void fixture_set_r9(void *);
void *fixture_map_flash(void *, size_t);
int fixture_protect_flash(void *, size_t);
_Noreturn void fixture_exit(int);
int *shared_errno_address(void);
FILE *shared_stream_address(void);
char ***shared_environ_address(void);
int shared_initial_state(const char *);
int shared_callback(int, int (*)(int));
void _start(void);

#if APP_BSS > 0
unsigned char application_bss[APP_BSS];
#endif
#if APP_DATA
int *initialized_errno = &errno;
FILE *initialized_stream = &_iob[2];
char ***initialized_environ = &environ;
#endif

static int
callback(int value)
{
	if (shared_errno_address() != &errno || errno != value - 1)
		fixture_exit(21);
	errno = value;
	_iob[2]._cnt = value + 1;
	return value + 2;
}

void
_start(void)
{
	unsigned char *flash;
	size_t flash_size = (uintptr_t)fixture_flash_end - (uintptr_t)fixture_flash_start;
	size_t map_size = (flash_size + 4095u) & ~(size_t)4095u;
	size_t init_size = (uintptr_t)fixture_init_end - (uintptr_t)fixture_init_start;
	size_t index;

	flash = fixture_map_flash((void *)(uintptr_t)0x10020000, map_size);
	if ((uintptr_t)flash != 0x10020000)
		fixture_exit(10);
	for (index = 0; index < flash_size; ++index)
		/* mmap2 supplies this checked writable QEMU mapping. */
		/* NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference) */
		flash[index] = fixture_flash_start[index];
	if (fixture_protect_flash(flash, map_size) != 0)
		fixture_exit(11);
	for (index = 0; index < ABI_STATE_SIZE; ++index)
		__libc_state_start[index] = index < init_size ? fixture_init_start[index] : 0;
	for (index = 0; index < ABI_RELOCATION_COUNT; ++index) {
		uint32_t *word = (uint32_t *)(void *)(__libc_state_start + abi_relocations[index]);
		*word += (uint32_t)(uintptr_t)__libc_state_start;
	}
	fixture_set_r9(__libc_state_start + ABI_GOT_OFFSET);
	if (__libc_state_start + ABI_STATE_SIZE != __libc_state_end ||
	    &__libc_state_end[0] != &_end[0])
		fixture_exit(12);
	if (shared_errno_address() != &errno || shared_stream_address() != _iob ||
	    shared_environ_address() != &environ)
		fixture_exit(13);
#if APP_DATA
	if (initialized_errno != &errno || initialized_stream != &_iob[2] ||
	    initialized_environ != &environ)
		fixture_exit(14);
#endif
	if (!shared_initial_state((const char *)_end))
		fixture_exit(15);
	if (shared_callback(40, callback) != 126 || errno != 41 || _iob[2]._cnt != 42)
		fixture_exit(16);
	fixture_exit(0);
}
