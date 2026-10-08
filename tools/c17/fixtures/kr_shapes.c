/*
 * Calibration input for kr_scan.py. Each line that the scan must report
 * carries "expect: <kind> <name>" in a trailing comment; no other line may be
 * reported. The file compiles with a host C compiler under -std=gnu17 so the
 * compiler oracle can be checked against the same expectations.
 */
#define __unused __attribute__((__unused__))

typedef int dev_t;
struct file { int f_count; };
struct proc { int p_pid; };
struct ops { int (*op_open)(); int (*op_close)(dev_t, int); }; /* expect: declaration op_open */
struct pair { int (*first)(), (*second)(); }; /* expect: declaration first */ /* expect: declaration second */

int empty_decl(); /* expect: declaration empty_decl */
int first_decl(), second_decl(); /* expect: declaration first_decl */ /* expect: declaration second_decl */
static int table_value = 1, *table_pointer = &table_value;
extern struct file *pointer_decl(); /* expect: declaration pointer_decl */
int typed_decl(dev_t, int);
int takes_callback(int a, int (*cb)()); /* expect: declaration cb */
int void_decl(void);
struct ops after_decl = { 0, 0 };

static struct file files[4];
static struct proc procs[4];

int
identifier_list(a, b) /* expect: definition identifier_list */
    int a;
    char *b;
{
    return a + (b != 0);
}

int
empty_definition() /* expect: definition empty_definition */
{
    return 0;
}

int
attribute_in_declarations(dev, flag) /* expect: definition attribute_in_declarations */
    dev_t dev __unused;
    int flag __unused;
{
    return 0;
}

int
literal_attribute(dev) /* expect: definition literal_attribute */
    dev_t dev __attribute__((unused));
{
    return 0;
}

struct file *
pointer_return(f) /* expect: definition pointer_return */
    register int f;
{
    return &files[f & 3];
}

struct proc *
pointer_return_spaced (pid) /* expect: definition pointer_return_spaced */
    register int pid;
{
    return &procs[pid & 3];
}

#ifdef NOT_DEFINED_ANYWHERE
extern int inactive_attributed() __attribute__((noreturn)); /* expect: declaration inactive_attributed */
legacy_implicit(); /* expect: declaration legacy_implicit */
void
inactive_branch(t1, t2) /* expect: definition inactive_branch */
    int t1, t2;
{
}
#endif

void
after_endif() /* expect: definition after_endif */
{
}

int
function_pointer_parameter(fp) /* expect: definition function_pointer_parameter */
    int (*fp)(); /* expect: declaration fp */
{
    return fp != 0;
}

int
prototype_definition(int a, char *b)
{
    return a + (b != 0);
}

int
void_definition(void)
{
    return empty_decl() + typed_decl(1, 2) + void_decl();
}

static int table[] = { 1, 2, 3 };

static int (*hook)(void);

int
indirect_calls(int x)
{
    int (*fp)(void) = hook;

    if (fp == 0)
        return 0;
    (*fp)();
    x += void_definition() + (*fp)();
    typed_decl(x, (*fp)());
    return (*fp)();
}

int
uses_calls(int x)
{
    int y = void_definition();

    if (x)
        return table[0] + y;
    return pointer_decl() != 0;
}
