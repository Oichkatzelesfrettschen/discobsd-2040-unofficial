/*
 * Host gate for the byte extents and counters in lib/libtcl, linked from the
 * library's own source under the address and undefined-behavior sanitizers.
 *
 * The command-separator case runs two commands joined by a semicolon, and
 * the regexp-result case reads the regexp and regsub commands' answers for
 * a match and a miss. Every other case crosses a width the library once
 * stored in unsigned short: a variable value, the
 * interpreter's append result, a substituted word, an expression string
 * operand, a list index named "end", a variable's reference count, a
 * pattern-cache length, a regexp backtrack count, a compiled regexp program
 * and a substituted submatch. Below 65536 all of them behave the same at
 * any width, so each case is sized at or just past that boundary.
 *
 * Each case runs in its own child under an alarm, so an overflow the
 * sanitizer aborts on, a fault or a loop that never ends fails that case and
 * the parent still reports the rest. With a case name as its argument the
 * program runs that case alone, which is how each negative control in the
 * Makefile names the one mechanism its mutated library breaks.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <tcl/tcl.h>

#include "regexp.h"
#include "regpriv.h"

/*
 * The slowest case, 33000 interpreted iterations under the sanitizers,
 * finishes in well under a second; a case still running at the alarm has
 * stopped advancing.
 */
#define CASE_SECONDS	10

/*
 * A value one past the largest unsigned short, built by doubling so the
 * variable grows through Tcl_SetVar2's own reallocation path.
 */
#define GROW_65536 \
	"set s x\n" \
	"while {[string length $s] < 65536} {append s $s}\n"

struct script_case {
	const char *name;
	const char *script;
	const char *expected;
};

static const struct script_case script_cases[] = {
	/* Tcl_Eval skips the semicolon that ends a command; at a
	   separator it does not skip, TclParseWords returns no words and
	   the command loop never advances. */
	{ "command-separator",
	  "set a 1; set b 2;; list $a $b",
	  "1 2" },
	/* regexp_execute() returns 1 on a match. regexp answers 1 and sets
	   its match variable; regsub answers 1 and stores the substituted
	   string, or answers 0 and leaves its variable alone on a miss. */
	{ "regexp-result",
	  "list [regexp {b+} abbc m] $m [regexp z abc]"
	  " [regsub b abc X v] $v [regsub z abc X w] [info exists w]"
	  " [regsub -all b abcb X u] $u",
	  "1 bb 0 1 aXc 0 0 1 aXcX" },
	/* Var.valueLength and Var.valueSpace: the next append after the
	   value passes 65535 bytes grows from the true capacity. */
	{ "var-extent",
	  GROW_65536 "append s y\nstring length $s",
	  "65537" },
	/* Interp.appendAvl and appendUsed: split appends each element to
	   the result through Tcl_AppendElement (list builds its result
	   with Tcl_Merge instead), and three 32768-byte elements pass
	   65535 bytes while every variable involved stays below it. */
	{ "append-extent",
	  "set s x\n"
	  "while {[string length $s] < 32768} {append s $s}\n"
	  "string length [split $s,$s,$s ,]",
	  "98306" },
	/* ParseValue.expandProc's needed: a bare word substituting two
	   65536-byte values asks for more than 65535 bytes at once. */
	{ "parse-extent",
	  GROW_65536 "string length $s$s",
	  "131072" },
	/* The expression parser's string operand. A quoted or braced
	   operand parses into the value buffer; a variable operand is
	   copied there, which is the path that sizes the buffer. */
	{ "expr-extent",
	  GROW_65536 "string length [expr {$s}]",
	  "65536" },
	/* "end" in lrange and lreplace past the old 30000 and 32767
	   sentinels: both commands take only an integer first index. */
	{ "list-end",
	  "set l {}\n"
	  "for {set i 0} {$i < 33000} {incr i} {lappend l $i}\n"
	  "list [lrange $l 32997 end] [llength [lreplace $l 32998 end z]]",
	  "{32997 32998 32999} 32999" },
	/* Var.upvarUses: 65536 upvar references to one global would wrap
	   the count to 0, and unsetting the global would then free a
	   variable every reference still points at. */
	{ "upvar-count",
	  "proc p {} {\n"
	  "  for {set i 0} {$i < 65536} {incr i} {upvar #0 x l$i}\n"
	  "  uplevel #0 {unset x}\n"
	  "  set l0\n"
	  "}\n"
	  "set x 1\n"
	  "list [catch p msg] $msg",
	  "1 {can't read \"l0\": no such variable}" },
	/* Interp.patLengths: an unused cache slot holds length -1 and a
	   NULL pattern. Stored in unsigned short, -1 equals the length of a
	   65535-byte pattern, which was then compared with that NULL. The
	   pattern itself is refused as longer than NEXT() can link. */
	{ "pattern-cache",
	  "set p a\n"
	  "while {[string length $p] < 65535} {append p $p}\n"
	  "set p [string range $p 0 65534]\n"
	  "list [string length $p] [catch {regexp $p abc} msg] $msg",
	  "65535 1 {invalid regular expression pattern}" },
};

