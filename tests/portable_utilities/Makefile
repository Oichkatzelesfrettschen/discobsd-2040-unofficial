HOSTCC?= cc
CFLAGS= -std=gnu17 -Wall -Wextra -Werror
PROGRAMS= getopt_host yes_host strings_host users_host yes_error_test \
	fixture_writer strings_stream_test strings_output_error_test

check: ${PROGRAMS}
	sh ./check.sh
	./yes_error_test
	./strings_stream_test
	./strings_output_error_test

getopt_host: ../../usr.bin/getopt/getopt.c
	${HOSTCC} ${CFLAGS} -o $@ ../../usr.bin/getopt/getopt.c

yes_host: ../../usr.bin/yes/yes.c
	${HOSTCC} ${CFLAGS} -o $@ ../../usr.bin/yes/yes.c

strings_host: ../../usr.bin/strings/strings.c
	${HOSTCC} ${CFLAGS} -idirafter ../../sys -o $@ ../../usr.bin/strings/strings.c

strings_stream.o: ../../usr.bin/strings/strings.c
	${HOSTCC} ${CFLAGS} -idirafter ../../sys \
		-Dmain=discobsd_strings_main -Dread=test_read -Dwrite=test_write \
		-c -o $@ ../../usr.bin/strings/strings.c

strings_stream_test: strings_stream_test.c strings_stream.o
	${HOSTCC} ${CFLAGS} -o $@ strings_stream_test.c strings_stream.o

strings_output_error_test: strings_output_error_test.c strings_stream.o
	${HOSTCC} ${CFLAGS} -idirafter ../../sys -o $@ \
		strings_output_error_test.c strings_stream.o

# macOS marks struct utmp deprecated in <utmp.h>; users.c reads the
# board's utmp records, which is the point of the program, and
# fixture_writer.c writes the records the test feeds it.
UTMP_CFLAGS= -Wno-deprecated-declarations

users_host: ../../usr.bin/users/users.c
	${HOSTCC} ${CFLAGS} ${UTMP_CFLAGS} -o $@ ../../usr.bin/users/users.c

yes_error.o: ../../usr.bin/yes/yes.c
	${HOSTCC} ${CFLAGS} -Dmain=discobsd_yes_main -Dwrite=test_write \
		-c -o $@ ../../usr.bin/yes/yes.c

yes_error_test: yes_error_test.c yes_error.o
	${HOSTCC} ${CFLAGS} -o $@ yes_error_test.c yes_error.o

fixture_writer: fixture_writer.c
	${HOSTCC} ${CFLAGS} ${UTMP_CFLAGS} -idirafter ../../sys -o $@ \
	    fixture_writer.c

clean:
	rm -f ${PROGRAMS} strings_stream.o yes_error.o regular.bin raw.aout raw-truncated.aout \
		packed.aout users.utmp users-overflow.utmp

all: check

.PHONY: all check clean
