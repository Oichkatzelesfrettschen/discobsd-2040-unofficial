/*
 * lib/libc/stdlib/getopt.c defines optreset and dc_main (usr.bin/dc/dc.c)
 * sets it to restart option parsing, which bc relies on when its child runs
 * dc_main after bc's own getopt loop. glibc has no optreset: the variable is
 * defined here and glibc restarts from optind = 1 on its own once an option
 * scan has finished.
 */
int optreset;
