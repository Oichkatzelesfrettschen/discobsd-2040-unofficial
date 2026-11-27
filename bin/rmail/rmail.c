/*
**  RMAIL -- UUCP mail server.
**
**  This program reads the >From ... remote from ... lines that
**  UUCP is so fond of and turns them into something reasonable.
**  It calls sendmail giving it a -f option built from these
**  lines.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <paths.h>

typedef char    bool;
#define TRUE    1
#define FALSE   0

bool    Debug;

static bool
append_char(char **dst, char *end, int c)
{
    if (*dst + 1 >= end)
        return FALSE;
    *(*dst)++ = (char)c;
    **dst = '\0';
    return TRUE;
}

static bool
append_text(char **dst, char *end, const char *text)
{
    while (*text != '\0')
        if (!append_char(dst, end, (unsigned char)*text++))
            return FALSE;
    return TRUE;
}

/* Append one shell word, including the single-quote escape sequence. */
static bool
append_quoted(char **dst, char *end, const char *text, size_t length)
{
    size_t i;

    if (!append_char(dst, end, '\''))
        return FALSE;
    for (i = 0; i < length; i++) {
        if (text[i] == '\'') {
            if (!append_text(dst, end, "'\\''"))
                return FALSE;
        } else if (!append_char(dst, end, (unsigned char)text[i]))
            return FALSE;
    }
    return append_char(dst, end, '\'');
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    FILE *out;  /* output to sendmail */
    char lbuf[1024];    /* one line of the message */
    char from[512];     /* accumulated path of sender */
    char ufrom[512];    /* user on remote system */
    char sys[512];      /* a system in path */
    char junk[1024];    /* scratchpad */
    char cmd[2000];
    char *cmdp;
    register char *cp;
    register char *uf = ufrom;  /* ptr into ufrom */
    size_t fromlen;
    int i;

#ifdef DEBUG
    if (argc > 1 && strcmp(argv[1], "-T") == 0)
    {
        Debug = TRUE;
        argc--;
        argv++;
    }
#endif
    if (argc < 2)
    {
        fprintf(stderr, "Usage: rmail user ...\n");
        exit(EX_USAGE);
    }

    from[0] = '\0';
    fromlen = 0;
    (void) strcpy(ufrom, "/dev/null");

    for (;;)
    {
        if (fgets(lbuf, sizeof lbuf, stdin) == NULL) {
            lbuf[0] = '\0';
            break;
        }
        if (strncmp(lbuf, "From ", 5) != 0 && strncmp(lbuf, ">From ", 6) != 0)
            break;
        if (sscanf(lbuf, "%1023s %511s", junk, ufrom) != 2) {
            fprintf(stderr, "rmail: malformed From line\n");
            exit(EX_DATAERR);
        }
        cp = lbuf;
        for (;;)
        {
            cp = index(cp+1, 'r');
            if (cp == NULL)
            {
                register char *p = rindex(uf, '!');

                if (p != NULL)
                {
                    *p = '\0';
                    (void) strcpy(sys, uf);
                    uf = p + 1;
                    break;
                }
                cp = "remote from somewhere";
            }
#ifdef DEBUG
            if (Debug)
                printf("cp='%s'\n", cp);
#endif
            if (strncmp(cp, "remote from ", 12)==0)
                break;
        }
        if (cp != NULL)
            if (sscanf(cp, "remote from %511s", sys) != 1) {
                fprintf(stderr, "rmail: malformed remote system\n");
                exit(EX_DATAERR);
            }
        if (strlen(sys) + 1 >= sizeof(from) - fromlen) {
            fprintf(stderr, "rmail: sender path too long\n");
            exit(EX_DATAERR);
        }
        (void) strcpy(from + fromlen, sys);
        fromlen += strlen(sys);
        from[fromlen++] = '!';
        from[fromlen] = '\0';
#ifdef DEBUG
        if (Debug)
            printf("ufrom='%s', sys='%s', from now '%s'\n", uf, sys, from);
#endif
    }
    if (strlen(uf) >= sizeof(from) - fromlen) {
        fprintf(stderr, "rmail: sender path too long\n");
        exit(EX_DATAERR);
    }
    (void) strcpy(from + fromlen, uf);

    cmd[0] = '\0';
    cmdp = cmd;
    if (!append_text(&cmdp, cmd + sizeof(cmd), _PATH_SENDMAIL " -ee -f") ||
        !append_quoted(&cmdp, cmd + sizeof(cmd), from, strlen(from)) ||
        !append_text(&cmdp, cmd + sizeof(cmd), " -i")) {
        fprintf(stderr, "rmail: command too long\n");
        exit(EX_DATAERR);
    }
    while (*++argv != NULL)
    {
        const char *recipient = *argv;
        size_t length = strlen(recipient);

        if (length >= 2 && recipient[0] == '(' && recipient[length - 1] == ')') {
            recipient++;
            length -= 2;
        }
        if (length == 0 || recipient[0] == '-') {
            fprintf(stderr, "rmail: invalid recipient\n");
            exit(EX_USAGE);
        }
        if (!append_char(&cmdp, cmd + sizeof(cmd), ' ') ||
            !append_quoted(&cmdp, cmd + sizeof(cmd), recipient, length)) {
            fprintf(stderr, "rmail: command too long\n");
            exit(EX_DATAERR);
        }
    }
#ifdef DEBUG
    if (Debug)
        printf("cmd='%s'\n", cmd);
#endif
    out = popen(cmd, "w");
    if (out == NULL) {
        perror("rmail: popen");
        exit(EX_OSERR);
    }
    if (lbuf[0] != '\0')
        fputs(lbuf, out);
    while (fgets(lbuf, sizeof lbuf, stdin))
        fputs(lbuf, out);
    i = pclose(out);
    if ((i & 0377) != 0)
    {
        fprintf(stderr, "pclose: status 0%o\n", i);
        exit(EX_OSERR);
    }

    exit((i >> 8) & 0377);
}
