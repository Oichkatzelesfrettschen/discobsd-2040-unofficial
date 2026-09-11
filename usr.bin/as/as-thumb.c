/*
 * Assembler for Thumb-1, the ARMv6-M instruction set of the Cortex-M0+.
 * The syntax is GNU as unified Thumb syntax, as arm-none-eabi-gcc -S emits it.
 *
 * The structure follows the MIPS assembler in as.c: two passes over file-based
 * scratch segments, so the resident set stays independent of input size. Three
 * things differ from the MIPS target and drive the rest of the design.
 *
 * Instructions are halfwords, so count[] advances by two and the emitted unit
 * is a halfword. A BL occupies two halfwords and straddles a word boundary
 * whenever it sits at an odd halfword, which the MIPS one-relocation-per-word
 * stream cannot address; the object therefore carries the sparse stream that
 * a_midmag's MID_ARM6 selects, each record naming the offset it patches.
 *
 * Branch and literal-load targets are usually labels defined later in the same
 * section. Those become fixup records in the relocation scratch file, and
 * resolvefix() walks them once every symbol is known: a target in the same
 * segment is patched into the segment scratch file and its record disappears,
 * and only a cross-segment or external target survives as a relocation.
 */
#ifdef CROSS
#   include <stdio.h>
#   include <nlist.h>
#else
#   include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <a.out.h>

#define WORDSZ          4               /* word size in bytes */
#define HALFSZ          2               /* instruction unit in bytes */

/*
 * Locals beginning with L or dot are stripped off by -X flag.
 */
#define IS_LOCAL(s)     ((s)->n_name[0] == 'L' || (s)->n_name[0] == '.')

/*
 * In-memory only marker for a symbol named by .thumb_func. The low bit of
 * such a symbol's address selects the Thumb instruction set on a BX or a
 * loaded function pointer. The label keeps its even value throughout, so a
 * branch this assembler resolves internally computes the right displacement,
 * and fputsym sets the bit only in the nlist it writes.
 */
#define N_THUMB         0x100

/*
 * Types of lexemes.
 */
enum {
    LEOF = 1,           /* end of file */
    LEOL,               /* end of line */
    LNAME,              /* identifier */
    LNUM,               /* integer number */
    LLSHIFT,            /* << */
    LRSHIFT,            /* >> */
    LASCII,             /* .ascii */
    LASCIZ,             /* .asciz, .string */
    LBSS,               /* .bss */
    LCOMM,              /* .comm */
    LLCOMM,             /* .lcomm */
    LDATA,              /* .data */
    LGLOBL,             /* .global, .globl */
    LHALF,              /* .hword, .short, .half */
    LSTRNG,             /* .strng */
    LRDATA,             /* .rdata */
    LTEXT,              /* .text */
    LEQU,               /* .equ */
    LWORD,              /* .word, .long, .4byte */
    LBYTE,              /* .byte */
    LSPACE,             /* .space, .skip */
    LFILE,              /* .file */
    LSECTION,           /* .section */
    LSYMTYPE,           /* %type of symbol */
    LSECTYPE,           /* %type of section */
    LPREVIOUS,          /* .previous */
    LALIGN,             /* .align */
    LP2ALIGN,           /* .p2align */
    LSET,               /* .set */
    LTYPE,              /* .type */
    LSIZE,              /* .size */
    LIDENT,             /* .ident */
    LWEAK,              /* .weak */
    LLOCAL,             /* .local */
    LTHUMB,             /* .thumb, .code, .arm */
    LTHUMBFUNC,         /* .thumb_func */
    LSYNTAX,            /* .syntax */
    LCPU,               /* .cpu, .arch, .fpu, .arch_extension */
    LEABIATTR,          /* .eabi_attribute */
    LLTORG,             /* .ltorg, .pool */
    LSKIPLINE,          /* directive whose arguments do not matter */
};

/*
 * Segment ids.
 */
enum {
    STEXT,
    SDATA,
    SSTRNG,
    SBSS,
    SEXT,
    SABS,               /* special case for getexpr() */
};

/*
 * Sizes of tables.
 * Hash sizes should be powers of 2!
 */
#define HASHSZ  1024            /* symbol name hash table size */
#define HCMDSZ  512             /* instruction hash table size */
#define STSIZE  (HASHSZ*9/10)   /* symbol name table size */
#define MAXRLAB 200             /* max relative (digit) labels */
#define MAXPOOL 128             /* max pending literal pool entries */

/*
 * On second pass, hashtab[] is not needed.
 * We use it under name newindex[] to reindex symbol references
 * when -x or -X options are enabled.
 */
#define newindex hashtab

/*
 * Convert segment id to symbol type.
 */
const int segmtype [] = {
    N_TEXT,             /* STEXT */
    N_DATA,             /* SDATA */
    N_STRNG,            /* SSTRNG */
    N_BSS,              /* SBSS */
    N_UNDF,             /* SEXT */
    N_ABS,              /* SABS */
};

/*
 * Convert segment id to relocation type.
 */
const int segmrel [] = {
    RTEXT,              /* STEXT */
    RDATA,              /* SDATA */
    RSTRNG,             /* SSTRNG */
    RBSS,               /* SBSS */
    REXT,               /* SEXT */
    RABS,               /* SABS */
};

/*
 * Convert symbol type to segment id.
 */
const int typesegm [] = {
    SEXT,               /* N_UNDF */
    SABS,               /* N_ABS */
    STEXT,              /* N_TEXT */
    SDATA,              /* N_DATA */
    SBSS,               /* N_BSS */
    SSTRNG,             /* N_STRNG */
};

/*
 * Table of local (numeric) labels.
 */
struct labeltab {
    int num;
    int value;
};

#define RLAB_OFFSET     (1 << 23)       /* index offset of relative label */
#define RLAB_MAXVAL     1000000         /* max value of relative label */

/*
 * A fixup is a reference whose target is not yet known. It lives in the
 * relocation scratch file until resolvefix() either patches it into the
 * segment or turns it into a relocation record.
 */
struct fixup {
    unsigned flags;                     /* segment bits and RTFMASK format */
    unsigned addr;                      /* offset in its segment */
    unsigned index;                     /* symbol index, or relative label */
    int addend;                         /* added to the target address */
};

#define FIXUPSZ 13                      /* bytes per fixup scratch record */

/*
 * A pending literal pool. Identical constants share one slot, as GNU as
 * does, so the value table and the reference table are separate: a slot
 * is emitted once and every load that wants it is patched to reach it.
 */
struct poolent {
    unsigned value;                     /* the constant, or symbol addend */
    int segment;                        /* segment of the expression */
    unsigned index;                     /* symbol index when segment is SEXT */
    unsigned at;                        /* offset where the slot was emitted */
};

struct poolref {
    unsigned ref;                       /* offset of the ldr to patch */
    int ent;                            /* index into pool[] */
};

/*
 * Instruction operand formats.
 */
enum {
    TNONE = 1,          /* no operands */
    TSHIFT,             /* lsls/lsrs/asrs rd, rm, #imm5 or rd, rm */
    TALU,               /* rd, rm through the 0x4000 data-processing block */
    TADD,               /* add and adds in all their forms */
    TSUB,               /* sub and subs */
    TMOV,               /* mov and movs */
    TCMP,               /* cmp, low or high registers, or an immediate */
    TCMN,               /* cmn rd, rm */
    TNEG,               /* negs/rsbs rd, rm, #0 */
    TBX,                /* bx/blx rm */
    TLDST,              /* the load and store block */
    TADR,               /* adr rd, label */
    TEXTEND,            /* sxth/sxtb/uxth/uxtb rd, rm */
    TREV,               /* rev/rev16/revsh rd, rm */
    TPUSHPOP,           /* push/pop {reglist} */
    TSTMLDM,            /* stmia/ldmia rn!, {reglist} */
    TBCOND,             /* b<cond> label */
    TB,                 /* b label */
    TBL,                /* bl label */
    TIMM8,              /* svc/bkpt/udf #imm8 */
    TMSR,               /* msr spec, rn */
    TMRS,               /* mrs rd, spec */
    TBARRIER,           /* dmb/dsb/isb [option] */
    TCPS,               /* cpsie/cpsid i */
};

struct optable {
    const char *name;                   /* instruction name */
    unsigned opcode;                    /* base encoding */
    unsigned type;                      /* operand format */
};

/*
 * Every 16-bit ARMv6-M encoding, plus the 32-bit BL, MSR, MRS, DMB, DSB
 * and ISB. Names carrying a condition suffix are expanded by lookcmd().
 */
const struct optable optable [] = {
    /* Shift by immediate, or the register form through the ALU block. */
    { "lsl",    0x0000, TSHIFT },
    { "lsls",   0x0000, TSHIFT },
    { "lsr",    0x0800, TSHIFT },
    { "lsrs",   0x0800, TSHIFT },
    { "asr",    0x1000, TSHIFT },
    { "asrs",   0x1000, TSHIFT },

    /* Add and subtract in every form. */
    { "add",    0,      TADD },
    { "adds",   0,      TADD },
    { "sub",    0,      TSUB },
    { "subs",   0,      TSUB },

    /* Move and compare. */
    { "mov",    0,      TMOV },
    { "movs",   1,      TMOV },
    { "cmp",    0,      TCMP },
    { "cmn",    0x42c0, TCMN },
    { "neg",    0x4240, TNEG },
    { "negs",   0x4240, TNEG },
    { "rsb",    0x4240, TNEG },
    { "rsbs",   0x4240, TNEG },

    /* Data processing, register to register. */
    { "and",    0x4000, TALU },
    { "ands",   0x4000, TALU },
    { "eor",    0x4040, TALU },
    { "eors",   0x4040, TALU },
    { "adc",    0x4140, TALU },
    { "adcs",   0x4140, TALU },
    { "sbc",    0x4180, TALU },
    { "sbcs",   0x4180, TALU },
    { "ror",    0x41c0, TALU },
    { "rors",   0x41c0, TALU },
    { "tst",    0x4200, TALU },
    { "orr",    0x4300, TALU },
    { "orrs",   0x4300, TALU },
    { "mul",    0x4340, TALU },
    { "muls",   0x4340, TALU },
    { "bic",    0x4380, TALU },
    { "bics",   0x4380, TALU },
    { "mvn",    0x43c0, TALU },
    { "mvns",   0x43c0, TALU },

    /* Branch and exchange. */
    { "bx",     0x4700, TBX },
    { "blx",    0x4780, TBX },

    /* Loads and stores. */
    /* The opcode field indexes ldsttab[]. */
    { "str",    0,      TLDST },
    { "ldr",    1,      TLDST },
    { "strb",   2,      TLDST },
    { "ldrb",   3,      TLDST },
    { "strh",   4,      TLDST },
    { "ldrh",   5,      TLDST },
    { "ldrsb",  6,      TLDST },
    { "ldrsh",  7,      TLDST },
    { "adr",    0,      TADR },

    /* Sign and zero extension. */
    { "sxth",   0xb200, TEXTEND },
    { "sxtb",   0xb240, TEXTEND },
    { "uxth",   0xb280, TEXTEND },
    { "uxtb",   0xb2c0, TEXTEND },

    /* Byte reversal. */
    { "rev",    0xba00, TREV },
    { "rev16",  0xba40, TREV },
    { "revsh",  0xbac0, TREV },

    /* Stack and multiple transfer. */
    { "push",   0xb400, TPUSHPOP },
    { "pop",    0xbc00, TPUSHPOP },
    { "stmia",  0xc000, TSTMLDM },
    { "stm",    0xc000, TSTMLDM },
    { "ldmia",  0xc800, TSTMLDM },
    { "ldm",    0xc800, TSTMLDM },

    /* Branches. */
    { "b",      0xe000, TB },
    { "bl",     0,      TBL },

    /* Exception and debug. */
    { "svc",    0xdf00, TIMM8 },
    { "swi",    0xdf00, TIMM8 },
    { "bkpt",   0xbe00, TIMM8 },
    { "udf",    0xde00, TIMM8 },

    /*
     * Hints. GNU as encodes nop on a Thumb-1 target as "mov r8, r8"
     * rather than the 0xbf00 hint, and GCC's own padding uses that form,
     * so the assemblers agree byte for byte.
     */
    { "nop",    0x46c0, TNONE },
    { "yield",  0xbf10, TNONE },
    { "wfe",    0xbf20, TNONE },
    { "wfi",    0xbf30, TNONE },
    { "sev",    0xbf40, TNONE },

    /* System register access and barriers, all 32-bit. */
    { "msr",    0,      TMSR },
    { "mrs",    0,      TMRS },
    { "dmb",    0xf3bf8f50, TBARRIER },
    { "dsb",    0xf3bf8f40, TBARRIER },
    { "isb",    0xf3bf8f60, TBARRIER },
    { "cpsie",  0xb662, TCPS },
    { "cpsid",  0xb672, TCPS },
    { 0, 0, 0 },
};

