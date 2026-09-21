/*
 * The Makefile renames exit() so the gate can read the status rather than
 * lose the process, and the rename takes the name gcc knows does not return
 * with it. Declaring the replacement noreturn keeps -Wreturn-type live over
 * the program's own code, where main() ends in exit() and returns nothing.
 */
void	umount_exit(int) __attribute__((noreturn));

/*
 * The output calls the Makefile renames beside exit(); the gate defines them.
 * FILE is the tree's, which include/stdio.h declares before this is reached.
 */
int	umount_printf(const char *, ...);
int	umount_fprintf(FILE *, const char *, ...);
