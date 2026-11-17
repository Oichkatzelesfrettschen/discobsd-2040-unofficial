# windowtest -- the user window admits a 170 KiB bss

windowtest checks that the RP2040 user window admits an image whose bss
exceeds 144 KiB and that every byte of a 170 KiB bss can be written and read
back. Exec refuses the image unless `USER_DATA_SIZE` covers its text, data,
bss and stack, and the pattern pass faults or reads back wrong if the window
is smaller than `machparam.h` states. It is a capacity and writability check:
it reads no MPU register and provokes no fault, so it establishes neither
protection nor the exact region boundary (`sys/arch/rp2040/doc/MPU.md` and
`mputest` own those).

It draws nothing and uses no panel, key or other device, so it runs from any
login, panel console or serial. It writes a position-dependent byte pattern
over the whole bss, reads it back and prints
`bss <n> bytes at <first>..<last>, <m> mismatches`, then `WINDOWTEST OK`
(exit 0) or `WINDOWTEST FAIL` (exit 1). A kernel with a smaller window refuses
the exec instead, which the shell reports as a failed exec with no verdict
line. `board_user_window.py` stages and runs it with the other window checks.
