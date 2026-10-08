/*
 * Calibration input for kr_scan.py. Each line that the scan must report
 * carries "expect: <kind> <name>" in a trailing comment; no other line may be
 * reported. The file compiles with a host C compiler under -std=gnu17 so the
 * compiler oracle can be checked against the same expectations.
 */
#define __unused __attribute__((__unused__))

static char continued_literal[] = "a backslash-newline \
keeps later line numbers";
typedef int dev_t;
struct file { int f_count; };
struct proc { int p_pid; };
struct ops { int (*op_open)(); int (*op_close)(dev_t, int); }; /* expect: declaration op_open */
struct pair { int (*first)(), (*second)(); }; /* expect: declaration first */ /* expect: declaration second */
struct qualified { int (* const fixed)(), (* volatile * restrict nested)(); }; /* expect: declaration fixed */ /* expect: declaration nested */

int empty_decl(); /* expect: declaration empty_decl */
int first_decl(), second_decl(); /* expect: declaration first_decl */ /* expect: declaration second_decl */
static int table_value = 1, *table_pointer = &table_value;
extern struct file *pointer_decl(); /* expect: declaration pointer_decl */
int typed_decl(dev_t, int);
int takes_callback(int a, int (*cb)()); /* expect: declaration cb */
void takes_function(int a, char *mid(), int last()); /* expect: declaration mid */ /* expect: declaration last */
int void_decl(void);
int (grouped_decl)(); /* expect: declaration grouped_decl */
int (**grouped_pointer)(); /* expect: declaration grouped_pointer */
static int (*handlers[2][3])(); /* expect: declaration handlers */
static int (* __attribute__((unused)) attributed_pointer)(); /* expect: declaration attributed_pointer */
int ((*nested_group))(), (*(*double_pointer))(); /* expect: declaration nested_group */ /* expect: declaration double_pointer */
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
dev_t (inactive_grouped)(); /* expect: declaration inactive_grouped */
int (* const inactive_qualified)(); /* expect: declaration inactive_qualified */
void inactive_outer(int inactive_callback()); /* expect: declaration inactive_callback */
int (*inactive_table[NSLOTS])(); /* expect: declaration inactive_table */
int ((*inactive_nested))(); /* expect: declaration inactive_nested */
int (* __attribute__((unused)) inactive_attributed_pointer)(); /* expect: declaration inactive_attributed_pointer */
/* expect: declaration inactive_spliced */ extern int inactive_\
spliced();
void
inactive_branch(t1, t2) /* expect: definition inactive_branch */
    int t1, t2;
{
}
#endif

#ifndef NOT_DEFINED_ANYWHERE
void
modern_shared_body(void)
{
/*
#endif inside a comment closes no group
 */
#else
void
legacy_shared_body() /* expect: definition legacy_shared_body */
{
#endif
}

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
(grouped_definition)() /* expect: definition grouped_definition */
{
    return 0;
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
    x += (void_definition)() + (grouped_definition)();
    typed_decl(x, (void_definition)());
    return (*fp)();
}

int
uses_calls(int x)
{
    extern int block_decl(); /* expect: declaration block_decl */
    extern __attribute__((unused, noinline)) int block_attributed(); /* expect: declaration block_attributed */
    char *block_pointer(), block_char; /* expect: declaration block_pointer */
    int y = void_definition();
#ifdef NOT_DEFINED_ANYWHERE
    extern int inactive_block(); /* expect: declaration inactive_block */
    extern __attribute__((noreturn)) int inactive_noreturn(); /* expect: declaration inactive_noreturn */
    struct file *inactive_block_pointer(); /* expect: declaration inactive_block_pointer */
#endif

    void_definition();
    y = y * void_definition();
    (void) void_definition();
    if (x)
        void_definition();
    else
        void_definition();

    if (x)
        return table[0] + y;
    return pointer_decl() != 0;
}