/*
 * Condition codes, in encoding order. "al" is index 14; "nv" is not
 * encodable as a branch and is absent.
 */
const char *condtab [] = {
    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "al", 0,
};

int hashtab [HASHSZ], hashctab [HCMDSZ];

#define ISHEX(c)        (ctype[(c)&0377] & 1)
#define ISOCTAL(c)      (ctype[(c)&0377] & 2)
#define ISDIGIT(c)      (ctype[(c)&0377] & 4)
#define ISLETTER(c)     (ctype[(c)&0377] & 8)

/*
 * Character classes, as in as.c, with two changes for ARM input: the
 * percent sign opens a symbol or section type and so reads as a name
 * character, and the at sign opens a comment rather than a type.
 */
const char ctype [256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,8,8,0,0,0,0,0,0,0,0,8,0,7,7,7,7,7,7,7,7,5,5,0,0,0,0,0,0,
    0,9,9,9,9,9,9,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,0,0,0,0,8,
    0,9,9,9,9,9,9,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,0,
};

FILE *sfile [SABS], *rfile [SABS];
unsigned count [SABS];
unsigned nfixup [SABS];                 /* fixup records per segment */
unsigned nreloc [SABS];                 /* relocation records per segment */
unsigned relbytes [SABS];               /* relocation bytes per segment */
int segm;
char *infile, *outfile = "a.out";
char tfilename[] = "/tmp/tasXXXXXX";
int line;                               /* Source line number */
int xflags, Xflag, uflag;
int stlength;                           /* Symbol table size in bytes */
int stalign;                            /* Symbol table alignment */
unsigned tbase, dbase, adbase, bbase;
struct nlist stab [STSIZE];
int stabfree;
char space [STSIZE*8];                  /* Area for symbol names */
int lastfree;                           /* Free space offset */
char name [256];
unsigned intval;
int extref;
int blexflag, backlex, blextype;

struct labeltab labeltab [MAXRLAB];     /* relative labels */
int nlabels;

struct poolent pool [MAXPOOL];          /* pending literal pool slots */
int npool;
struct poolref poolrefs [MAXPOOL];      /* loads waiting for those slots */
int npoolref;
unsigned poolfirst;                     /* offset of the oldest pending ldr */

int lastthumbfunc = -1;                 /* symbol awaiting a .thumb_func label */

/*
 * Set by getterm() when an expression names an already-defined Thumb
 * function. getexpr() folds the symbol's value into a plain number, which
 * loses the identity that carries the low address bit, so a caller that
 * stores an absolute address consults this flag. Callers clear it first,
 * the way as.c handles its own expression flags.
 */
int expr_thumb;

/*
 * Set while the first of the two source passes runs. That pass exists only
 * to learn label addresses, so an expression it cannot evaluate yet -- the
 * difference of a forward label and a local one in a switch table -- yields
 * zero instead of an error. Every construct that reaches this produces a
 * fixed number of bytes whatever its value, so the addresses the pass
 * computes are the ones the second pass assembles against.
 */
int prescan;

struct reloc relabs = { RABS, 0, 0, 0 };

/* Forward declarations. */
unsigned getexpr (int *s);
void ltorg (void);
int getreg (void);
void align (int);
void alignfill (int, int);

/*
 * Fatal error message.
 */
void
uerror(char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    fprintf (stderr, "as: ");
    if (infile)
        fprintf (stderr, "%s, ", infile);
    if (line)
        fprintf (stderr, "%d: ", line);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    fprintf (stderr, "\n");
    exit (1);
}

/*
 * Write a 4-byte word to the file, little-endian.
 */
void
fputword(unsigned int w, FILE *f)
{
    putc (w, f);
    putc (w >> 8, f);
    putc (w >> 16, f);
    putc (w >> 24, f);
}

/*
 * Emit a Thumb sparse relocation record, and return its length.
 */
unsigned int
fputrel(struct reloc *r, FILE *f)
{
    unsigned nbytes = 5;

    putc (r->flags, f);
    fputword (r->addr, f);
    if ((r->flags & RSMASK) == REXT) {
        putc (r->index, f);
        putc (r->index >> 8, f);
        putc (r->index >> 16, f);
        nbytes += 3;
    }
    return nbytes;
}

/*
 * Write and read a fixup record in the relocation scratch file.
 */
void
fputfix(struct fixup *fx, FILE *f)
{
    putc (fx->flags, f);
    fputword (fx->addr, f);
    fputword (fx->index, f);
    fputword ((unsigned) fx->addend, f);
}

void
fgetfix(FILE *f, struct fixup *fx)
{
    unsigned v;

    fx->flags = getc (f);
    fx->addr = getc (f);
    fx->addr |= getc (f) << 8;
    fx->addr |= getc (f) << 16;
    fx->addr |= (unsigned) getc (f) << 24;
    fx->index = getc (f);
    fx->index |= getc (f) << 8;
    fx->index |= getc (f) << 16;
    fx->index |= (unsigned) getc (f) << 24;
    v = getc (f);
    v |= getc (f) << 8;
    v |= getc (f) << 16;
    v |= (unsigned) getc (f) << 24;
    fx->addend = (int) v;
}

/*
 * Write the a.out header to the file. MID_ARM6 tells ld to read the
 * sparse relocation stream.
 */
void
fputhdr(struct exec *filhdr, FILE *coutb)
{
    fputword (filhdr->a_midmag, coutb);
    fputword (filhdr->a_text, coutb);
    fputword (filhdr->a_data, coutb);
    fputword (filhdr->a_bss, coutb);
    fputword (filhdr->a_reltext, coutb);
    fputword (filhdr->a_reldata, coutb);
    fputword (filhdr->a_syms, coutb);
    fputword (filhdr->a_entry, coutb);
}

/*
 * Emit the nlist record for the symbol. A .thumb_func symbol carries the
 * low bit here and nowhere else, so internal branch resolution saw the
 * even address while a loaded function pointer gets the odd one.
 */
void
fputsym(struct nlist *s, FILE *file)
{
    unsigned value = s->n_value;
    int i;

    if ((s->n_type & N_THUMB) && (s->n_type & N_TYPE) == N_TEXT)
        value |= 1;
    putc (s->n_len, file);
    putc (s->n_type & 0xff & ~N_LOC, file);
    fputword (value, file);
    for (i=0; i<s->n_len; i++)
        putc (s->n_name[i], file);
}

/*
 * Create temporary files for STEXT, SDATA and SSTRNG segments.
 */
void
startup(void)
{
    int i;
    int fd = mkstemp (tfilename);

    if (fd == -1)
        uerror ("cannot create temporary file %s", tfilename);
    else
        close (fd);
    for (i=STEXT; i<SBSS; i++) {
        sfile [i] = fopen (tfilename, "w+");
        if (! sfile [i])
            uerror ("cannot open %s", tfilename);
        unlink (tfilename);
        rfile [i] = fopen (tfilename, "w+");
        if (! rfile [i])
            uerror ("cannot open %s", tfilename);
        unlink (tfilename);
    }
    line = 1;
}

/*
 * Suboptimal 32-bit hash function.
 * Copyright (C) 2006 Serge Vakulenko.
 */
unsigned int
hash_rot13(const char *s)
{
    unsigned hash, c;

    hash = 0;
    while ((c = (unsigned char) *s++) != 0) {
        hash += c;
        hash -= (hash << 13) | (hash >> 19);
    }
    return hash;
}

void
hashinit(void)
{
    int i, h;
    const struct optable *p;

    for (i=0; i<HCMDSZ; i++)
        hashctab[i] = -1;
    for (p=optable; p->name; p++) {
        h = hash_rot13 (p->name) & (HCMDSZ-1);
        while (hashctab[h] != -1)
            if (--h < 0)
                h += HCMDSZ;
        hashctab[h] = p - optable;
    }
    for (i=0; i<HASHSZ; i++)
        hashtab[i] = -1;
}

int
hexdig(int c)
{
    if (c <= '9')
        return (c - '0');
    else if (c <= 'F')
        return (c - 'A' + 10);
    else
        return (c - 'a' + 10);
}

/*
 * Get hexadecimal number 0xZZZ
 */
void
gethnum(void)
{
    int c;
    char *cp;

    c = getchar ();
    for (cp=name; ISHEX(c); c=getchar())
        *cp++ = hexdig (c);
    ungetc (c, stdin);
    intval = 0;
    for (c=0; c<32; c+=4) {
        if (--cp < name)
            return;
        intval |= (unsigned) *cp << c;
    }
}

/*
 * Get a number: 1234 decimal, 01234 octal.
 */
void
getnum(int c)
{
    char *cp;
    int leadingzero;

    leadingzero = (c=='0');
    for (cp=name; ISDIGIT(c); c=getchar())
        *cp++ = hexdig (c);
    ungetc (c, stdin);
    intval = 0;
    if (leadingzero) {
        for (c=0; c<=27; c+=3) {
            if (--cp < name)
                return;
            intval |= (unsigned) *cp << c;
        }
        if (--cp < name)
            return;
        intval |= (unsigned) *cp << 30;
        return;
    }
    for (c=1; ; c*=10) {
        if (--cp < name)
            return;
        intval += *cp * c;
    }
}

void
getname(int c)
{
    char *cp;

    for (cp=name; ISLETTER (c) || ISDIGIT (c); c=getchar())
        *cp++ = c;
    *cp = 0;
    ungetc (c, stdin);
}

/*
 * GCC writes ELF symbol and section types with a percent sign on ARM,
 * where the MIPS back end writes an at sign.
 */
int
looktype(void)
{
    switch (name [1]) {
    case 'c':
        if (! strcmp ("%common", name)) return (LSYMTYPE);
        break;
    case 'f':
        if (! strcmp ("%fini_array", name)) return (LSECTYPE);
        if (! strcmp ("%function", name)) return (LSYMTYPE);
        break;
    case 'g':
        if (! strcmp ("%gnu_indirect_function", name)) return (LSYMTYPE);
        if (! strcmp ("%gnu_unique_object", name)) return (LSYMTYPE);
        break;
    case 'i':
        if (! strcmp ("%init_array", name)) return (LSECTYPE);
        break;
    case 'n':
        if (! strcmp ("%nobits", name)) return (LSECTYPE);
        if (! strcmp ("%note", name)) return (LSECTYPE);
        if (! strcmp ("%notype", name)) return (LSYMTYPE);
        break;
    case 'o':
        if (! strcmp ("%object", name)) return (LSYMTYPE);
        break;
    case 'p':
        if (! strcmp ("%progbits", name)) return (LSECTYPE);
        if (! strcmp ("%preinit_array", name)) return (LSECTYPE);
        break;
    case 't':
        if (! strcmp ("%tls_object", name)) return (LSYMTYPE);
        break;
    }
    return (-1);
}

