/*
 * Interactive line editor for bin/sh: raw-mode cursor editing, a
 * bounded command-history ring, and PATH/filename tab completion.
 * Self-contained: takes its inputs as parameters, touches no shell
 * global state, and is compiled both into sh and into the host test
 * harness (tests/edit_test.c) unchanged.
 */
#ifndef SH_EDIT_H
#define SH_EDIT_H

/*
 * Read one interactively-edited command line from fdin, echoing and
 * redrawing on fdout, prompting with `prompt'.  `path' is a
 * colon-separated PATH-style string used for first-word (command
 * name) tab completion.  The finished line, without a trailing
 * newline, is written into buf (up to bufsz-1 bytes, NUL-terminated
 * defensively but the return value is authoritative).  Returns the
 * line length, or -1 if the line was terminated by EOF (Ctrl-D) on
 * an empty line.
 */
int editline(int fdin, int fdout, const char *ps1, const char *path,
    char *buf, int bufsz);

#endif /* SH_EDIT_H */