/*
 * Tcl_Eval writes into the command while it looks a variable up and puts
 * the byte back afterwards, so it is given a writable copy of the script.
 */
static int
run_script(const struct script_case *c)
{
	Tcl_Interp *interp;
	unsigned char *script;
	int status;

	script = (unsigned char *)strdup(c->script);
	if (script == NULL) {
		perror("strdup");
		return 0;
	}
	interp = Tcl_CreateInterp();
	status = Tcl_Eval(interp, script, 0, NULL);
	if (status != TCL_OK) {
		fprintf(stderr, "libtcl %s: status %d, result \"%.200s\"\n",
		    c->name, status, (const char *)interp->result);
		return 0;
	}
	if (strcmp((const char *)interp->result, c->expected) != 0) {
		fprintf(stderr, "libtcl %s: expected \"%s\", got \"%.200s\"\n",
		    c->name, c->expected, (const char *)interp->result);
		return 0;
	}
	return 1;
}

/* Compile pattern with the size regexp_size() reports; NULL if refused. */
static regexp_t *
compile_pattern(const char *pattern)
{
	regexp_t *r;
	unsigned size;

	size = regexp_size((const unsigned char *)pattern);
	if (size == 0)
		return NULL;
	r = malloc(size);
	if (r == NULL)
		return NULL;
	if (!regexp_compile(r, (const unsigned char *)pattern)) {
		free(r);
		return NULL;
	}
	return r;
}

static char *
repeated(int c, size_t count, const char *suffix)
{
	char *s;

	s = malloc(count + strlen(suffix) + 1);
	if (s == NULL)
		return NULL;
	memset(s, c, count);
	strcpy(s + count, suffix);
	return s;
}

static int
expect(int condition, const char *name, const char *message)
{
	if (!condition)
		fprintf(stderr, "libtcl %s: %s\n", name, message);
	return condition;
}

/*
 * A STAR or PLUS backtrack walks its repeat count down to one below the
 * minimum. The count is signed, so a failing STAR stops at -1 instead of
 * wrapping to 65535 and reading that far past the operand's start, and a
 * run longer than 65535 characters keeps its true count.
 */
static int
regexp_backtrack(void)
{
	static const char name[] = "regexp-backtrack";
	const unsigned char *s;
	regexp_t *r;
	char *subject;
	int ok = 1;

	r = compile_pattern("xa*b");
	if (!expect(r != NULL, name, "xa*b did not compile"))
		return 0;
	ok &= expect(regexp_execute(r, (const unsigned char *)"xaac") == 0,
	    name, "xa*b matched xaac");
	s = (const unsigned char *)"xaab";
	ok &= expect(regexp_execute(r, s) == 1 && r->startp[0] == s &&
	    r->endp[0] == s + 4, name, "xa*b did not match all of xaab");
	free(r);

	r = compile_pattern("xa+b");
	if (!expect(r != NULL, name, "xa+b did not compile"))
		return 0;
	ok &= expect(regexp_execute(r, (const unsigned char *)"xaac") == 0,
	    name, "xa+b matched xaac");
	free(r);

	r = compile_pattern("a*b");
	subject = repeated('a', 70000, "b");
	if (!expect(r != NULL && subject != NULL, name, "a*b setup failed"))
		return 0;
	s = (const unsigned char *)subject;
	ok &= expect(regexp_execute(r, s) == 1 && r->startp[0] == s &&
	    r->endp[0] == s + 70001, name,
	    "a*b did not match 70000 a's and a b");
	subject[70000] = 'c';
	ok &= expect(regexp_execute(r, s) == 0, name,
	    "a*b matched 70000 a's and a c");
	free(subject);
	free(r);
	return ok;
}

