TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# init.c compiles from the tree against the tree's headers with its system
# calls renamed onto the doubles in init_fork_test.c. init.c is held at
# WARNLEVEL=legacy (sbin/init/Makefile) and keeps K&R definitions, so it
# builds at -Wall -Werror with clang's deprecated-non-prototype diagnostic
# off; the test itself builds at -Wall -Wextra -Werror.
HOSTCC?= cc
INIT_SRC= ${TOPSRC}/sbin/init/init.c
DAEMONS_SRC= ${TOPSRC}/sbin/init/daemons.c
TREE_INCLUDES= -nostdinc -isystem ${TOPSRC}/include
CALLS= fork wait sleep kill syslog openlog closelog open creat close dup2 \
	execl execve signal sigvec sigblock sigsetmask sigpause alarm sysctl \
	setpgrp errno
RENAMES= ${CALLS:@c@-D${c}=test_${c}@} -Dmain=init_program_main
INIT_CFLAGS= -I${TOPSRC}/sbin/init -std=gnu17 -Wall -Werror -Wno-deprecated-non-prototype \
	-ffunction-sections -fdata-sections \
	${TREE_INCLUDES} ${RENAMES}
TEST_CFLAGS= -std=gnu17 -Wall -Wextra -Werror ${TREE_INCLUDES} -I${TOPSRC}/sbin/init
GC_SECTIONS!= if [ "$$(uname -s)" = Darwin ] ; then echo -Wl,-dead_strip ; \
	else echo -Wl,--gc-sections ; fi

# Each mutation restores one defect the gate exists to reject and must fail
# a named check. mutate.sh requires its pattern to match exactly once.
MUTANTS= term-kills-marker wterm-kills-marker spawn-no-delay \
	runcom-waits-after-refusal daemon-refusal-no-backoff \
	alarm-handler-schedules alarm-handler-no-rearm console-joins-tty \
	console-switch-unblocked

check: init_fork_test ${MUTANTS:@m@init_${m}.test@}
	./init_fork_test
	@set -eu; for m in ${MUTANTS}; do \
		if ./init_$$m.test >init_$$m.out 2>&1; then \
			cat init_$$m.out >&2; \
			echo "FAIL init fork refusal: $$m mutation passed" >&2; \
			exit 1; \
		fi; \
		if ! grep -q '^FAIL ' init_$$m.out; then \
			cat init_$$m.out >&2; \
			echo "FAIL init fork refusal: $$m failed without a check" >&2; \
			exit 1; \
		fi; \
		echo "PASS init fork refusal $$m mutation rejected"; \
	done

init_fork_test: init_fork_test.c init.o daemons.o
	${HOSTCC} ${TEST_CFLAGS} ${GC_SECTIONS} -o $@ init_fork_test.c init.o daemons.o

daemons.o: ${DAEMONS_SRC}
	${HOSTCC} ${TEST_CFLAGS} -c -o $@ ${DAEMONS_SRC}

init.o: ${INIT_SRC}
	${HOSTCC} ${INIT_CFLAGS} -c -o $@ ${INIT_SRC}

init_term-kills-marker.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ 'if (p->pid > 0) {' 'if (p->pid != 0) {'

init_wterm-kills-marker.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ 'if (p->wpid > 0)\n\t\tkill(p->wpid, SIGKILL);' \
	    'if (p->wpid != 0)\n\t\tkill(p->wpid, SIGKILL);'

init_spawn-no-delay.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ 'sleep(SPAWN_DELAY);' ';'

init_runcom-waits-after-refusal.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ 'if (pid < 0)\n\t\treturn (0);\n\tif (pid == 0) {\n\t\tf = open' \
	    'if (pid == 0) {\n\t\tf = open'

init_daemon-refusal-no-backoff.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ 'dfailed(d, -1);\n\t\treturn;' 'return;'

init_alarm-handler-schedules.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ '\tdtimer = 1;\n\talarm(1);\n}' '\tdtimer = 1;\n\talarm(1);\n\tdschedule();\n}'

init_alarm-handler-no-rearm.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ '\tdtimer = 1;\n\talarm(1);\n}' '\tdtimer = 1;\n}'

init_console-joins-tty.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ '\tsetpgrp(0, 1);\n\tf = open(ctty, O_WRONLY);\n\tsetpgrp(0, 0);' '\tf = open(ctty, O_WRONLY);'

init_console-switch-unblocked.c: ${INIT_SRC} mutate.sh
	sh ./mutate.sh ${INIT_SRC} $@ '\tomask = sigblock(sigmask(SIGHUP) | sigmask(SIGTERM) | sigmask(SIGTSTP));\n\tsetpgrp' '\tomask = 0;\n\tsetpgrp'

.for m in ${MUTANTS}
init_${m}.test: init_fork_test.c init_${m}.c daemons.o
	${HOSTCC} ${INIT_CFLAGS} -c -o init_${m}.o init_${m}.c
	${HOSTCC} ${TEST_CFLAGS} ${GC_SECTIONS} -o $@ init_fork_test.c init_${m}.o daemons.o
.endfor

clean:
	rm -f init_fork_test init.o daemons.o init_*-*.c init_*-*.o init_*-*.test init_*-*.out

.PHONY: check clean
