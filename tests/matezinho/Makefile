TOPSRC!=cd ../..; pwd
PYTHON?=
HOST_CC?=cc

check:
	${HOST_CC} -std=c17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
	    -o core_test core_test.c ${TOPSRC}/usr.bin/matezinho/core.c \
	    ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c
	./core_test
	${PYTHON} check.py

check-qemu:
	${PYTHON} check.py --qemu

check-sd:
	${HOST_CC} -std=c17 -Wall -Wextra -Werror -fPIC -shared \
	    -o libsd_driver.so sd_driver.c ${TOPSRC}/sys/arch/rp2040/dev/sdspi.c
	LD_LIBRARY_PATH="${.CURDIR}:$${LD_LIBRARY_PATH:-}" \
	    dotnet run --project sdtest

clean:
	rm -f core_test libsd_driver.so
	rm -rf sdtest/bin sdtest/obj

.PHONY: check check-qemu clean