/*
 * NEXT() links nodes with two-byte offsets, so regexp_size() refuses a
 * program longer than 0xffff bytes rather than report a size the second
 * pass overruns. A literal compiles to about its own length plus a dozen
 * bytes of nodes.
 */
static int
regexp_program(void)
{
	static const char name[] = "regexp-program";
	regexp_t *r;
	char *pattern;
	int ok = 1;

	pattern = repeated('a', 65600, "");
	if (!expect(pattern != NULL, name, "allocation failed"))
		return 0;
	ok &= expect(regexp_size((const unsigned char *)pattern) == 0, name,
	    "a 65600-byte literal was accepted");
	free(pattern);

	pattern = repeated('a', 60000, "");
	if (!expect(pattern != NULL, name, "allocation failed"))
		return 0;
	r = compile_pattern(pattern);
	ok &= expect(r != NULL, name, "a 60000-byte literal was refused");
	if (r != NULL) {
		ok &= expect(regexp_execute(r, (const unsigned char *)pattern)
		    == 1, name, "a 60000-byte literal did not match itself");
		free(r);
	}
	free(pattern);
	return ok;
}

/*
 * regexp_substitute() copies each submatch with strncpy() for its full
 * length, which a 70000-character submatch exceeds 65535 by.
 */
static int
regexp_substitute_case(void)
{
	static const char name[] = "regexp-substitute";
	regexp_t *r;
	char *subject, *out;
	int ok = 1;

	r = compile_pattern("(a+)b");
	subject = repeated('a', 70000, "b");
	out = malloc(70001);
	if (!expect(r != NULL && subject != NULL && out != NULL, name,
	    "setup failed"))
		return 0;
	ok &= expect(regexp_execute(r, (const unsigned char *)subject) == 1,
	    name, "(a+)b did not match");
	ok &= expect(regexp_substitute(r, (const unsigned char *)"\\1",
	    (unsigned char *)out) == 1 && strlen(out) == 70000 &&
	    out[69999] == 'a', name, "\\1 did not copy all 70000 a's");
	free(out);
	free(subject);
	free(r);
	return ok;
}

struct native_case {
	const char *name;
	int (*body)(void);
};

static const struct native_case native_cases[] = {
	{ "regexp-backtrack", regexp_backtrack },
	{ "regexp-program", regexp_program },
	{ "regexp-substitute", regexp_substitute_case },
};

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

/*
 * Run one case in a child. The child leaves through _exit, so a case that
 * reports its verdict is judged by that verdict and not by what the
 * interpreter it abandons still holds.
 */
static int
isolated(const char *name, const struct script_case *script,
	const struct native_case *native)
{
	pid_t child;
	int status, ok;

	fflush(stdout);
	fflush(stderr);
	child = fork();
	if (child < 0) {
		perror("fork");
		return 0;
	}
	if (child == 0) {
		alarm(CASE_SECONDS);
		ok = script != NULL ? run_script(script) : native->body();
		fflush(stderr);
		_exit(ok ? 0 : 1);
	}
	if (waitpid(child, &status, 0) != child) {
		perror("waitpid");
		return 0;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		printf("ok %s\n", name);
		return 1;
	}
	if (WIFSIGNALED(status))
		printf("FAIL %s: signal %d\n", name, WTERMSIG(status));
	else
		printf("FAIL %s: exit %d\n", name, WEXITSTATUS(status));
	return 0;
}

int
main(int argc, char **argv)
{
	const char *only = argc > 1 ? argv[1] : NULL;
	size_t i;
	int failures = 0, ran = 0;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [case]\n", argv[0]);
		return 2;
	}
	for (i = 0; i < NELEM(script_cases); i++) {
		if (only != NULL && strcmp(only, script_cases[i].name) != 0)
			continue;
		ran++;
		if (!isolated(script_cases[i].name, &script_cases[i], NULL))
			failures++;
	}
	for (i = 0; i < NELEM(native_cases); i++) {
		if (only != NULL && strcmp(only, native_cases[i].name) != 0)
			continue;
		ran++;
		if (!isolated(native_cases[i].name, NULL, &native_cases[i]))
			failures++;
	}
	if (ran == 0) {
		fprintf(stderr, "libtcl: no case named %s\n", only);
		return 2;
	}
	printf("libtcl contracts: %d of %d cases failed\n", failures, ran);
	return failures != 0;
}
