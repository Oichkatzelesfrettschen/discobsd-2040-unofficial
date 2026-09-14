/*
 * hsaout -c raw packed      pack a raw OMAGIC a.out
 * hsaout -d packed raw      unpack a container into a raw a.out
 * hsaout -t packed ...      check containers, printing one line each
 * hsaout -s raw ...         bytes and root blocks raw against packed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libhsaout.h"

static unsigned char *
slurp (const char *name, size_t *len)
{
    FILE *f = fopen (name, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0, got;

    if (f == NULL) {
        perror (name);
        return NULL;
    }
    do {
        if (n == cap) {
            cap = cap ? cap * 2 : 65536;
            buf = realloc (buf, cap);
            if (buf == NULL) {
                fprintf (stderr, "%s: out of memory\n", name);
                fclose (f);
                return NULL;
            }
        }
        got = fread (buf + n, 1, cap - n, f);
        n += got;
    } while (got != 0);
    if (ferror (f)) {
        perror (name);
        free (buf);
        fclose (f);
        return NULL;
    }
    fclose (f);
    *len = n;
    return buf;
}

static int
spit (const char *name, const unsigned char *buf, size_t len)
{
    FILE *f = fopen (name, "wb");

    if (f == NULL || fwrite (buf, 1, len, f) != len || fclose (f) != 0) {
        perror (name);
        return 1;
    }
    return 0;
}

static int
usage (void)
{
    fprintf (stderr, "usage: hsaout -c raw packed | -d packed raw | -t packed... | -s raw...\n");
    return 2;
}

int
main (int argc, char **argv)
{
    unsigned char *in, *out;
    size_t inlen, outlen;
    const char *err;
    int i, rc = 0;

    if (argc < 3 || argv[1][0] != '-' || argv[1][2] != '\0')
        return usage ();
    switch (argv[1][1]) {
    case 'c':
    case 'd':
        if (argc != 4)
            return usage ();
        if ((in = slurp (argv[2], &inlen)) == NULL)
            return 1;
        out = argv[1][1] == 'c' ? hsaout_pack (in, inlen, &outlen, &err) :
            hsaout_unpack (in, inlen, &outlen, &err);
        if (out == NULL) {
            fprintf (stderr, "%s: %s\n", argv[2], err);
            return 1;
        }
        rc = spit (argv[3], out, outlen);
        free (in);
        free (out);
        return rc;
    case 't':
        for (i = 2; i < argc; i++) {
            if ((in = slurp (argv[i], &inlen)) == NULL) {
                rc = 1;
                continue;
            }
            out = hsaout_unpack (in, inlen, &outlen, &err);
            if (out == NULL) {
                printf ("%s: %s\n", argv[i], err);
                rc = 1;
            } else
                printf ("%s: ok, %zu packed bytes expand to %zu\n", argv[i],
                    inlen, outlen);
            free (in);
            free (out);
        }
        return rc;
    case 's':
        printf ("%-24s %8s %8s %6s %6s\n", "file", "raw", "packed", "rblk",
            "pblk");
        for (i = 2; i < argc; i++) {
            if ((in = slurp (argv[i], &inlen)) == NULL) {
                rc = 1;
                continue;
            }
            out = hsaout_pack (in, inlen, &outlen, &err);
            if (out == NULL) {
                printf ("%-24s %8zu %s\n", argv[i], inlen, err);
                rc = 1;
            } else
                printf ("%-24s %8zu %8zu %6lu %6lu\n", argv[i], inlen, outlen,
                    hsaout_blocks (inlen), hsaout_blocks (outlen));
            free (in);
            free (out);
        }
        return rc;
    default:
        return usage ();
    }
}
