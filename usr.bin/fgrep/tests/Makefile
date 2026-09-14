HOST_CC?=      cc
HOST_CFLAGS?= -O2 -Wall -Wextra -Werror

CAPACITY_PROG= fgrep_capacity_test
HOST_PROG=     fgrep_host
PROGS=         ${CAPACITY_PROG} ${HOST_PROG}

check:         ${PROGS}
		./${CAPACITY_PROG}
		sh ./functional_test.sh ./${HOST_PROG}

${CAPACITY_PROG}: capacity_test.c ../fgrep.c
		${HOST_CC} ${HOST_CFLAGS} \
		    -Wno-deprecated-non-prototype -Wno-strict-prototypes \
		    -Wno-old-style-definition -o $@ capacity_test.c

${HOST_PROG}:  ../fgrep.c
		${HOST_CC} ${HOST_CFLAGS} \
		    -Wno-deprecated-non-prototype -Wno-strict-prototypes \
		    -Wno-old-style-definition -o $@ ../fgrep.c

clean:
		rm -f ${PROGS}

.PHONY:        check clean
