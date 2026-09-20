CC?=cc
CFLAGS?=-std=c17 -Wall -Wextra -Werror

check:
	@${CC} ${CFLAGS} -c control.c -o /dev/null
	@if ${CC} ${CFLAGS} -c legacy.c -o /dev/null >/dev/null 2>&1; then \
		echo "legacy control-character macros passed" >&2; \
		exit 1; \
	fi
	@echo "control-character macro calibration passed"

.PHONY: check
