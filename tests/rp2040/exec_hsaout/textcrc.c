/*
 * textcrc: on-device check that a process's text survives a swap. It
 * sums its own text from _start to etext, sleeps long enough for the
 * console's next commands to swap it out and back, sums again, and
 * prints "TEXTCRC OK" or "TEXTCRC BAD". Installed twice by the test
 * manifest, raw and packed, so the packed clean-text restoration in
 * swapin is what the packed copy exercises. Regression tool, not
 * shipped in the root manifest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char _start[], etext[];

static unsigned
sum (void)
{
    const unsigned char *p = (const unsigned char *) _start;
    unsigned crc = 0xffffffffU;
    int i;

    while (p < (const unsigned char *) etext) {
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
    }
    return ~crc;
}

int
main (int argc, char **argv)
{
    unsigned before, after;
    int secs = argc > 1 ? atoi (argv[1]) : 8;

    before = sum ();
    printf ("textcrc: text %u bytes, crc %08x, sleeping %d s\n",
        (unsigned) (etext - _start), before, secs);
    fflush (stdout);
    sleep (secs);
    after = sum ();
    printf ("textcrc: crc %08x after\n", after);
    printf (before == after ? "TEXTCRC OK\n" : "TEXTCRC BAD\n");
    return before != after;
}
