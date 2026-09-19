/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Concatenate files.
 */
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef CAT_TEST_BUFSIZ
#define CAT_BUFSIZ CAT_TEST_BUFSIZ
#else
#define CAT_BUFSIZ BUFSIZ
_Static_assert(BUFSIZ == MAXBSIZE,
    "cat transfer size must match the kernel block size");
_Static_assert(BUFSIZ == DEV_BSIZE,
    "cat transfer size must match the device block size");
#endif

enum cat_flag {
    CAT_NUMBER_NONBLANK = 1U << 0,
    CAT_SHOW_ENDS = 1U << 1,
    CAT_NUMBER = 1U << 2,
    CAT_SQUEEZE_BLANK = 1U << 3,
    CAT_SHOW_TABS = 1U << 4,
    CAT_UNBUFFERED = 1U << 5,
    CAT_SHOW_NONPRINTING = 1U << 6,
    CAT_SPACED = 1U << 7,
    CAT_IN_LINE = 1U << 8
};

struct cat_state {
    unsigned flags;
    int line_number;
};

static void copyopt(FILE *, struct cat_state *);
int fastcat(int);

int
main(int argc, char **argv)
{
    struct cat_state state = { 0, 1 };
    FILE *input;
    int character;
    int use_stdin = 0;
    int output_identity_valid = 0;
    int copy_status = 0;
    dev_t output_device = 0;
    ino_t output_inode = 0;
    struct stat status;
    int retval = 0;

    for( ; argc>1 && argv[1][0]=='-'; argc--,argv++) {
        switch(argv[1][1]) {
        case 0:
            break;
        case 'u':
            setbuf(stdout, (char *)NULL);
            state.flags |= CAT_UNBUFFERED;
            continue;
        case 'n':
            state.flags |= CAT_NUMBER;
            continue;
        case 'b':
            state.flags |= CAT_NUMBER_NONBLANK | CAT_NUMBER;
            continue;
        case 'v':
            state.flags |= CAT_SHOW_NONPRINTING;
            continue;
        case 's':
            state.flags |= CAT_SQUEEZE_BLANK;
            continue;
        case 'e':
            state.flags |= CAT_SHOW_ENDS | CAT_SHOW_NONPRINTING;
            continue;
        case 't':
            state.flags |= CAT_SHOW_TABS | CAT_SHOW_NONPRINTING;
            continue;
        }
        break;
    }
    if (fstat(fileno(stdout), &status) == 0) {
        if (!S_ISCHR(status.st_mode) && !S_ISBLK(status.st_mode)) {
            output_device = status.st_dev;
            output_inode = status.st_ino;
            output_identity_valid = 1;
        }
    }
    if (argc < 2) {
        argc = 2;
        use_stdin = 1;
    }
    while (--argc > 0) {
        if (use_stdin || ((*++argv)[0]=='-' && (*argv)[1]=='\0'))
            input = stdin;
        else {
            if ((input = fopen(*argv, "r")) == NULL) {
                perror(*argv);
                retval = 1;
                continue;
            }
        }
        if (output_identity_valid && fstat(fileno(input), &status) == 0) {
            if (S_ISREG(status.st_mode) &&
                status.st_dev == output_device &&
                status.st_ino == output_inode) {
                fprintf(stderr, "cat: input %s is output\n",
                   use_stdin ? "-" : *argv);
                fclose(input);
                retval = 1;
                continue;
            }
        }
        copy_status = 0;
        if (state.flags & (CAT_NUMBER | CAT_SQUEEZE_BLANK |
            CAT_SHOW_NONPRINTING))
            copyopt(input, &state);
        else if (state.flags & CAT_UNBUFFERED) {
            while ((character = getc(input)) != EOF)
                putchar(character);
        } else {
            copy_status = fastcat(fileno(input));
            if (copy_status != 0)
                retval = copy_status;
        }
        if (input != stdin)
            fclose(input);
        else
            clearerr(input);       /* reset sticky eof */
        if (copy_status == 2)
            break;
        if (ferror(stdout)) {
            fprintf(stderr, "cat: output write error\n");
            retval = 1;
            break;
        }
    }
    return retval;
}

static void
copyopt(FILE *input, struct cat_state *state)
{
    int character;

top:
    character = getc(input);
    if (character == EOF)
        return;
    if (character == '\n') {
        if (!(state->flags & CAT_IN_LINE)) {
            if ((state->flags & CAT_SQUEEZE_BLANK) &&
                (state->flags & CAT_SPACED))
                goto top;
            state->flags |= CAT_SPACED;
        }
        if ((state->flags & CAT_NUMBER) &&
            !(state->flags & CAT_NUMBER_NONBLANK) &&
            !(state->flags & CAT_IN_LINE))
            printf("%6d\t", state->line_number++);
        if (state->flags & CAT_SHOW_ENDS)
            putchar('$');
        putchar('\n');
        state->flags &= ~CAT_IN_LINE;
        goto top;
    }
    if ((state->flags & CAT_NUMBER) && !(state->flags & CAT_IN_LINE))
        printf("%6d\t", state->line_number++);
    state->flags |= CAT_IN_LINE;
    if (state->flags & CAT_SHOW_NONPRINTING) {
        if (!(state->flags & CAT_SHOW_TABS) && character == '\t')
            putchar(character);
        else {
            if (character > 0177) {
                printf("M-");
                character &= 0177;
            }
            if (character < ' ')
                printf("^%c", character + '@');
            else if (character == 0177)
                printf("^?");
            else
                putchar(character);
        }
    } else
        putchar(character);
    state->flags &= ~CAT_SPACED;
    goto top;
}

int
fastcat(int input_fd)
{
    /*
     * sys_inode.c reports MAXBSIZE and each target defines DEV_BSIZE as
     * BUFSIZ.  One automatic buffer avoids allocator state and applet BSS.
     */
    char buffer[CAT_BUFSIZ];
    size_t offset;
    size_t remaining;
    ssize_t bytes_read;
    ssize_t bytes_written;
    int output_fd;

    output_fd = fileno(stdout);
    if (output_fd < 0) {
        perror("cat: write error");
        return 2;
    }

    /* A pipe may accept only part of a block, so each read drains fully. */
    while ((bytes_read = read(input_fd, buffer, sizeof(buffer))) > 0) {
        offset = 0;
        remaining = (size_t)bytes_read;
        do {
            bytes_written = write(output_fd, buffer + offset, remaining);
            if (bytes_written <= 0) {
                if (bytes_written == 0)
                    errno = EIO;
                perror("cat: write error");
                return 2;
            }
            offset += (size_t)bytes_written;
            remaining -= (size_t)bytes_written;
        } while (remaining != 0);
    }

    if (bytes_read < 0) {
        perror("cat: read error");
        return 1;
    }
    return 0;
}
