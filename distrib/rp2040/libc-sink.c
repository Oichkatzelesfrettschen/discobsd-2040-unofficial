/*
 * Reference program for distrib/rp2040/Makefile.inc's closure extraction.
 *
 * This file calls no useful computation; it only takes the address of, or
 * calls, every libc entry point a small board program is expected to need:
 * stdio without floating-point conversion, the string and ctype families,
 * malloc, the common syscalls, signal, exec, wait, stat, time, errno and
 * perror, qsort, the strtol family, and getenv. Linking it against the
 * full lib/libc.a and reading the linker map (see the sink: rule) names
 * the exact set of .o members the reduced board archive must carry.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>

/*
 * Every call below feeds sink so a compiler that recognizes a libc name as
 * a built-in function (strcmp, memcmp, and the like, whose result this
 * file never otherwise inspects) cannot fold or delete it: -fno-builtin
 * on the sink's own compile (see the sink: rule) blocks the substitution
 * outright, and sink gives the optimizer a second, independent reason to
 * keep the call.
 */
volatile long sink;

static void
touch_stdio(void)
{
	FILE *f = fopen("/tmp/x", "r");
	char buf[64];
	char terminal_name[L_ctermid];

	sink += (long)ctermid(terminal_name);
	sink += (long)fgets(buf, sizeof(buf), f);
	sink += fputs(buf, stdout);
	sink += fputc('x', stdout);
	sink += fgetc(f);
	sink += puts(buf);
	sink += printf("%s %d %u %x %c\n", buf, 1, 2u, 3u, 'a');
	sink += fprintf(stderr, "%s\n", buf);
	sink += sprintf(buf, "%d", 42);
	sink += snprintf(buf, sizeof(buf), "%d", 42);
	sink += sscanf(buf, "%d", &errno);
	sink += fclose(f);
	sink += remove("/tmp/x");
	rewind(f);
	sink += fseek(f, 0, SEEK_SET);
	sink += ftell(f);
	sink += feof(f);
	sink += ferror(f);
	sink += fflush(f);
}

static void
touch_string_ctype(void)
{
	char a[16] = "hello", b[16], *dup;

	sink += (long)strcpy(b, a);
	sink += (long)strncpy(b, a, sizeof(b) - 1);
	b[sizeof(b) - 1] = '\0';
	sink += (long)strcat(b, "");
	sink += (long)strncat(b, "", 0);
	sink += strlen(a);
	sink += strcmp(a, b);
	sink += strncmp(a, b, 4);
	sink += strcasecmp(a, b);
	sink += (long)strchr(a, 'e');
	sink += (long)strrchr(a, 'e');
	sink += (long)strstr(a, "ll");
	sink += (long)strtok(b, " ");
	dup = strdup(a);
	sink += (long)dup;
	free(dup);
	sink += (long)memcpy(b, a, sizeof(a));
	sink += (long)memmove(b, a, sizeof(a));
	sink += (long)memset(b, 0, sizeof(b));
	sink += memcmp(a, b, sizeof(a));
	sink += timingsafe_bcmp(a, b, sizeof(a));
	explicit_bzero(b, sizeof(b));
	sink += isalpha((unsigned char)a[0]);
	sink += isdigit((unsigned char)a[0]);
	sink += isspace((unsigned char)a[0]);
	sink += toupper((unsigned char)a[0]);
	sink += tolower((unsigned char)a[0]);
}

static void
touch_malloc(void)
{
	void *p = malloc(16);

	p = realloc(p, 32);
	sink += (long)p;
	free(p);
	p = calloc(4, 4);
	sink += (long)p;
	free(p);
}

static void
touch_vis(void)
{
	char destination[16];
	const char source[] = {'A', '\0', '7'};

	sink += (long)vis(destination, '\n', VIS_CSTYLE, '\0');
	sink += (long)nvis(destination, sizeof(destination), '\n', VIS_CSTYLE,
	    '\0');
	sink += strvis(destination, "A", 0);
	sink += strnvis(destination, sizeof(destination), "A", 0);
	sink += strvisx(destination, source, sizeof(source), VIS_CSTYLE);
	sink += strnvisx(destination, sizeof(destination), source, sizeof(source),
	    VIS_CSTYLE);
}

static int
cmp(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

static void
touch_stdlib(void)
{
	int v[4] = { 4, 3, 2, 1 };

	qsort(v, 4, sizeof(v[0]), cmp);
	sink += v[0];
	sink += strtol("42", NULL, 10);
	sink += strtoul("42", NULL, 10);
	sink += atoi("42");
	sink += atol("42");
	sink += (long)getenv("PATH");
}

static void
touch_syscalls(void)
{
	struct stat st;
	pid_t pid;
	int status;

	sink += stat("/tmp", &st);
	sink += fstat(0, &st);
	sink += open("/tmp/x", O_RDONLY);
	sink += read(0, NULL, 0);
	sink += write(1, "", 0);
	sink += close(0);
	sink += lseek(0, 0, SEEK_SET);
	sink += unlink("/tmp/x");
	pid = fork();
	if (pid == 0)
		sink += execl("/bin/sh", "sh", (char *)NULL);
	sink += waitpid(pid, &status, 0);
	sink += wait(&status);
	sink += getpid();
	sink += kill(pid, SIGTERM);
	sink += raise(0);
	sink += (long)signal(SIGINT, SIG_IGN);
	sink += time(NULL);
	sink += sleep(0);
}

static jmp_buf jb;
static sigjmp_buf sjb;

/*
 * setjmp, _setjmp and sigsetjmp with their longjmp partners: an emulator or
 * interpreter conveys a trap out of a deep call chain through them, so the
 * board libc carries all three variants.
 */
static void
touch_jmp(void)
{
	if (setjmp(jb) == 0)
		longjmp(jb, 1);
	if (_setjmp(jb) == 0)
		_longjmp(jb, 1);
	if (sigsetjmp(sjb, 1) == 0)
		siglongjmp(sjb, 1);
}

int
main(int argc, char **argv)
{
	touch_stdio();
	touch_string_ctype();
	touch_malloc();
	touch_vis();
	touch_stdlib();
	touch_syscalls();
	touch_jmp();
	if (argc > 1)
		perror(argv[1]);
	exit(0);
	return 0;
}
