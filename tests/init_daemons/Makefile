TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# daemons.c makes no system call, so it compiles from the tree against the
# tree's headers and links with the host's C library unchanged.
HOSTCC?= cc
DAEMONS_SRC= ${TOPSRC}/sbin/init/daemons.c
CFLAGS_TREE= -std=gnu17 -Wall -Wextra -Werror -nostdinc \
	-isystem ${TOPSRC}/include -I${TOPSRC}/sbin/init

# Each mutation restores one defect the gate exists to reject and must fail
# a named check. ../init_contracts/mutate.sh requires its pattern to match
# exactly once.
MUTATE= sh ${TOPSRC}/tests/init_contracts/mutate.sh
MUTANTS= backoff-uncapped healthy-reset-missing capped-never-disables \
	id-range-unchecked command-truncated

check: init_daemons_test ${MUTANTS:@m@daemons_${m}.test@}
	./init_daemons_test
	@set -eu; for m in ${MUTANTS}; do \
		if ./daemons_$$m.test >daemons_$$m.out 2>&1; then \
			cat daemons_$$m.out >&2; \
			echo "FAIL init daemon table: $$m mutation passed" >&2; \
			exit 1; \
		fi; \
		if ! grep -q '^FAIL ' daemons_$$m.out; then \
			cat daemons_$$m.out >&2; \
			echo "FAIL init daemon table: $$m failed without a check" >&2; \
			exit 1; \
		fi; \
		echo "PASS init daemon table $$m mutation rejected"; \
	done

init_daemons_test: init_daemons_test.c ${DAEMONS_SRC}
	${HOSTCC} ${CFLAGS_TREE} -o $@ init_daemons_test.c ${DAEMONS_SRC}

daemons_backoff-uncapped.c: ${DAEMONS_SRC}
	${MUTATE} ${DAEMONS_SRC} $@ 'if (delay > DBACKOFF_MAX)\n\t\t\tdelay = DBACKOFF_MAX;' ';'

daemons_healthy-reset-missing.c: ${DAEMONS_SRC}
	${MUTATE} ${DAEMONS_SRC} $@ 'd->fails = 0;\n\td->fails++;' 'd->fails++;'

daemons_capped-never-disables.c: ${DAEMONS_SRC}
	${MUTATE} ${DAEMONS_SRC} $@ 'if (d->fails > DCAPPED_TRIES) {' 'if (0) {'

daemons_id-range-unchecked.c: ${DAEMONS_SRC}
	${MUTATE} ${DAEMONS_SRC} $@ 'return (value > 65535 ? -1 : value);' 'return (value);'

daemons_command-truncated.c: ${DAEMONS_SRC}
	${MUTATE} ${DAEMONS_SRC} $@ 'if (out + blank + 1 >= DCMDSIZ)\n\t\t\treturn (-1);' \
	    'if (out + blank + 1 >= DCMDSIZ)\n\t\t\tbreak;'

.for m in ${MUTANTS}
daemons_${m}.test: init_daemons_test.c daemons_${m}.c
	${HOSTCC} ${CFLAGS_TREE} -o $@ init_daemons_test.c daemons_${m}.c
.endfor

clean:
	rm -f init_daemons_test daemons_*-*.c daemons_*-*.test daemons_*-*.out

.PHONY: check clean