int
lookacmd(void)
{
    switch (name [1]) {
    case '2':
        if (! strcmp (".2byte", name)) return (LHALF);
        break;
    case '4':
        if (! strcmp (".4byte", name)) return (LWORD);
        break;
    case 'a':
        if (! strcmp (".ascii", name)) return (LASCII);
        if (! strcmp (".asciz", name)) return (LASCIZ);
        if (! strcmp (".align", name)) return (LALIGN);
        if (! strcmp (".arm", name)) return (LTHUMB);
        if (! strcmp (".arch", name)) return (LCPU);
        if (! strcmp (".arch_extension", name)) return (LCPU);
        break;
    case 'b':
        if (! strcmp (".bss", name)) return (LBSS);
        if (! strcmp (".byte", name)) return (LBYTE);
        break;
    case 'c':
        if (! strcmp (".comm", name)) return (LCOMM);
        if (! strcmp (".code", name)) return (LTHUMB);
        if (! strcmp (".cpu", name)) return (LCPU);
        if (! strcmp (".cfi_startproc", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_endproc", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_sections", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_def_cfa_offset", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_offset", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_def_cfa_register", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_restore", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_remember_state", name)) return (LSKIPLINE);
        if (! strcmp (".cfi_restore_state", name)) return (LSKIPLINE);
        break;
    case 'd':
        if (! strcmp (".data", name)) return (LDATA);
        break;
    case 'e':
        if (! strcmp (".equ", name)) return (LEQU);
        if (! strcmp (".eabi_attribute", name)) return (LEABIATTR);
        if (! strcmp (".end", name)) return (LSKIPLINE);
        if (! strcmp (".extern", name)) return (LSKIPLINE);
        break;
    case 'f':
        if (! strcmp (".file", name)) return (LFILE);
        if (! strcmp (".fpu", name)) return (LCPU);
        if (! strcmp (".fnstart", name)) return (LSKIPLINE);
        if (! strcmp (".fnend", name)) return (LSKIPLINE);
        break;
    case 'g':
        if (! strcmp (".globl", name)) return (LGLOBL);
        if (! strcmp (".global", name)) return (LGLOBL);
        break;
    case 'h':
        if (! strcmp (".half", name)) return (LHALF);
        if (! strcmp (".hword", name)) return (LHALF);
        break;
    case 'i':
        if (! strcmp (".ident", name)) return (LIDENT);
        if (! strcmp (".inst", name)) return (LHALF);
        break;
    case 'l':
        if (! strcmp (".local", name)) return (LLOCAL);
        if (! strcmp (".long", name)) return (LWORD);
        if (! strcmp (".lcomm", name)) return (LLCOMM);
        if (! strcmp (".ltorg", name)) return (LLTORG);
        break;
    case 'p':
        if (! strcmp (".previous", name)) return (LPREVIOUS);
        if (! strcmp (".p2align", name)) return (LP2ALIGN);
        if (! strcmp (".pool", name)) return (LLTORG);
        if (! strcmp (".personality", name)) return (LSKIPLINE);
        break;
    case 'r':
        if (! strcmp (".rdata", name)) return (LRDATA);
        break;
    case 's':
        if (! strcmp (".section", name)) return (LSECTION);
        if (! strcmp (".set", name)) return (LSET);
        if (! strcmp (".size", name)) return (LSIZE);
        if (! strcmp (".space", name)) return (LSPACE);
        if (! strcmp (".skip", name)) return (LSPACE);
        if (! strcmp (".short", name)) return (LHALF);
        if (! strcmp (".string", name)) return (LASCIZ);
        if (! strcmp (".strng", name)) return (LSTRNG);
        if (! strcmp (".syntax", name)) return (LSYNTAX);
        break;
    case 't':
        if (! strcmp (".text", name)) return (LTEXT);
        if (! strcmp (".type", name)) return (LTYPE);
        if (! strcmp (".thumb", name)) return (LTHUMB);
        if (! strcmp (".thumb_func", name)) return (LTHUMBFUNC);
        break;
    case 'w':
        if (! strcmp (".word", name)) return (LWORD);
        if (! strcmp (".weak", name)) return (LWEAK);
        break;
    }
    return (-1);
}

/*
 * Change a segment based on a section name. A read-only section becomes
 * SSTRNG, which pass2 folds into the data segment.
 */
void
setsection(void)
{
    struct {
        const char *name;
        int len;
        int segm;
    } const *p, map[] = {
        { ".text",    5, STEXT  },
        { ".data",    5, SDATA  },
        { ".sdata",   6, SDATA  },
        { ".rodata",  7, SSTRNG },
        { ".bss",     4, SBSS   },
        { ".sbss",    5, SBSS   },
        { ".init_array",  11, SDATA },
        { ".fini_array",  11, SDATA },
        { ".preinit_array", 14, SDATA },
        { 0, 0, 0 },
    };

    for (p=map; p->name; p++) {
        if (strncmp (name, p->name, p->len) == 0 &&
            (name [p->len] == 0 || name [p->len] == '.'))
        {
            if (segm == STEXT && p->segm != STEXT)
                ltorg ();
            segm = p->segm;
            return;
        }
    }
    uerror ("bad .section name %s", name);
}

/*
 * Recognize a register name in operand position. Returns 0 to 15, or -1.
 * The AAPCS aliases and the architectural names both appear in compiler
 * and hand-written input.
 */
int
lookregname(const char *s)
{
    int v;

    if ((s[0] == 'r' || s[0] == 'R') && ISDIGIT (s[1])) {
        v = s[1] - '0';
        if (s[2] == 0)
            return v;
        if (ISDIGIT (s[2]) && s[3] == 0) {
            v = v * 10 + (s[2] - '0');
            if (v <= 15)
                return v;
        }
        return -1;
    }
    if (! strcmp (s, "sp") || ! strcmp (s, "SP")) return 13;
    if (! strcmp (s, "lr") || ! strcmp (s, "LR")) return 14;
    if (! strcmp (s, "pc") || ! strcmp (s, "PC")) return 15;
    if (! strcmp (s, "ip")) return 12;
    if (! strcmp (s, "fp")) return 11;
    if (! strcmp (s, "sl")) return 10;
    if (! strcmp (s, "sb")) return 9;
    return -1;
}

/*
 * Look up an instruction. A conditional branch carries its condition in
 * the mnemonic, so "bne" resolves to "b" with the condition in *cond.
 */
int
lookcmd(int *cond)
{
    int i, h, n;
    char base [64];

    *cond = -1;
    h = hash_rot13 (name) & (HCMDSZ-1);
    while ((i = hashctab[h]) != -1) {
        if (! strcmp (optable[i].name, name))
            return (i);
        if (--h < 0)
            h += HCMDSZ;
    }
    /* Try to split a two-letter condition suffix off the mnemonic. */
    n = strlen (name);
    if (n < 3 || n >= (int) sizeof(base))
        return (-1);
    for (i=0; condtab[i]; i++) {
        if (strcmp (name + n - 2, condtab[i]) != 0)
            continue;
        memcpy (base, name, n - 2);
        base [n - 2] = 0;
        /* Only a branch takes a condition on ARMv6-M. */
        if (strcmp (base, "b") != 0)
            continue;
        *cond = i;
        h = hash_rot13 (base) & (HCMDSZ-1);
        while ((n = hashctab[h]) != -1) {
            if (! strcmp (optable[n].name, base))
                return (n);
            if (--h < 0)
                h += HCMDSZ;
        }
        return (-1);
    }
    return (-1);
}

char *
alloc(int len)
{
    int r;

    r = lastfree;
    lastfree += len;
    if (lastfree > (int) sizeof(space))
        uerror ("out of memory");
    return (space + r);
}

int
lookname(void)
{
    int i, h, n_name_len;

    h = hash_rot13 (name) & (HASHSZ-1);
    while ((i = hashtab[h]) != -1) {
        if (! strcmp (stab[i].n_name, name))
            return (i);
        if (--h < 0)
            h += HASHSZ;
    }
    if ((i = stabfree++) >= STSIZE)
        uerror ("symbol table overflow");
    stab[i].n_len = strlen (name);
    n_name_len = stab[i].n_len + 1;
    stab[i].n_name = alloc (n_name_len);
    memcpy (stab[i].n_name, name, n_name_len);
    stab[i].n_value = 0;
    stab[i].n_type = N_UNDF;
    hashtab[h] = i;
    return (i);
}

/*
 * Read a lexical element. Unlike the MIPS lexer, '@' opens a comment and
 * '#' introduces an immediate, which is how GNU as reads ARM input.
 */
int
getlex(int *pval)
{
    int c;

    if (blexflag) {
        blexflag = 0;
        *pval = blextype;
        return (backlex);
    }
    for (;;) {
        switch (c = getchar()) {
        case '@':
skiptoeol:  while ((c = getchar()) != '\n')
                if (c == EOF)
                    return (LEOF);
            /* FALLTHROUGH */
        case '\n':
            ++line;
            c = getchar ();
            if (c == '@')
                goto skiptoeol;
            ungetc (c, stdin);
            /* FALLTHROUGH */
        case ';':
            *pval = line;
            return (LEOL);
        case ' ':
        case '\t':
        case '\r':
            continue;
        case EOF:
            return (LEOF);
        case '/':
            c = getchar ();
            if (c == '*') {
                /* Block comment. */
                for (;;) {
                    c = getchar ();
                    if (c == EOF)
                        return (LEOF);
                    if (c == '\n')
                        ++line;
                    if (c == '*') {
                        c = getchar ();
                        if (c == '/')
                            break;
                        ungetc (c, stdin);
                    }
                }
                continue;
            }
            ungetc (c, stdin);
            return ('/');
        case '\\':
            c = getchar ();
            if (c=='<')
                return (LLSHIFT);
            if (c=='>')
                return (LRSHIFT);
            ungetc (c, stdin);
            return ('\\');
        case '#':
            /* An immediate prefix carries no value of its own. */
            return ('#');
        case '\'':      case '^':       case '&':       case '|':
        case '~':       case '+':       case '-':       case '*':
        case '"':       case ',':       case '[':       case ']':
        case '(':       case ')':       case '{':       case '}':
        case '=':       case ':':       case '!':
            return (c);
        case '<':
            c = getchar ();
            if (c == '<')
                return (LLSHIFT);
            ungetc (c, stdin);
            return ('<');
        case '>':
            c = getchar ();
            if (c == '>')
                return (LRSHIFT);
            ungetc (c, stdin);
            return ('>');
        case '0':
            if ((c = getchar ()) == 'x' || c=='X') {
                gethnum ();
                return (LNUM);
            }
            ungetc (c, stdin);
            c = '0';
            /* FALLTHROUGH */
        case '1':       case '2':       case '3':
        case '4':       case '5':       case '6':       case '7':
        case '8':       case '9':
            getnum (c);
            return (LNUM);
        case '%':
            getname (c);
            *pval = looktype();
            if (*pval != -1)
                return (*pval);
            return ('%');
        default:
            if (! ISLETTER (c))
                uerror ("bad character: \\%o", c & 0377);
            getname (c);
            if (name[0] == '.') {
                if (name[1] == 0)
                    return ('.');
                *pval = lookacmd();
                if (*pval != -1)
                    return (*pval);
            }
            return (LNAME);
        }
    }
}

void
ungetlex(int val, int type)
{
    blexflag = 1;
    backlex = val;
    blextype = type;
}

int
getterm(void)
{
    int ty;
    int cval, s;

    switch (getlex (&cval)) {
    default:
        uerror ("operand missed");
        /* NOTREACHED */
        /* FALLTHROUGH */
    case LNUM:
        cval = getchar ();
        if (cval == 'b' || cval == 'B')
            extref = RLAB_OFFSET - intval;
        else if (cval == 'f' || cval == 'F')
            extref = RLAB_OFFSET + intval;
        else {
            ungetc (cval, stdin);
            return (SABS);
        }
        if (intval >= RLAB_MAXVAL)
            uerror ("too large relative label");
        intval = 0;
        return (SEXT);
    case '\'':
        /* Character literal. */
        intval = getchar ();
        if (intval == '\\') {
            intval = getchar ();
            switch (intval) {
            case 'n': intval = '\n'; break;
            case 't': intval = '\t'; break;
            case 'r': intval = '\r'; break;
            case 'b': intval = '\b'; break;
            case 'f': intval = '\f'; break;
            case '0': intval = 0; break;
            }
        }
        cval = getchar ();
        if (cval != '\'')
            ungetc (cval, stdin);
        return (SABS);
    case LNAME:
        intval = 0;
        cval = lookname();
        ty = stab[cval].n_type & N_TYPE;
        if (ty==N_UNDF || ty==N_COMM) {
            extref = cval;
            return (SEXT);
        }
        if (ty == N_TEXT && (stab[cval].n_type & N_THUMB))
            expr_thumb = 1;
        intval = stab[cval].n_value;
        return (typesegm [ty]);
    case '.':
        intval = count[segm];
        return (segm);
    case '(':
        getexpr (&s);
        if (getlex (&cval) != ')')
            uerror ("bad () syntax");
        return (s);
    }
}

/*
 * Get an expression. Return a value, put a base segment id to *s.
 */
unsigned int
getexpr(int *s)
{
    int clex;
    int cval, s2;
    unsigned rez;

    switch (clex = getlex (&cval)) {
    default:
        ungetlex (clex, cval);
        rez = 0;
        *s = SABS;
        break;
    case '-':
        /* Unary minus. */
        *s = getterm ();
        if (*s != SABS)
            uerror ("bad negation of a relocatable value");
        rez = -intval;
        break;
    case '~':
        *s = getterm ();
        if (*s != SABS)
            uerror ("bad complement of a relocatable value");
        rez = ~intval;
        break;
    case LNUM:
    case LNAME:
    case '.':
    case '(':
    case '\'':
        ungetlex (clex, cval);
        *s = getterm ();
        rez = intval;
        break;
    }
    for (;;) {
        switch (clex = getlex (&cval)) {
        case '+':
            s2 = getterm ();
            if (*s == SABS)
                *s = s2;
            else if (s2 != SABS)
                uerror ("too complex expression");
            rez += intval;
            break;
        case '-':
            s2 = getterm ();
            if (s2 == *s && s2 != SEXT)
                *s = SABS;
            else if (s2 != SABS) {
                if (! prescan)
                    uerror ("too complex expression");
                *s = SABS;
                rez = 0;
                intval = 0;
            }
            rez -= intval;
            break;
        case '&':
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez &= intval;
            break;
        case '|':
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez |= intval;
            break;
        case '^':
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez ^= intval;
            break;
        case LLSHIFT:
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez <<= intval & 037;
            break;
        case LRSHIFT:
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez >>= intval & 037;
            break;
        case '*':
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            rez *= intval;
            break;
        case '/':
            s2 = getterm ();
            if (*s != SABS || s2 != SABS)
                uerror ("too complex expression");
            if (intval == 0) {
                if (! prescan)
                    uerror ("division by zero");
                intval = 1;
            }
            rez /= intval;
            break;
        default:
            ungetlex (clex, cval);
            intval = rez;
            return (rez);
        }
    }
    /* NOTREACHED */
}

/*
 * Emit one instruction halfword into the current segment.
 */
void
emithalf(unsigned int h)
{
    if (segm >= SBSS)
        uerror ("instruction or data in bss");
    putc (h, sfile[segm]);
    putc (h >> 8, sfile[segm]);
    count[segm] += HALFSZ;
}

void
emitword(unsigned int w)
{
    emithalf (w & 0xffff);
    emithalf (w >> 16);
}

/*
 * Record a reference whose target is not yet known.
 */
void
emitfix(unsigned int format, int segment, unsigned int index,
    unsigned int addr, int addend)
{
    struct fixup fx;

    fx.flags = (segmrel [segment] & RSMASK) | (format & RTFMASK);
    fx.addr = addr;
    fx.index = index;
    fx.addend = addend;
    fputfix (&fx, rfile[segm]);
    nfixup[segm]++;
}

/*
 * Patch a halfword already written to the segment scratch file.
 */
void
patchhalf(int s, unsigned int addr, unsigned int h)
{
    long save = ftell (sfile[s]);

    fseek (sfile[s], (long) addr, 0);
    putc (h, sfile[s]);
    putc (h >> 8, sfile[s]);
    fseek (sfile[s], save, 0);
}

unsigned int
peekhalf(int s, unsigned int addr)
{
    long save = ftell (sfile[s]);
    unsigned h;

    fseek (sfile[s], (long) addr, 0);
    h = getc (sfile[s]) & 0xff;
    h |= (getc (sfile[s]) & 0xff) << 8;
    fseek (sfile[s], save, 0);
    return h;
}

/*
 * Align the current segment to a power-of-two boundary.
 */
/*
 * Align the current segment. GNU as fills the padding an .align directive
 * inserts into code with the nop encoding, and fills the padding it adds
 * on its own before a literal pool with zeroes; the two assemblers agree
 * only if that distinction is kept, so usenop says which caller this is.
 */
void
alignfill(int align_bits, int usenop)
{
    unsigned nbytes, align_mask, c;

    align_mask = (1 << align_bits) - 1;
    nbytes = count[segm] & align_mask;
    if (nbytes == 0)
        return;
    nbytes = align_mask + 1 - nbytes;
    if (usenop && segm == STEXT &&
        ! (count[segm] & 1) && ! (nbytes & 1)) {
        for (c=0; c<nbytes; c+=HALFSZ)
            emithalf (0x46c0);
    } else if (segm < SBSS) {
        for (c=0; c<nbytes; c++) {
            count[segm]++;
            putc (0, sfile[segm]);
        }
    } else
        count[segm] += nbytes;
}

void
align(int align_bits)
{
    alignfill (align_bits, 0);
}

/*
 * Emit a needed amount of zeroes.
 */
void
add_space(unsigned int nbytes, unsigned int fill_data)
{
    unsigned c;

    if (segm < SBSS) {
        for (c=0; c<nbytes; c++) {
            count[segm]++;
            if (fill_data)
                putc (0, sfile[segm]);
        }
    } else
        count[segm] += nbytes;
}

/*
 * Emit the pending literal pool and patch every ldr that refers into it.
 * Called at .ltorg, when the pool would drift out of the 1020-byte reach
 * of a PC-relative load, when the section changes, and at end of input.
 */
void
ltorg(void)
{
    int i, savedsegm;
    unsigned hw, disp;

    if (npool == 0)
        return;
    savedsegm = segm;
    segm = STEXT;
    align (2);
    for (i=0; i<npool; i++) {
        pool[i].at = count[STEXT];
        /*
         * A slot holding a relocatable address needs a relocation of its
         * own, exactly as a .word would.
         */
        if (pool[i].segment != SABS)
            emitfix (RTABS32, pool[i].segment, pool[i].index, pool[i].at,
                (int) pool[i].value);
        emitword (pool[i].segment == SABS ? pool[i].value : 0);
    }
    for (i=0; i<npoolref; i++) {
        /* The load reads (pc & ~3) + imm8*4, with pc two halfwords on. */
        disp = pool[poolrefs[i].ent].at -
               ((poolrefs[i].ref + 4) & ~3u);
        if (disp > 1020 || (disp & 3))
            uerror ("literal pool out of reach of its load");
        hw = peekhalf (STEXT, poolrefs[i].ref);
        patchhalf (STEXT, poolrefs[i].ref, (hw & 0xff00) | (disp >> 2));
    }
    npool = 0;
    npoolref = 0;
    segm = savedsegm;
}

/*
 * Note a load from the pending pool. An identical constant already queued
 * shares its slot; the caller has emitted the placeholder load already.
 */
void
poolref(unsigned int value, int segment, unsigned int index, unsigned int ref)
{
    int i;

    for (i=0; i<npool; i++)
        if (pool[i].value == value && pool[i].segment == segment &&
            (segment != SEXT || pool[i].index == index))
            break;
    if (i == npool) {
        if (npool >= MAXPOOL)
            uerror ("literal pool overflow; add a .ltorg");
        pool[npool].value = value;
        pool[npool].segment = segment;
        pool[npool].index = index;
        pool[npool].at = 0;
        npool++;
    }
    if (npoolref >= MAXPOOL)
        uerror ("too many literal loads; add a .ltorg");
    if (npoolref == 0)
        poolfirst = ref;
    poolrefs[npoolref].ref = ref;
    poolrefs[npoolref].ent = i;
    npoolref++;
}

/*
 * Internal-only fixup formats. They never reach an object file: a literal
 * load and an ADR reach only 1020 bytes and must resolve inside the
 * section, so resolvefix() reports an error rather than emitting a record.
 */
#define RTPCLOAD    0x0c        /* ldr rd, [pc, #imm8*4] */
#define RTADR       0x0d        /* add rd, pc, #imm8*4 */

/*
 * Load and store encodings, indexed by the optable opcode field.
 */
const struct ldsttab {
    unsigned immbase;           /* base, #immediate form; 0 if absent */
    int scale;                  /* immediate scale for that form */
    unsigned regbase;           /* base, register offset form */
    unsigned spbase;            /* base, SP relative form; 0 if absent */
    int pcok;                   /* PC-relative literal load allowed */
} ldsttab [] = {
    { 0x6000, 4, 0x5000, 0x9000, 0 },   /* str */
    { 0x6800, 4, 0x5800, 0x9800, 1 },   /* ldr */
    { 0x7000, 1, 0x5400, 0,      0 },   /* strb */
    { 0x7800, 1, 0x5c00, 0,      0 },   /* ldrb */
    { 0x8000, 2, 0x5200, 0,      0 },   /* strh */
    { 0x8800, 2, 0x5a00, 0,      0 },   /* ldrh */
    { 0,      0, 0x5600, 0,      0 },   /* ldrsb */
    { 0,      0, 0x5e00, 0,      0 },   /* ldrsh */
};

/*
 * Special register numbers for MSR and MRS.
 */
const struct systab {
    const char *name;
    int num;
} systab [] = {
    { "apsr",    0 }, { "iapsr",  1 }, { "eapsr",   2 }, { "xpsr",    3 },
    { "ipsr",    5 }, { "epsr",   6 }, { "iepsr",   7 }, { "msp",     8 },
    { "psp",     9 }, { "primask", 16 }, { "control", 20 },
    { 0, 0 },
};

void
expect(int c, const char *what)
{
    int cval;

    if (getlex (&cval) != c)
        uerror ("%s expected", what);
}

/*
 * Read a register in operand position.
 */
int
getreg(void)
{
    int cval, r;

    if (getlex (&cval) != LNAME)
        uerror ("register expected");
    r = lookregname (name);
    if (r < 0)
        uerror ("bad register name %s", name);
    return r;
}

/*
 * Read a register that must be one of r0 through r7.
 */
int
getlow(void)
{
    int r = getreg ();

    if (r > 7)
        uerror ("r%d is not encodable here; ARMv6-M takes r0 to r7", r);
    return r;
}

/*
 * Read an immediate: an optional '#' and then an absolute expression.
 */
unsigned int
getimm(void)
{
    int clex, cval, s;

    clex = getlex (&cval);
    if (clex != '#')
        ungetlex (clex, cval);
    getexpr (&s);
    if (s != SABS)
        uerror ("absolute value required");
    return intval;
}

/*
 * Peek at the next lexeme without consuming it.
 */
int
peeklex(int *pval)
{
    int clex = getlex (pval);

    ungetlex (clex, *pval);
    return clex;
}

/*
 * Read a register list "{r0, r4-r7, lr}". Returns the low-register mask in
 * the low eight bits; bit 14 marks LR and bit 15 marks PC.
 */
unsigned int
getreglist(void)
{
    unsigned mask = 0;
    int clex, cval, r, r2, i;

    expect ('{', "{");
    for (;;) {
        clex = peeklex (&cval);
        if (clex == '}')
            break;
        r = getreg ();
        clex = getlex (&cval);
        if (clex == '-') {
            r2 = getreg ();
            if (r2 < r)
                uerror ("bad register range");
            for (i=r; i<=r2; i++)
                mask |= 1u << i;
            clex = getlex (&cval);
        } else
            mask |= 1u << r;
        if (clex != ',') {
            ungetlex (clex, cval);
            break;
        }
    }
    expect ('}', "}");
    if (mask == 0)
        uerror ("empty register list");
    return mask;
}

/*
 * Read a branch target and either resolve it here or leave a fixup.
 * 'at' is the offset of the instruction being encoded.
 */
void
branchtarget(unsigned int format, unsigned int at, int *resolved, int *disp)
{
    int s;
    unsigned value;

    value = getexpr (&s);
    if (s == SABS)
        uerror ("branch target must be a label");
    if (s == segm) {
        *resolved = 1;
        *disp = (int) value - (int) (at + 4);
        return;
    }
    *resolved = 0;
    *disp = 0;
    emitfix (format, s, (s == SEXT) ? (unsigned) extref : 0, at, (int) value);
}

/*
 * Build and emit one machine instruction.
 */
void
makecmd(unsigned int opcode, unsigned int type, int cond)
{
    unsigned at = count[segm];
    unsigned imm, mask, hw1, hw2;
    int rd, rn, rm, clex, cval, s, resolved, disp, i;
    const struct ldsttab *lst;

    switch (type) {
    case TNONE:
        emithalf (opcode);
        break;

    case TSHIFT:
        rd = getlow ();
        expect (',', "comma");
        rm = getlow ();
        clex = getlex (&cval);
        if (clex != ',') {
            /* "lsls rd, rm" shifts rd by the amount in rm. */
            ungetlex (clex, cval);
            emithalf ((0x4080 + ((opcode >> 11) << 6)) | (rm << 3) | rd);
            break;
        }
        clex = peeklex (&cval);
        if (clex == LNAME && lookregname (name) >= 0) {
            /* "lsls rd, rn, rm" shifts rd, so rn must repeat rd. */
            if (rd != rm)
                uerror ("ARMv6-M shifts by a register write their source");
            rm = getlow ();
            emithalf ((0x4080 + ((opcode >> 11) << 6)) | (rm << 3) | rd);
            break;
        }
        imm = getimm ();
        if (opcode == 0x0000) {
            /* LSL takes 0 to 31; a shift of 0 is the register move. */
            if (imm > 31)
                uerror ("shift amount out of range");
        } else {
            /* LSR and ASR take 1 to 32, and encode 32 as zero. */
            if (imm < 1 || imm > 32)
                uerror ("shift amount out of range");
            imm &= 31;
        }
        emithalf (opcode | (imm << 6) | (rm << 3) | rd);
        break;

    case TALU:
        rd = getlow ();
        expect (',', "comma");
        rm = getlow ();
        clex = getlex (&cval);
        if (clex == ',') {
            /* Three-operand spelling; the destination repeats. */
            if (rd != rm)
                uerror ("ARMv6-M data processing writes its first operand");
            rm = getlow ();
        } else
            ungetlex (clex, cval);
        emithalf (opcode | (rm << 3) | rd);
        break;

    case TNEG:
        rd = getlow ();
        expect (',', "comma");
        rm = getlow ();
        clex = getlex (&cval);
        if (clex == ',') {
            imm = getimm ();
            if (imm != 0)
                uerror ("rsbs takes an immediate of zero");
        } else
            ungetlex (clex, cval);
        emithalf (opcode | (rm << 3) | rd);
        break;

    case TCMN:
        rn = getlow ();
        expect (',', "comma");
        rm = getlow ();
        emithalf (opcode | (rm << 3) | rn);
        break;

    case TADD:
    case TSUB:
        rd = getreg ();
        expect (',', "comma");
        clex = peeklex (&cval);
        if (clex == '#') {
            /* "adds rd, #imm8", or "add sp, #imm7*4". */
            imm = getimm ();
            if (rd == 13) {
                if (imm > 508 || (imm & 3))
                    uerror ("stack adjustment out of range");
                emithalf ((type == TADD ? 0xb000 : 0xb080) | (imm >> 2));
            } else {
                if (rd > 7)
                    uerror ("r%d is not encodable here", rd);
                if (imm > 255)
                    uerror ("immediate out of range");
                emithalf ((type == TADD ? 0x3000 : 0x3800) | (rd << 8) | imm);
            }
            break;
        }
        rn = getreg ();
        clex = getlex (&cval);
        if (clex != ',') {
            /* "add rd, rm" -- the high-register form; sub has none. */
            ungetlex (clex, cval);
            if (type != TADD)
                uerror ("sub needs three operands");
            emithalf (0x4400 | ((rd & 8) << 4) | (rn << 3) | (rd & 7));
            break;
        }
        clex = peeklex (&cval);
        if (clex == '#') {
            imm = getimm ();
            if (rn == 13) {
                if (rd == 13) {
                    if (imm > 508 || (imm & 3))
                        uerror ("stack adjustment out of range");
                    emithalf ((type == TADD ? 0xb000 : 0xb080) | (imm >> 2));
                } else {
                    if (type != TADD)
                        uerror ("no sub from sp into a register");
                    if (rd > 7 || imm > 1020 || (imm & 3))
                        uerror ("add from sp out of range");
                    emithalf (0xa800 | (rd << 8) | (imm >> 2));
                }
                break;
            }
            if (rn == 15) {
                if (type != TADD)
                    uerror ("no sub from pc");
                if (rd > 7 || imm > 1020 || (imm & 3))
                    uerror ("add from pc out of range");
                emithalf (0xa000 | (rd << 8) | (imm >> 2));
                break;
            }
            if (rd > 7 || rn > 7)
                uerror ("r%d is not encodable here", rd > 7 ? rd : rn);
            /*
             * Both encodings fit when the destination repeats the source
             * and the immediate is small. GNU as takes the two-operand
             * eight-bit form there, and the assemblers must agree.
             */
            if (rd == rn && imm <= 255)
                emithalf ((type == TADD ? 0x3000 : 0x3800) | (rd << 8) | imm);
            else if (imm <= 7)
                emithalf ((type == TADD ? 0x1c00 : 0x1e00) |
                    (imm << 6) | (rn << 3) | rd);
            else
                uerror ("immediate out of range");
            break;
        }
        rm = getreg ();
        if (rd == 13 && rn == 13 && type == TADD) {
            emithalf (0x4400 | 0x80 | (rm << 3) | 5);
            break;
        }
        if (rd > 7 || rn > 7 || rm > 7) {
            if (type != TADD || rd != rn)
                uerror ("r%d is not encodable here", rd);
            emithalf (0x4400 | ((rd & 8) << 4) | (rm << 3) | (rd & 7));
            break;
        }
        emithalf ((type == TADD ? 0x1800 : 0x1a00) |
            (rm << 6) | (rn << 3) | rd);
        break;

    case TMOV:
        rd = getreg ();
        expect (',', "comma");
        clex = peeklex (&cval);
        if (clex == '#') {
            imm = getimm ();
            if (rd > 7)
                uerror ("r%d is not encodable here", rd);
            if (imm > 255)
                uerror ("immediate out of range");
            emithalf (0x2000 | (rd << 8) | imm);
            break;
        }
        rm = getreg ();
        if (opcode == 1 && rd <= 7 && rm <= 7) {
            /* movs between low registers is lsls rd, rm, #0. */
            /* lsls rd, rm, #0 is the low-register move. */
            emithalf ((rm << 3) | rd);
            break;
        }
        emithalf (0x4600 | ((rd & 8) << 4) | (rm << 3) | (rd & 7));
        break;

    case TCMP:
        rn = getreg ();
        expect (',', "comma");
        clex = peeklex (&cval);
        if (clex == '#') {
            imm = getimm ();
            if (rn > 7)
                uerror ("r%d is not encodable here", rn);
            if (imm > 255)
                uerror ("immediate out of range");
            emithalf (0x2800 | (rn << 8) | imm);
            break;
        }
        rm = getreg ();
        if (rn <= 7 && rm <= 7)
            emithalf (0x4280 | (rm << 3) | rn);
        else
            emithalf (0x4500 | ((rn & 8) << 4) | (rm << 3) | (rn & 7));
        break;

    case TBX:
        rm = getreg ();
        emithalf (opcode | (rm << 3));
        break;

    case TLDST:
        lst = &ldsttab [opcode];
        rd = getlow ();
        expect (',', "comma");
        clex = getlex (&cval);
        if (clex == '=') {
            /* Literal pool pseudo-instruction. */
            if (! lst->pcok)
                uerror ("only ldr takes a literal pool operand");
            expr_thumb = 0;
            imm = getexpr (&s);
            if (expr_thumb)
                imm |= 1;
            emithalf (0x4800 | (rd << 8));
            poolref (imm, s, (s == SEXT) ? (unsigned) extref : 0, at);
            break;
        }
        if (clex != '[') {
            /* "ldr rd, label" -- a PC-relative load from the section. */
            ungetlex (clex, cval);
            if (! lst->pcok)
                uerror ("only ldr takes a label operand");
            /*
             * A forward label is still undefined here, so the section
             * check waits for resolvefix, which knows the final segment.
             */
            imm = getexpr (&s);
            emithalf (0x4800 | (rd << 8));
            emitfix (RTPCLOAD, s, (s == SEXT) ? (unsigned) extref : 0,
                at, (int) imm);
            break;
        }
        rn = getreg ();
        clex = getlex (&cval);
        if (clex == ']') {
            /* "[rn]" is "[rn, #0]". */
            if (rn == 13 && lst->spbase)
                emithalf (lst->spbase | (rd << 8));
            else if (lst->immbase)
                emithalf (lst->immbase | (rn << 3) | rd);
            else
                uerror ("this load takes a register offset");
            break;
        }
        if (clex != ',')
            uerror ("comma or ] expected");
        clex = peeklex (&cval);
        if (clex == '#') {
            imm = getimm ();
            expect (']', "]");
            if (rn == 13) {
                if (! lst->spbase)
                    uerror ("no sp-relative form of this transfer");
                if (imm > 1020 || (imm & 3))
                    uerror ("sp offset out of range");
                emithalf (lst->spbase | (rd << 8) | (imm >> 2));
                break;
            }
            if (rn == 15) {
                if (! lst->pcok)
                    uerror ("no pc-relative form of this transfer");
                if (imm > 1020 || (imm & 3))
                    uerror ("pc offset out of range");
                emithalf (0x4800 | (rd << 8) | (imm >> 2));
                break;
            }
            if (! lst->immbase)
                uerror ("this load takes a register offset");
            if (rn > 7)
                uerror ("r%d is not encodable here", rn);
            if (imm % lst->scale)
                uerror ("offset is not a multiple of %d", lst->scale);
            if (imm / lst->scale > 31)
                uerror ("offset out of range");
            emithalf (lst->immbase | ((imm / lst->scale) << 6) |
                (rn << 3) | rd);
            break;
        }
        rm = getlow ();
        expect (']', "]");
        if (rn > 7)
            uerror ("r%d is not encodable here", rn);
        emithalf (lst->regbase | (rm << 6) | (rn << 3) | rd);
        break;

    case TADR:
        rd = getlow ();
        expect (',', "comma");
        imm = getexpr (&s);
        emithalf (0xa000 | (rd << 8));
        emitfix (RTADR, s, (s == SEXT) ? (unsigned) extref : 0,
            at, (int) imm);
        break;

    case TEXTEND:
    case TREV:
        rd = getlow ();
        expect (',', "comma");
        rm = getlow ();
        emithalf (opcode | (rm << 3) | rd);
        break;

    case TPUSHPOP:
        mask = getreglist ();
        if (opcode == 0xb400) {
            if (mask & ~0x40ffu)
                uerror ("push takes r0 to r7 and lr");
            emithalf (opcode | ((mask & 0x4000) ? 0x100 : 0) | (mask & 0xff));
        } else {
            if (mask & ~0x80ffu)
                uerror ("pop takes r0 to r7 and pc");
            emithalf (opcode | ((mask & 0x8000) ? 0x100 : 0) | (mask & 0xff));
        }
        break;

    case TSTMLDM:
        rn = getlow ();
        clex = getlex (&cval);
        if (clex != '!')
            ungetlex (clex, cval);
        expect (',', "comma");
        mask = getreglist ();
        if (mask & ~0xffu)
            uerror ("this transfer takes r0 to r7");
        emithalf (opcode | (rn << 8) | mask);
        break;

    case TBCOND:
        emithalf (0xd000 | (cond << 8));
        branchtarget (RTJUMP8, at, &resolved, &disp);
        if (resolved) {
            if (disp < -256 || disp > 254 || (disp & 1))
                uerror ("conditional branch out of range");
            patchhalf (segm, at, 0xd000 | (cond << 8) | ((disp >> 1) & 0xff));
        }
        break;

    case TB:
        emithalf (0xe000);
        branchtarget (RTJUMP11, at, &resolved, &disp);
        if (resolved) {
            if (disp < -2048 || disp > 2046 || (disp & 1))
                uerror ("branch out of range");
            patchhalf (segm, at, 0xe000 | ((disp >> 1) & 0x7ff));
        }
        break;

    case TBL:
        emithalf (0xf000);
        emithalf (0xf800);
        branchtarget (RTCALL, at, &resolved, &disp);
        if (resolved) {
            if (disp < -(1 << 22) || disp >= (1 << 22) || (disp & 1))
                uerror ("bl out of range");
            patchhalf (segm, at, 0xf000 | ((disp >> 12) & 0x7ff));
            patchhalf (segm, at + 2, 0xf800 | ((disp >> 1) & 0x7ff));
        }
        break;

    case TIMM8:
        imm = getimm ();
        if (imm > 255)
            uerror ("immediate out of range");
        emithalf (opcode | imm);
        break;

    case TMSR:
        if (getlex (&cval) != LNAME)
            uerror ("special register expected");
        for (i=0; systab[i].name; i++)
            if (! strcmp (systab[i].name, name))
                break;
        if (! systab[i].name)
            uerror ("unknown special register %s", name);
        expect (',', "comma");
        rn = getreg ();
        hw1 = 0xf380 | rn;
        hw2 = 0x8800 | systab[i].num;
        emithalf (hw1);
        emithalf (hw2);
        break;

    case TMRS:
        rd = getreg ();
        expect (',', "comma");
        if (getlex (&cval) != LNAME)
            uerror ("special register expected");
        for (i=0; systab[i].name; i++)
            if (! strcmp (systab[i].name, name))
                break;
        if (! systab[i].name)
            uerror ("unknown special register %s", name);
        emithalf (0xf3ef);
        emithalf (0x8000 | (rd << 8) | systab[i].num);
        break;

    case TBARRIER:
        clex = peeklex (&cval);
        imm = 15;
        if (clex == LNAME) {
            getlex (&cval);
            if (strcmp (name, "sy") != 0)
                uerror ("only the sy barrier option is encodable");
        } else if (clex == '#')
            imm = getimm ();
        emithalf ((opcode >> 16) & 0xffff);
        emithalf ((opcode & 0xfff0) | (imm & 15));
        break;

    case TCPS:
        if (getlex (&cval) != LNAME)
            uerror ("cps takes the i flag");
        if (strcmp (name, "i") != 0)
            uerror ("cps takes the i flag");
        emithalf (opcode);
        break;

    default:
        uerror ("internal error: unknown operand format");
    }
}

void
makeascii(int zeroterm)
{
    int c, nbytes;
    int cval;

    c = getlex (&cval);
    if (c != '"')
        uerror ("no string parameter");
    nbytes = 0;
    for (;;) {
        c = getchar ();
        switch (c) {
        case EOF:
            uerror ("EOF in text string");
        case '"':
            break;
        case '\\':
            c = getchar ();
            switch (c) {
            case EOF:
                uerror ("EOF in text string");
            case '\n':
                continue;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
                cval = c & 07;
                c = getchar ();
                if (c>='0' && c<='7') {
                    cval = (cval << 3) | (c & 7);
                    c = getchar ();
                    if (c>='0' && c<='7')
                        cval = (cval << 3) | (c & 7);
                    else
                        ungetc (c, stdin);
                } else
                    ungetc (c, stdin);
                c = cval;
                break;
            case 'x':
                cval = 0;
                for (;;) {
                    c = getchar ();
                    if (! ISHEX (c)) {
                        ungetc (c, stdin);
                        break;
                    }
                    cval = (cval << 4) | hexdig (c);
                }
                c = cval;
                break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'r': c = '\r'; break;
            case 'n': c = '\n'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case 'a': c = '\a'; break;
            }
            /* FALLTHROUGH */
        default:
            putc (c, sfile[segm]);
            nbytes++;
            continue;
        }
        break;
    }
    if (zeroterm) {
        putc (0, sfile[segm]);
        nbytes++;
    }
    add_space (nbytes, 0);
}

/*
 * Skip a string from the input file.
 */
void
skipstring(void)
{
    int c, cval;

    c = getlex (&cval);
    if (c != '"')
        uerror ("no string parameter");
    for (;;) {
        c = getchar ();
        switch (c) {
        case EOF:
            uerror ("EOF in text string");
        case '"':
            break;
        case '\\':
            c = getchar ();
            if (c == EOF)
                uerror ("EOF in text string");
            continue;
        default:
            continue;
        }
        break;
    }
}

/*
 * Skip the rest of the line. Directives that carry no meaning for an
 * a.out object -- unwind tables, CFI, EABI attributes -- end up here.
 */
void
skipline(void)
{
    int c;

    while ((c = getchar ()) != '\n' && c != EOF)
        ;
    if (c == '\n')
        ungetc (c, stdin);
}

/*
 * Emit the pending pool before it drifts beyond the reach of its oldest
 * load. A PC-relative load reads at most 1020 bytes forward, and the
 * check leaves room for the alignment and the instruction in hand.
 */
void
poolcheck(void)
{
    if (npoolref > 0 && segm == STEXT &&
        count[STEXT] - poolfirst > 900)
        ltorg ();
}

void
pass1(void)
{
    int clex;
    int cval, tval, csegm, nbytes;
    unsigned addr;

    segm = STEXT;
    for (;;) {
        clex = getlex (&cval);
        switch (clex) {
        case LEOF:
done:       segm = STEXT;
            ltorg ();
            align (2);
            segm = SDATA;
            align (2);
            segm = SSTRNG;
            align (2);
            segm = SBSS;
            align (2);
            return;
        case LEOL:
            continue;
        case ':':
            continue;
        case '.':
            if (getlex (&cval) != '=')
                uerror ("bad instruction");
            addr = getexpr (&csegm);
            if (csegm != segm)
                uerror ("bad count assignment");
            if (addr < count[segm])
                uerror ("negative count increment");
            if (segm == SBSS)
                count [segm] = addr;
            else
                add_space (addr - count[segm], 1);
            break;
        case LNAME:
            /*
             * The mnemonic must be looked up before the next lexeme is
             * read, because getlex overwrites name[]. A colon or an equals
             * sign never calls getname, so lookname below still sees it.
             */
            cval = lookcmd (&tval);
            csegm = tval;
            clex = getlex (&tval);
            if (clex == ':') {
                /* Label. */
                cval = lookname();
                stab[cval].n_value = count[segm];
                stab[cval].n_type &= ~N_TYPE;
                stab[cval].n_type |= segmtype [segm];
                if (lastthumbfunc >= 0) {
                    stab[cval].n_type |= N_THUMB;
                    lastthumbfunc = -1;
                }
                continue;
            } else if (clex == '=') {
                /* Symbol definition. */
                cval = lookname();
                stab[cval].n_value = getexpr (&csegm);
                if (csegm == SEXT)
                    uerror ("indirect equivalence");
                stab[cval].n_type &= N_EXT;
                stab[cval].n_type |= segmtype [csegm];
                break;
            }
            /* Machine instruction. */
            ungetlex (clex, tval);
            if (cval < 0)
                uerror ("bad instruction");
            if (csegm >= 0 && optable[cval].type == TB) {
                /* A condition suffix turns b into the conditional form. */
                if (csegm == 14)
                    makecmd (0, TB, -1);
                else
                    makecmd (0, TBCOND, csegm);
            } else
                makecmd (optable[cval].opcode, optable[cval].type, csegm);
            poolcheck ();
            break;
        case LNUM:
            /* Local label. */
            if (nlabels >= MAXRLAB)
                uerror ("too many digital labels");
            labeltab[nlabels].num = intval;
            labeltab[nlabels].value = count[segm];
            ++nlabels;
            if (getlex (&tval) != ':')
                uerror ("bad digital label");
            continue;
        case LTEXT:
            segm = STEXT;
            break;
        case LDATA:
            ltorg ();
            segm = SDATA;
            break;
        case LSTRNG:
        case LRDATA:
            ltorg ();
            segm = SSTRNG;
            break;
        case LBSS:
            ltorg ();
            segm = SBSS;
            break;
        case LWORD:
            align (2);
            for (;;) {
                expr_thumb = 0;
                getexpr (&cval);
                if (expr_thumb)
                    intval |= 1;
                if (cval == SABS)
                    emitword (intval);
                else {
                    emitfix (RTABS32, cval,
                        (cval == SEXT) ? (unsigned) extref : 0,
                        count[segm], (int) intval);
                    emitword (0);
                }
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            break;
        case LBYTE:
            nbytes = 0;
            for (;;) {
                getexpr (&cval);
                if (cval != SABS)
                    uerror (".byte takes an absolute value");
                putc (intval, sfile[segm]);
                nbytes++;
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            add_space (nbytes, 0);
            break;
        case LHALF:
            align (1);
            nbytes = 0;
            for (;;) {
                getexpr (&cval);
                if (cval != SABS)
                    uerror (".hword takes an absolute value");
                putc (intval, sfile[segm]);
                putc (intval >> 8, sfile[segm]);
                nbytes += 2;
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            add_space (nbytes, 0);
            break;
        case LSPACE:
            /* .space num[,fill] */
            getexpr (&cval);
            nbytes = intval;
            clex = getlex (&cval);
            if (clex == ',')
                getexpr (&cval);
            else
                ungetlex (clex, cval);
            add_space (nbytes, 1);
            break;
        case LALIGN:
        case LP2ALIGN:
            /* .align num -- a power of two on ARM, as on MIPS. */
            clex = getlex (&cval);
            if (clex != LNUM) {
                ungetlex (clex, cval);
                alignfill (2, 1);
                break;
            }
            alignfill (intval, 1);
            /* An optional fill value and maximum skip are ignored. */
            clex = getlex (&cval);
            if (clex == ',')
                skipline ();
            else
                ungetlex (clex, cval);
            break;
        case LASCII:
            makeascii (0);
            break;
        case LASCIZ:
            makeascii (1);
            break;
        case LGLOBL:
            for (;;) {
                if (getlex (&cval) != LNAME)
                    uerror ("bad parameter of .global");
                cval = lookname();
                if (stab[cval].n_type & N_LOC)
                    uerror ("local name redefined as global");
                stab[cval].n_type |= N_EXT;
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            break;
        case LLOCAL:
            for (;;) {
                if (getlex (&cval) != LNAME)
                    uerror ("bad parameter of .local");
                cval = lookname();
                if (stab[cval].n_type & N_EXT)
                    uerror ("global name redefined as local");
                stab[cval].n_type |= N_LOC;
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            break;
        case LWEAK:
            for (;;) {
                if (getlex (&cval) != LNAME)
                    uerror ("bad parameter of .weak");
                cval = lookname();
                stab[cval].n_type |= N_WEAK;
                clex = getlex (&cval);
                if (clex != ',') {
                    ungetlex (clex, cval);
                    break;
                }
            }
            break;
        case LEQU:
        case LSET:
            /* .equ name,value and .set name,value */
            if (getlex (&cval) != LNAME)
                uerror ("bad parameter of .set");
            cval = lookname();
            clex = getlex (&tval);
            if (clex != ',')
                uerror ("bad value of .set");
            stab[cval].n_value = getexpr (&csegm);
            if (csegm == SEXT)
                uerror ("indirect equivalence");
            stab[cval].n_type &= N_EXT;
            stab[cval].n_type |= segmtype [csegm];
            break;
        case LCOMM:
        case LLCOMM:
            /* .comm name,len[,alignment] */
            if (getlex (&cval) != LNAME)
                uerror ("bad parameter of .comm");
            cval = lookname();
            if (stab[cval].n_type != N_UNDF &&
                stab[cval].n_type != N_LOC &&
                (stab[cval].n_type & N_TYPE) != N_COMM)
                uerror ("name already defined");
            if (clex == LLCOMM || (stab[cval].n_type & N_LOC))
                stab[cval].n_type = N_COMM;
            else
                stab[cval].n_type = N_EXT | N_COMM;
            clex = getlex (&tval);
            if (clex == ',') {
                getexpr (&tval);
                if (tval != SABS)
                    uerror ("bad length of .comm");
            } else {
                ungetlex (clex, cval);
                intval = 1;
            }
            stab[cval].n_value = intval;
            clex = getlex (&cval);
            if (clex != ',') {
                ungetlex (clex, cval);
                break;
            }
            getexpr (&tval);
            if (tval != SABS)
                uerror ("bad .comm alignment");
            break;
        case LFILE:
            /* .file ["name" | num "name"] */
            clex = getlex (&cval);
            if (clex == LNUM)
                skipstring ();
            else if (clex == '"') {
                ungetlex (clex, cval);
                skipstring ();
            } else
                ungetlex (clex, cval);
            break;
        case LIDENT:
            skipstring();
            break;
        case LSECTION:
            /* .section name[,"flags"[,%type[,entsize]]] */
            clex = getlex (&cval);
            if (clex != LNAME && clex != LBSS && clex != LTEXT &&
                clex != LDATA && clex != LRDATA)
                uerror ("bad name of .section");
            if (clex == LBSS)  strcpy (name, ".bss");
            if (clex == LTEXT) strcpy (name, ".text");
            if (clex == LDATA) strcpy (name, ".data");
            if (clex == LRDATA) strcpy (name, ".rodata");
            setsection();
            skipline ();
            break;
        case LPREVIOUS:
            break;
        case LTHUMB:
            /* .thumb, .code 16 -- the only state this target has. */
            clex = getlex (&cval);
            if (clex == LNUM) {
                if (intval != 16)
                    uerror ("only Thumb code is encodable on ARMv6-M");
            } else
                ungetlex (clex, cval);
            break;
        case LTHUMBFUNC:
            /* The next label names a Thumb function. */
            lastthumbfunc = 1;
            break;
        case LSYNTAX:
            clex = getlex (&cval);
            if (clex != LNAME || strcmp (name, "unified") != 0)
                uerror ("only unified syntax is accepted");
            break;
        case LCPU:
        case LEABIATTR:
        case LSKIPLINE:
            skipline ();
            break;
        case LLTORG:
            ltorg ();
            break;
        case LTYPE:
            /* .type name,%type */
            if (getlex (&cval) != LNAME)
                uerror ("bad name of .type");
            cval = lookname ();
            clex = getlex (&tval);
            if (clex != ',') {
                ungetlex (clex, tval);
                break;
            }
            clex = getlex (&tval);
            if (clex == LSYMTYPE) {
                if (! strcmp (name, "%function"))
                    stab[cval].n_type |= N_THUMB;
            } else if (clex != LNAME)
                uerror ("bad type of .type");
            break;
        case LSIZE:
            /* .size name,expr */
            if (getlex (&cval) != LNAME)
                uerror ("bad name of .size");
            clex = getlex (&cval);
            if (clex != ',') {
                ungetlex (clex, cval);
                break;
            }
            getexpr (&csegm);
            break;
        default:
            uerror ("bad syntax");
        }
        clex = getlex (&cval);
        if (clex == LEOF)
            goto done;
        if (clex != LEOL)
            uerror ("bad instruction arguments");
    }
}

/*
 * Discard everything pass1 emitted, keeping the symbol table, and rewind
 * the input so pass1 can run again.
 *
 * GCC's -Os switch tables read "(.Lfwd - .Lhere)/2" into a .byte, a
 * difference of two labels that is constant but unknown while .Lfwd is
 * still ahead of the cursor; getexpr rejects it as a subtraction of an
 * undefined symbol. Thumb-1 has no relaxation and this assembler narrows
 * nothing, so every instruction's size is the same on both runs and the
 * label values the first run computes are the values the second run
 * assembles against.
 */
void
rescan(void)
{
    int i;

    for (i=STEXT; i<SBSS; i++) {
        rewind (sfile[i]);
        if (ftruncate (fileno (sfile[i]), (off_t) 0) != 0)
            uerror ("cannot rewind a scratch file");
        rewind (rfile[i]);
        if (ftruncate (fileno (rfile[i]), (off_t) 0) != 0)
            uerror ("cannot rewind a scratch file");
        count[i] = 0;
        nfixup[i] = 0;
    }
    count[SBSS] = 0;
    nlabels = 0;
    npool = 0;
    npoolref = 0;
    lastthumbfunc = -1;
    blexflag = 0;
    segm = STEXT;
    line = 1;
    rewind (stdin);
}

/*
 * Find the relative label address, by the reference address and the
 * label number. Backward references have negative label numbers.
 */
int
findlabel(int addr, int sym)
{
    struct labeltab *p;

    if (sym < 0) {
        for (p=labeltab+nlabels-1; p>=labeltab; --p)
            if (p->value <= addr && p->num == -sym)
                return (p->value);
        uerror ("undefined label %db at address %d", -sym, addr);
    } else {
        for (p=labeltab; p<labeltab+nlabels; ++p)
            if (p->value > addr && p->num == sym)
                return (p->value);
        uerror ("undefined label %df at address %d", sym, addr);
    }
    return (0);
}

/*
 * Turn each fixup into either a patched instruction or a relocation.
 * A target in the same segment resolves here, because the segment bases
 * cancel in a PC-relative displacement; everything else survives as a
 * record for ld.
 */
void
resolvefix(void)
{
    struct fixup fx;
    struct reloc rel;
    struct nlist *sym;
    unsigned i, s, value, hw;
    int tsegm, disp;
    FILE *rfd;

    for (s=STEXT; s<SBSS; s++) {
        rfd = fopen (tfilename, "w+");
        if (! rfd)
            uerror ("cannot open %s", tfilename);
        unlink (tfilename);
        rewind (rfile[s]);
        segm = s;
        for (i=0; i<nfixup[s]; i++) {
            fgetfix (rfile[s], &fx);
            sym = 0;
            value = (unsigned) fx.addend;
            if ((fx.flags & RSMASK) == REXT) {
                if (fx.index >= RLAB_OFFSET - RLAB_MAXVAL) {
                    /* A numeric label is always in the current segment. */
                    value += findlabel (fx.addr, (int) fx.index - RLAB_OFFSET);
                    tsegm = s;
                } else {
                    if (fx.index >= (unsigned) stabfree)
                        uerror ("internal error: symbol index %u out of range",
                            fx.index);
                    sym = &stab[fx.index];
                    if (sym->n_type == N_EXT+N_UNDF ||
                        sym->n_type == N_EXT+N_COMM ||
                        (sym->n_type & N_TYPE) == N_UNDF ||
                        (sym->n_type & N_TYPE) == N_COMM)
                        tsegm = SEXT;
                    else {
                        value += sym->n_value;
                        tsegm = typesegm [sym->n_type & N_TYPE];
                        /*
                         * Folding a defined symbol into a segment record
                         * drops its identity, so the Thumb bit that lives
                         * in the symbol value has to move into the stored
                         * address here. GNU as keeps the record against
                         * the symbol and carries the bit in its value; the
                         * linked result is the same odd address. Only an
                         * absolute word takes it -- a branch displacement
                         * is computed from the even address.
                         */
                        if ((fx.flags & RTFMASK) == RTABS32 &&
                            (sym->n_type & N_THUMB) &&
                            (sym->n_type & N_TYPE) == N_TEXT)
                            value |= 1;
                    }
                }
            } else {
                /* The record already names a segment. */
                switch (fx.flags & RSMASK) {
                case RTEXT:  tsegm = STEXT;  break;
                case RDATA:  tsegm = SDATA;  break;
                case RSTRNG: tsegm = SSTRNG; break;
                case RBSS:   tsegm = SBSS;   break;
                default:     tsegm = SABS;   break;
                }
            }

            switch (fx.flags & RTFMASK) {
            case RTPCLOAD:
            case RTADR:
                if (tsegm != (int) s || s != STEXT)
                    uerror ("PC-relative reference leaves its section");
                disp = (int) value - (int) ((fx.addr + 4) & ~3u);
                if (disp < 0 || disp > 1020 || (disp & 3))
                    uerror ("PC-relative reference out of range");
                hw = peekhalf (s, fx.addr);
                patchhalf (s, fx.addr, (hw & 0xff00) | (disp >> 2));
                continue;

            case RTJUMP8:
            case RTJUMP11:
            case RTCALL:
                if (tsegm == (int) s) {
                    disp = (int) value - (int) (fx.addr + 4);
                    hw = peekhalf (s, fx.addr);
                    if ((fx.flags & RTFMASK) == RTJUMP8) {
                        if (disp < -256 || disp > 254 || (disp & 1))
                            uerror ("conditional branch out of range");
                        patchhalf (s, fx.addr,
                            (hw & 0xff00) | ((disp >> 1) & 0xff));
                    } else if ((fx.flags & RTFMASK) == RTJUMP11) {
                        if (disp < -2048 || disp > 2046 || (disp & 1))
                            uerror ("branch out of range");
                        patchhalf (s, fx.addr, 0xe000 | ((disp >> 1) & 0x7ff));
                    } else {
                        if (disp < -(1 << 22) || disp >= (1 << 22) ||
                            (disp & 1))
                            uerror ("bl out of range");
                        patchhalf (s, fx.addr,
                            0xf000 | ((disp >> 12) & 0x7ff));
                        patchhalf (s, fx.addr + 2,
                            0xf800 | ((disp >> 1) & 0x7ff));
                    }
                    continue;
                }
                /*
                 * A cross-segment or external target keeps its addend in
                 * the instruction, where ld reads it back.
                 */
                if ((fx.flags & RTFMASK) == RTCALL) {
                    patchhalf (s, fx.addr,
                        0xf000 | (((int) value >> 12) & 0x7ff));
                    patchhalf (s, fx.addr + 2,
                        0xf800 | (((int) value >> 1) & 0x7ff));
                } else if ((fx.flags & RTFMASK) == RTJUMP11) {
                    patchhalf (s, fx.addr,
                        0xe000 | (((int) value >> 1) & 0x7ff));
                } else {
                    hw = peekhalf (s, fx.addr);
                    patchhalf (s, fx.addr,
                        (hw & 0xff00) | (((int) value >> 1) & 0xff));
                }
                break;

            case RTABS32:
                /* The addend lives in the word itself, as a .word does. */
                patchhalf (s, fx.addr, value & 0xffff);
                patchhalf (s, fx.addr + 2, value >> 16);
                break;

            default:
                uerror ("internal error: bad fixup format");
            }

            rel.flags = (segmrel [tsegm] & RSMASK) | (fx.flags & RTFMASK);
            rel.addr = fx.addr;
            rel.index = (tsegm == SEXT) ? fx.index : 0;
            rel.offset = 0;
            fputrel (&rel, rfd);
            nreloc[s]++;
        }
        fclose (rfile[s]);
        rfile[s] = rfd;
    }
    segm = STEXT;
}

/*
 * Read a Thumb sparse relocation record.
 */
void
fgetrel(FILE *f, struct reloc *r)
{
    r->flags = getc (f);
    r->addr = getc (f);
    r->addr |= getc (f) << 8;
    r->addr |= getc (f) << 16;
    r->addr |= (unsigned) getc (f) << 24;
    r->offset = 0;
    r->index = 0;
    if ((r->flags & RSMASK) == REXT) {
        r->index = getc (f);
        r->index |= getc (f) << 8;
        r->index |= getc (f) << 16;
    }
}

void
middle(void)
{
    int i, snum, nbytes;

    stlength = 0;
    for (snum=0, i=0; i<stabfree; i++) {
        switch (stab[i].n_type & ~N_THUMB) {
        case N_UNDF:
            /* Without -u option, undefined symbol is considered external */
            if (uflag)
                uerror ("%s: name undefined", stab[i].n_name);
            stab[i].n_type |= N_EXT;
            break;
        case N_COMM:
            /* Allocate a local common block */
            count[SBSS] = (count[SBSS] + WORDSZ-1) & ~(WORDSZ-1);
            nbytes = stab[i].n_value;
            stab[i].n_value = count[SBSS];
            stab[i].n_type = N_BSS;
            count[SBSS] += nbytes;
            break;
        }
        if (xflags)
            newindex[i] = snum;

        if (! xflags || (stab[i].n_type & N_EXT) ||
            (Xflag && ! IS_LOCAL(&stab[i])))
        {
            stlength += 2 + WORDSZ + stab[i].n_len;
            snum++;
        }
    }
    stalign = WORDSZ - stlength % WORDSZ;
    stlength += stalign;
    line = 0;
}

void
makeheader(unsigned int rtsize, unsigned int rdsize)
{
    struct exec hdr;

    count[SBSS] = (count[SBSS] + WORDSZ-1) & ~(WORDSZ-1);

    hdr.a_midmag = RMAGIC | (MID_ARM6 << 16);
    hdr.a_text = count [STEXT];
    hdr.a_data = count [SDATA] + count [SSTRNG];
    hdr.a_bss = count [SBSS];
    hdr.a_reltext = rtsize;
    hdr.a_reldata = rdsize;
    hdr.a_syms = stlength;
    hdr.a_entry = 0;
    fseek (stdout, 0, 0);
    fputhdr (&hdr, stdout);
}

/*
 * Read and patch a 32-bit word in a segment scratch file.
 */
unsigned int
peekword(int s, unsigned int addr)
{
    return peekhalf (s, addr) | (peekhalf (s, addr + 2) << 16);
}

void
pokeword(int s, unsigned int addr, unsigned int w)
{
    patchhalf (s, addr, w & 0xffff);
    patchhalf (s, addr + 2, w >> 16);
}

/*
 * Second pass. Symbol values and every stored absolute address move from
 * segment-relative to image-relative coordinates, which is what ld expects
 * to add its own origin to. The relocation stream is rewritten in place:
 * a record naming a symbol that turned out to be defined becomes a segment
 * record, and the string pseudo-segment folds into data.
 */
void
pass2(void)
{
    int i, s;
    unsigned h, w, delta;
    struct reloc rel;
    struct nlist *sym;
    FILE *rfd;
    int c;

    tbase = 0;
    dbase = tbase + count[STEXT];
    adbase = dbase + count[SDATA];
    bbase = adbase + count[SSTRNG];

    for (i=0; i<stabfree; i++) {
        switch (stab[i].n_type & N_TYPE) {
        case N_UNDF:
        case N_ABS:
            break;
        case N_TEXT:
            stab[i].n_value += tbase;
            break;
        case N_DATA:
            stab[i].n_value += dbase;
            break;
        case N_STRNG:
            stab[i].n_value += adbase;
            stab[i].n_type += N_DATA - N_STRNG;
            break;
        case N_BSS:
            stab[i].n_value += bbase;
            break;
        }
    }

    for (s=STEXT; s<SBSS; s++) {
        rfd = fopen (tfilename, "w+");
        if (! rfd)
            uerror ("cannot open %s", tfilename);
        unlink (tfilename);
        rewind (rfile[s]);
        relbytes[s] = 0;
        for (h=0; h<nreloc[s]; h++) {
            fgetrel (rfile[s], &rel);
            sym = 0;
            delta = 0;
            switch (rel.flags & RSMASK) {
            case RTEXT:  delta = tbase;  break;
            case RDATA:  delta = dbase;  break;
            case RSTRNG: delta = adbase; break;
            case RBSS:   delta = bbase;  break;
            case REXT:
                sym = &stab[rel.index];
                if (sym->n_type == N_EXT+N_UNDF ||
                    sym->n_type == N_EXT+N_COMM) {
                    /* Still external; reindex and leave the addend. */
                    if (xflags)
                        rel.index = newindex [rel.index];
                    sym = 0;
                } else {
                    delta = sym->n_value;
                    rel.flags &= RTFMASK;
                    switch (sym->n_type & N_TYPE) {
                    case N_TEXT: rel.flags |= RTEXT; break;
                    case N_DATA: rel.flags |= RDATA; break;
                    case N_BSS:  rel.flags |= RBSS;  break;
                    case N_ABS:  rel.flags |= RABS;  break;
                    default:     rel.flags |= RABS;  break;
                    }
                }
                break;
            default:
                break;
            }
            if (delta != 0) {
                switch (rel.flags & RTFMASK) {
                case RTABS32:
                    w = peekword (s, rel.addr);
                    pokeword (s, rel.addr, w + delta);
                    break;
                default:
                    /*
                     * A PC-relative record still names a symbol whose final
                     * address ld computes, so only the segment moves here.
                     */
                    break;
                }
            }
            /* The string pseudo-segment is part of data in the object. */
            if ((rel.flags & RSMASK) == RSTRNG) {
                rel.flags &= ~RSMASK;
                rel.flags |= RDATA;
            }
            if (s == SSTRNG)
                rel.addr += count[SDATA];
            relbytes[s] += fputrel (&rel, rfd);
        }
        fclose (rfile[s]);
        rfile[s] = rfd;
    }

    /* Emit the segments. */
    fseek (stdout, sizeof(struct exec), 0);
    for (s=STEXT; s<SBSS; s++) {
        rewind (sfile[s]);
        for (h=0; h<count[s]; h++) {
            c = getc (sfile[s]);
            putchar (c == EOF ? 0 : c);
        }
    }
}

/*
 * Copy the relocation records of a segment to the output.
 */
unsigned int
makereloc(int s)
{
    unsigned i;
    int c;

    if (relbytes[s] == 0)
        return 0;
    rewind (rfile[s]);
    for (i=0; i<relbytes[s]; i++) {
        c = getc (rfile[s]);
        putchar (c == EOF ? 0 : c);
    }
    return relbytes[s];
}

/*
 * Align the relocation section to an integral number of words.
 */
unsigned int
alignreloc(unsigned int nbytes)
{
    while (nbytes % WORDSZ) {
        putchar (0);
        nbytes++;
    }
    return nbytes;
}

void
makesymtab(void)
{
    int i;

    for (i=0; i<stabfree; i++) {
        if (! xflags || (stab[i].n_type & N_EXT) ||
            (Xflag && ! IS_LOCAL(&stab[i])))
        {
            fputsym (&stab[i], stdout);
        }
    }
    while (stalign--)
        putchar (0);
}

void
usage(void)
{
    fprintf (stderr, "Usage:\n");
    fprintf (stderr, "  as [-uxX] [-o outfile] [infile]\n");
    fprintf (stderr, "Options:\n");
    fprintf (stderr, "  -o filename     Set output file name, default a.out\n");
    fprintf (stderr, "  -u              Treat undefined names as error\n");
    fprintf (stderr, "  -x              Discard local symbols\n");
    fprintf (stderr, "  -X              Discard locals starting with 'L' or '.'\n");
    exit (1);
}

int
main(int argc, char *argv[])
{
    int i;
    char *cp;
    int ofile = 0;
    unsigned rtsize, rdsize;

    for (i=1; i<argc; i++) {
        switch (argv[i][0]) {
        case '-':
            if (argv[i][1] == 0)
                break;
            for (cp=argv[i]+1; *cp; cp++) {
                switch (*cp) {
                case 'X':       /* strip L* and .* locals */
                    Xflag++;
                    /* FALLTHROUGH */
                case 'x':       /* strip local symbols */
                    xflags++;
                    break;
                case 'u':       /* treat undefines as error */
                    uflag++;
                    break;
                case 'o':       /* output file name */
                    if (ofile)
                        uerror ("too many -o flags");
                    ofile = 1;
                    if (cp [1]) {
                        outfile = cp+1;
                        while (*++cp);
                        --cp;
                    } else if (i+1 < argc)
                        outfile = argv[++i];
                    break;
                case 'v':       /* verbose */
                case 'g':       /* debug */
                    break;
                case 'I':       /* include dir */
                    if (cp[1] == 0)
                        i++;
                    else {
                        while (*++cp);
                        --cp;
                    }
                    break;
                case 'O':       /* optimization level */
                case '-':       /* long option */
                case 'n':       /* -no-xyz */
                case 'm':       /* -mcpu=, -mthumb, -march= */
                case 'E':       /* -EL */
                case 'W':       /* warning control */
                case 'a':       /* listing control */
                case 'k':       /* PIC */
                case 'f':       /* fast */
                    while (*++cp);
                    --cp;
                    break;
                default:
                    fprintf (stderr, "Unknown option: %s\n", cp);
                    usage();
                }
            }
            break;
        default:
            if (infile)
                uerror ("too many input files");
            infile = argv[i];
            break;
        }
    }
    if (! infile && isatty(0))
        usage();

    if (infile && ! freopen (infile, "r", stdin))
        uerror ("cannot open %s", infile);
    if (! freopen (outfile, "w", stdout))
        uerror ("cannot open %s", outfile);

    startup ();                         /* Open temporary files */
    hashinit ();                        /* Initialize hash tables */
    prescan = 1;
    pass1 ();                           /* Learn every label's address */
    prescan = 0;
    rescan ();                          /* Discard the output, keep symbols */
    pass1 ();                           /* Assemble against known labels */
    resolvefix ();                      /* Patch or relocate each reference */
    middle ();                          /* Prepare symbol table */
    pass2 ();                           /* Second pass */
    rtsize = makereloc (STEXT);         /* Emit relocation info: text */
    rtsize = alignreloc (rtsize);
    rdsize = makereloc (SDATA);         /* data */
    rdsize += makereloc (SSTRNG);       /* rodata */
    rdsize = alignreloc (rdsize);
    makesymtab ();                      /* Emit symbol table */
    makeheader (rtsize, rdsize);        /* Write a.out header */
    return 0;
}
