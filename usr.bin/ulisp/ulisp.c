/* uLisp PicoCalc Release 4.8f - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 14th October 2025

   Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

/*
 * This file is the interpreter of uLisp PicoCalc 4.8f as a C17 program for
 * DiscoBSD and POSIX hosts. It derives from ulisp-picocalc.ino of
 * https://github.com/technoblogy/ulisp-picocalc at commit
 * c031b63c00b1e55ef64e556fd3372e38ed0ab062 (SHA-256 65608bb7d6a48dc0
 * 2dd38f67d10a826bc7a60456e106c4b7212a2dd0483ebd8b) and stays under the MIT
 * license above, whose text NOTICE reproduces.
 *
 * The evaluator, reader, printer and builtins are the original's. The parts
 * that met the Arduino core, the display, keyboard and SD libraries are
 * replaced by the calls of platform.h, and these mechanisms are the port's:
 *
 *   - the workspace is allocated at startup, so WORKSPACESIZE is a variable;
 *   - the builtin table is builtins.def, and the documentation strings are
 *     a file that (documentation) reads, not resident data;
 *   - markobject() reverses pointers instead of recursing, and every
 *     recursive C function begins with checkstack();
 *   - an image records the workspace address it was saved from and
 *     loadimage() rebases it;
 *   - cells, symbols and string words are pointer-sized, so one source
 *     serves 32-bit and 64-bit hosts;
 *   - the Wi-Fi, I2C, SPI, extra serial port, analogue output and display
 *     rotation functions are absent, with the devices they need.
 */

// Configuration

#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "platform.h"

enum { COLOR_WHITE = 0xffff, COLOR_BLACK = 0 };

#define WORDALIGNED __attribute__((aligned (4)))
#define BUFFERSIZE 36  // Number of bits+4
#define PATHSIZE 64    // Longest file name with its terminator
#define CODESIZE 256   /* Bytes */
#define WORKSPACESIZE WorkspaceSize    /* Objects, chosen at startup */

// C Macros

#define nil                NULL
#define car(x)             (((object *) (x))->car)
#define cdr(x)             (((object *) (x))->cdr)

#define first(x)           car(x)
#define rest(x)            cdr(x)
#define second(x)          first(rest(x))
#define cddr(x)            cdr(cdr(x))
#define third(x)           first(cddr(x))

#define push(x, y)         ((y) = cons((x),(y)))
#define pop(y)             ((y) = cdr(y))

#define protect(y)         push((y), GCStack)
#define unprotect()        pop(GCStack)

#define integerp(x)        ((x) != NULL && (x)->type == NUMBER)
#define floatp(x)          ((x) != NULL && (x)->type == FLOAT)
#define symbolp(x)         ((x) != NULL && (x)->type == SYMBOL)
#define stringp(x)         ((x) != NULL && (x)->type == STRING)
#define characterp(x)      ((x) != NULL && (x)->type == CHARACTER)
#define arrayp(x)          ((x) != NULL && (x)->type == ARRAY)
#define streamp(x)         ((x) != NULL && (x)->type == STREAM)

#define mark(x)            (car(x) = (object *)(((uintptr_t)(car(x))) | MARKBIT))
#define unmark(x)          (car(x) = (object *)(((uintptr_t)(car(x))) & ~MARKBIT))
#define marked(x)          ((((uintptr_t)(car(x))) & MARKBIT) != 0)
#define MARKBIT            1
#define VISITBIT           2  // With MARKBIT, while markobject() traces a cdr

#define setflag(x)         (Flags = Flags | 1<<(x))
#define clrflag(x)         (Flags = Flags & ~(1<<(x)))
#define tstflag(x)         (Flags & 1<<(x))

#define issp(x)            (x == ' ' || x == '\n' || x == '\r' || x == '\t')
#define isbr(x)            (x == ')' || x == '(' || x == '"' || x == '#' || x == '\'')
#define fntype(x)          (getminmax((uint16_t)(x))>>6)
#define longsymbolp(x)     (((x)->name & 0x03) == 0)
#define longnamep(x)       (((x) & 0x03) == 0)
#define twist(x)           ((uint32_t)((x)<<2) | (((x) & 0xC0000000)>>30))
#define untwist(x)         (((x)>>2 & 0x3FFFFFFF) | ((x) & 0x03)<<30)
#define arraysize(x)       (sizeof(x) / sizeof(x[0]))
// Every function that can call itself, directly or through others, starts with this
#define checkstack()       if ((uintptr_t)__builtin_frame_address(0) < StackLimit) stackoverflow()
#define stringifyX(x)      #x
#define stringify(x)       stringifyX(x)
#define PACKEDS            0x43238000
#define BUILTINS           0xF4240000
#define ENDFUNCTIONS       0x0BDC0000

// Code marker stores start and end of code block
#define startblock(x)      ((x->integer) & 0xFFFF)
#define endblock(x)        ((x->integer) >> 16 & 0xFFFF)

// Constants

#define USERSTREAMS        16
#define TRACEMAX 3  // Maximum number of traced functions
enum type { ZZERO=0, SYMBOL=2, CODE=4, NUMBER=6, STREAM=8, CHARACTER=10, FLOAT=12, ARRAY=14, STRING=16, PAIR=18 };  // ARRAY STRING and PAIR must be last
enum stream { SERIALSTREAM, SDSTREAM, STRINGSTREAM, GFXSTREAM };
enum token { UNUSED, BRA, KET, QUO, DOT, ENDOFINPUT };
enum fntypes_t { OTHER_FORMS, TAIL_FORMS, FUNCTIONS, SPECIAL_FORMS };

// Typedefs

typedef uintptr_t symbol_t;
typedef uint32_t builtin_t;
typedef uint32_t chars_t;

typedef struct sobject {
  union {
    struct {
      struct sobject *car;
      struct sobject *cdr;
    };
    struct {
      uintptr_t type;
      union {
        symbol_t name;
        int integer;
        uintptr_t chars; // For strings: four characters; a whole word, so that zero is also an empty cdr
        float single_float;
      };
    };
  };
} object;

typedef object *(*fn_ptr_type)(object *, object *);
typedef void (*mapfun_t)(object *, object **);
typedef int (*intfn_ptr_type)(int w, int x, int y, int z);

typedef const struct {
  const char *string;
  fn_ptr_type fptr;
  uint8_t minmax;
} tbl_entry_t;

// Stream typedefs

typedef uint8_t nstream_t;

typedef int (*gfun_t)(void);
typedef void (*pfun_t)(char);

typedef pfun_t (*pstream_ptr_t)(uint8_t address);
typedef gfun_t (*gstream_ptr_t)(uint8_t address);

typedef const struct {
  const char *streamname;
  pstream_ptr_t pfunptr;
  gstream_ptr_t gfunptr;
} stream_entry_t;

enum builtins {
#define N(e, name, fn, minmax, doc) e,
#define B(name, fn, minmax, doc)
#define K(name, value, context)
#include "builtins.def"
#undef N
#undef B
#undef K
};

// Global variables

static object *Workspace;
static unsigned int WorkspaceSize;
static uintptr_t StackLimit;  // eval refuses to recurse below this address
static uint8_t MyCode[CODESIZE] WORDALIGNED;

static jmp_buf toplevel_handler;
static jmp_buf *handler = &toplevel_handler;
static unsigned int Freespace = 0;
static object *Freelist;
static unsigned int TraceFn[TRACEMAX];
static unsigned int TraceDepth[TRACEMAX];
static builtin_t Context;
#define BACKTRACESIZE 8
static uint8_t TraceStart = 0, TraceTop = 0;
static symbol_t Backtrace[BACKTRACESIZE];

static object *GlobalEnv;
static object *GCStack = NULL;
static object *GlobalString;
static object *GlobalStringTail;
static int GlobalStringIndex = 0;
static uint8_t PrintCount = 0;
static uint8_t BreakLevel = 0;
static char LastChar = 0;
static char LastPrint = 0;

// Flags
enum flag { PRINTREADABLY, RETURNFLAG, ESCAPE, EXITEDITOR, LIBRARYLOADED, NOESC, NOECHO, MUFFLEERRORS, BACKTRACE };
typedef uint16_t flags_t;
static flags_t Flags = 1<<PRINTREADABLY; // Set by default

// Forward references
static object *tee;

// Function prototypes

static int modbacktrace (int n);
static void printbacktrace (void);
static void errorsub (symbol_t fname, const char *string);
// These leave by longjmp()
#define NORETURN __attribute__((noreturn))
static void errorend (void) NORETURN;
static void stackoverflow (void) NORETURN;
static void errorsym (symbol_t fname, const char *string, object *symbol) NORETURN;
static void errorsym2 (symbol_t fname, const char *string) NORETURN;
static void error (const char *string, object *symbol) NORETURN;
static void error2 (const char *string) NORETURN;
static void formaterr (object *formatstr, const char *string, uint8_t p) NORETURN;
static void initworkspace (void);
static object *myalloc (void);
static void myfree (object *obj);
static object *number (int n);
static object *makefloat (float f);
static object *character (uint8_t c);
static object *cons (object *arg1, object *arg2);
static object *symbol (symbol_t name);
static object *bsymbol (builtin_t name);
static object *codehead (int entry);
static object *intern (symbol_t name);
static bool eqsymbols (object *obj, char *buffer);
static object *internlong (char *buffer);
static object *stream (uint8_t streamtype, uint8_t address);
static object *newstring (void);
static object *features (void);
static void markobject (object *obj);
static void sweep (void);
static void gc (object *form, object *env);
static void movepointer (object *from, object *to);
static uintptr_t compactimage (object **arg);
static char *MakeFilename (object *arg, char *buffer);
static void imagewrite (const void *data, unsigned int bytes);
static int saveimage (object *arg);
static object *rebase (object *ptr, intptr_t delta);
static void rebaseimage (unsigned int imagesize, intptr_t delta);
static int loadimagefile (const char *path, object **autorun);
static int loadimage (object *arg);
static int tracing (symbol_t name);
static void trace (symbol_t name);
static void untrace (symbol_t name);
static bool consp (object *x);
static bool listp (object *x);
static object *quote (object *arg);
static builtin_t builtin (symbol_t name);
static symbol_t sym (builtin_t x);
static int8_t toradix40 (char ch);
static char fromradix40 (char n);
static uint32_t pack40 (char *buffer);
static bool valid40 (char *buffer);
static int8_t digitvalue (char d);
static int checkinteger (object *obj);
static int checkbitvalue (object *obj);
static float checkintfloat (object *obj);
static int checkchar (object *obj);
static object *checkstring (object *obj);
static int isstream (object *obj);
static int isbuiltin (object *obj, builtin_t n);
static bool builtinp (symbol_t name);
static int checkkeyword (object *obj);
static void checkargs (object *args);
static bool eq (object *arg1, object *arg2);
static bool equal (object *arg1, object *arg2);
static int listlength (object *list);
static object *checkarguments (object *args, int min, int max);
static object *add_floats (object *args, float fresult);
static object *subtract_floats (object *args, float fresult);
static object *negate (object *arg);
static object *multiply_floats (object *args, float fresult);
static object *divide_floats (object *args, float fresult);
static object *remmod (object *args, bool mod);
static object *compare (object *args, bool lt, bool gt, bool eq);
static int intpower (int base, int exp);
static object *testargument (object *args);
static object *delassoc (object *key, object **alist);
static int nextpower2 (int n);
static object *buildarray (int n, int s, object *def);
static object *makearray (object *dims, object *def, bool bitp);
static object **arrayref (object *array, int index, int size);
static object **getarray (object *array, object *subs, object *env, int *bit);
static void rslice (object *array, int size, int slice, object *dims, object *args);
static object *readarray (int d, object *args);
static object *readbitarray (gfun_t gfun);
static void pslice (object *array, int size, int slice, object *dims, pfun_t pfun, bool bitp);
static void printarray (object *array, pfun_t pfun);
static void indent (uint8_t spaces, char ch, pfun_t pfun);
static object *startstring (void);
static object *princtostring (object *arg);
static void buildstring (char ch, object** tail);
static object *readstring (uint8_t delim, bool esc, gfun_t gfun);
static int stringlength (object *form);
static object **getcharplace (object *string, int n, int *shift);
static uint8_t nthchar (object *string, int n);
static int gstr (void);
static void pstr (char c);
static object *lispstring (char *s);
static int stringcompare (object *args, bool lt, bool gt, bool eq);
static object *documentation (object *arg, object *env);
static object *apropos (object *arg, bool print);
static char *cstring (object *form, char *buffer, int buflen);
static object *value (symbol_t n, object *env);
static object *findpair (object *var, object *env);
static bool boundp (object *var, object *env);
static object *findvalue (object *var, object *env);
static object *closure (int tc, symbol_t name, object *function, object *args, object **env);
static object *apply (object *function, object *args, object *env);
static object **place (object *args, object *env, int *bit);
static object *carx (object *arg);
static object *cdrx (object *arg);
static object *cxxxr (object *args, uint8_t pattern);
static object *mapcl (object *args, object *env, bool mapl);
static void mapcarfun (object *result, object **tail);
static void mapcanfun (object *result, object **tail);
static object *mapcarcan (object *args, object *env, mapfun_t fun, bool maplist);
static object *dobody (object *args, object *env, bool star);
static void SDwrite (char c);
static void gfxwrite (char c);
static int SDread (void);
static pfun_t pfun_serial (uint8_t address);
static pfun_t pfun_string (uint8_t address);
static pfun_t pfun_sd (uint8_t address);
static pfun_t pfun_gfx (uint8_t address);
static gfun_t gfun_serial (uint8_t address);
static gfun_t gfun_sd (uint8_t address);
static const stream_entry_t *streamtable (int n);
static pfun_t pstreamfun (object *args);
static gfun_t gstreamfun (object *args);
static void checkanalogread (int pin);
static void playnote (int pin, int note, int octave);
static void nonote (int pin);
static void doze (int secs);
static void pcount (char c);
static uint8_t atomwidth (object *obj);
static uint8_t basewidth (object *obj, uint8_t base);
static bool quoted (object *obj);
static int subwidth (object *obj, int w);
static int subwidthlist (object *form, int w);
static void superprint (object *form, int lm, pfun_t pfun);
static object *edit (object *fun);
static object *call (int entry, int nargs, object *args, object *env);
static void putcode (object *arg, int origin, int pc);
static int assemble (int pass, int origin, object *entries, object *env, object *pcpair);
static object *sp_quote (object *args, object *env);
static object *sp_or (object *args, object *env);
static object *sp_defun (object *args, object *env);
static object *sp_defvar (object *args, object *env);
static object *sp_setq (object *args, object *env);
static object *sp_loop (object *args, object *env);
static object *sp_push (object *args, object *env);
static object *sp_pop (object *args, object *env);
static object *sp_incf (object *args, object *env);
static object *sp_decf (object *args, object *env);
static object *sp_setf (object *args, object *env);
static object *sp_dolist (object *args, object *env);
static object *sp_dotimes (object *args, object *env);
static object *sp_do (object *args, object *env);
static object *sp_dostar (object *args, object *env);
static object *sp_trace (object *args, object *env);
static object *sp_untrace (object *args, object *env);
static object *sp_formillis (object *args, object *env);
static object *sp_time (object *args, object *env);
static object *sp_withoutputtostring (object *args, object *env);
static object *sp_withsdcard (object *args, object *env);
static object *sp_defcode (object *args, object *env);
static object *tf_progn (object *args, object *env);
static object *tf_if (object *args, object *env);
static object *tf_cond (object *args, object *env);
static object *tf_when (object *args, object *env);
static object *tf_unless (object *args, object *env);
static object *tf_case (object *args, object *env);
static object *tf_and (object *args, object *env);
static object *fn_not (object *args, object *env);
static object *fn_cons (object *args, object *env);
static object *fn_atom (object *args, object *env);
static object *fn_listp (object *args, object *env);
static object *fn_consp (object *args, object *env);
static object *fn_symbolp (object *args, object *env);
static object *fn_arrayp (object *args, object *env);
static object *fn_boundp (object *args, object *env);
static object *fn_keywordp (object *args, object *env);
static object *fn_setfn (object *args, object *env);
static object *fn_streamp (object *args, object *env);
static object *fn_eq (object *args, object *env);
static object *fn_equal (object *args, object *env);
static object *fn_car (object *args, object *env);
static object *fn_cdr (object *args, object *env);
static object *fn_caar (object *args, object *env);
static object *fn_cadr (object *args, object *env);
static object *fn_cdar (object *args, object *env);
static object *fn_cddr (object *args, object *env);
static object *fn_caaar (object *args, object *env);
static object *fn_caadr (object *args, object *env);
static object *fn_cadar (object *args, object *env);
static object *fn_caddr (object *args, object *env);
static object *fn_cdaar (object *args, object *env);
static object *fn_cdadr (object *args, object *env);
static object *fn_cddar (object *args, object *env);
static object *fn_cdddr (object *args, object *env);
static object *fn_length (object *args, object *env);
static object *fn_arraydimensions (object *args, object *env);
static object *fn_list (object *args, object *env);
static object *fn_copylist (object *args, object *env);
static object *fn_makearray (object *args, object *env);
static object *fn_reverse (object *args, object *env);
static object *fn_nth (object *args, object *env);
static object *fn_aref (object *args, object *env);
static object *fn_assoc (object *args, object *env);
static object *fn_member (object *args, object *env);
static object *fn_apply (object *args, object *env);
static object *fn_funcall (object *args, object *env);
static object *fn_append (object *args, object *env);
static object *fn_mapc (object *args, object *env);
static object *fn_mapl (object *args, object *env);
static object *fn_mapcar (object *args, object *env);
static object *fn_mapcan (object *args, object *env);
static object *fn_maplist (object *args, object *env);
static object *fn_mapcon (object *args, object *env);
static object *fn_add (object *args, object *env);
static object *fn_subtract (object *args, object *env);
static object *fn_multiply (object *args, object *env);
static object *fn_divide (object *args, object *env);
static object *fn_mod (object *args, object *env);
static object *fn_rem (object *args, object *env);
static object *fn_oneplus (object *args, object *env);
static object *fn_oneminus (object *args, object *env);
static object *fn_abs (object *args, object *env);
static object *fn_random (object *args, object *env);
static object *fn_maxfn (object *args, object *env);
static object *fn_minfn (object *args, object *env);
static object *fn_noteq (object *args, object *env);
static object *fn_numeq (object *args, object *env);
static object *fn_less (object *args, object *env);
static object *fn_lesseq (object *args, object *env);
static object *fn_greater (object *args, object *env);
static object *fn_greatereq (object *args, object *env);
static object *fn_plusp (object *args, object *env);
static object *fn_minusp (object *args, object *env);
static object *fn_zerop (object *args, object *env);
static object *fn_oddp (object *args, object *env);
static object *fn_evenp (object *args, object *env);
static object *fn_integerp (object *args, object *env);
static object *fn_numberp (object *args, object *env);
static object *fn_floatfn (object *args, object *env);
static object *fn_floatp (object *args, object *env);
static object *fn_sin (object *args, object *env);
static object *fn_cos (object *args, object *env);
static object *fn_tan (object *args, object *env);
static object *fn_asin (object *args, object *env);
static object *fn_acos (object *args, object *env);
static object *fn_atan (object *args, object *env);
static object *fn_sinh (object *args, object *env);
static object *fn_cosh (object *args, object *env);
static object *fn_tanh (object *args, object *env);
static object *fn_exp (object *args, object *env);
static object *fn_sqrt (object *args, object *env);
static object *fn_log (object *args, object *env);
static object *fn_expt (object *args, object *env);
static object *fn_ceiling (object *args, object *env);
static object *fn_floor (object *args, object *env);
static object *fn_truncate (object *args, object *env);
static object *fn_round (object *args, object *env);
static object *fn_char (object *args, object *env);
static object *fn_charcode (object *args, object *env);
static object *fn_codechar (object *args, object *env);
static object *fn_characterp (object *args, object *env);
static object *fn_stringp (object *args, object *env);
static object *fn_stringeq (object *args, object *env);
static object *fn_stringless (object *args, object *env);
static object *fn_stringgreater (object *args, object *env);
static object *fn_stringnoteq (object *args, object *env);
static object *fn_stringlesseq (object *args, object *env);
static object *fn_stringgreatereq (object *args, object *env);
static object *fn_sort (object *args, object *env);
static object *fn_stringfn (object *args, object *env);
static object *fn_concatenate (object *args, object *env);
static object *fn_subseq (object *args, object *env);
static object *fn_search (object *args, object *env);
static object *fn_readfromstring (object *args, object *env);
static object *fn_princtostring (object *args, object *env);
static object *fn_prin1tostring (object *args, object *env);
static object *fn_logand (object *args, object *env);
static object *fn_logior (object *args, object *env);
static object *fn_logxor (object *args, object *env);
static object *fn_lognot (object *args, object *env);
static object *fn_ash (object *args, object *env);
static object *fn_logbitp (object *args, object *env);
static object *fn_eval (object *args, object *env);
static object *fn_return (object *args, object *env);
static object *fn_globals (object *args, object *env);
static object *fn_locals (object *args, object *env);
static object *fn_makunbound (object *args, object *env);
static object *fn_break (object *args, object *env);
static object *fn_read (object *args, object *env);
static object *fn_prin1 (object *args, object *env);
static object *fn_print (object *args, object *env);
static object *fn_princ (object *args, object *env);
static object *fn_terpri (object *args, object *env);
static object *fn_readbyte (object *args, object *env);
static object *fn_readline (object *args, object *env);
static object *fn_writebyte (object *args, object *env);
static object *fn_writestring (object *args, object *env);
static object *fn_writeline (object *args, object *env);
static object *fn_gc (object *args, object *env);
static object *fn_room (object *args, object *env);
static object *fn_backtrace (object *args, object *env);
static object *fn_saveimage (object *args, object *env);
static object *fn_loadimage (object *args, object *env);
static object *fn_cls (object *args, object *env);
static object *fn_pinmode (object *args, object *env);
static object *fn_digitalread (object *args, object *env);
static object *fn_digitalwrite (object *args, object *env);
static object *fn_analogread (object *args, object *env);
static object *fn_delay (object *args, object *env);
static object *fn_millis (object *args, object *env);
static object *fn_sleep (object *args, object *env);
static object *fn_note (object *args, object *env);
static object *fn_register (object *args, object *env);
static object *fn_edit (object *args, object *env);
static object *fn_pprint (object *args, object *env);
static object *fn_pprintall (object *args, object *env);
static object *fn_format (object *args, object *env);
static object *fn_require (object *args, object *env);
static object *fn_listlibrary (object *args, object *env);
static object *sp_help (object *args, object *env);
static object *fn_documentation (object *args, object *env);
static object *fn_apropos (object *args, object *env);
static object *fn_aproposlist (object *args, object *env);
static object *sp_unwindprotect (object *args, object *env);
static object *sp_ignoreerrors (object *args, object *env);
static object *sp_error (object *args, object *env);
static object *fn_directory (object *args, object *env);
static object *sp_withgfx (object *args, object *env);
static object *fn_drawpixel (object *args, object *env);
static object *fn_drawline (object *args, object *env);
static object *fn_drawrect (object *args, object *env);
static object *fn_fillrect (object *args, object *env);
static object *fn_drawcircle (object *args, object *env);
static object *fn_fillcircle (object *args, object *env);
static object *fn_drawroundrect (object *args, object *env);
static object *fn_fillroundrect (object *args, object *env);
static object *fn_drawtriangle (object *args, object *env);
static object *fn_filltriangle (object *args, object *env);
static object *fn_drawchar (object *args, object *env);
static object *fn_setcursor (object *args, object *env);
static object *fn_settextcolor (object *args, object *env);
static object *fn_settextsize (object *args, object *env);
static object *fn_settextwrap (object *args, object *env);
static object *fn_fillscreen (object *args, object *env);
static object *fn_getkey (object *args, object *env);
static object *fn_readpixel (object *args, object *env);
static void writeTwo (uint16_t word);
static void writeFour (uint32_t word);
static void savebmp (object *arg);
static object *fn_savebmp (object *args, object *env);
static const tbl_entry_t *table (int n);
static unsigned int tablesize (int n);
static builtin_t lookupbuiltin (char* c);
static intptr_t lookupfn (builtin_t name);
static uint8_t getminmax (builtin_t name);
static void checkminmax (builtin_t name, int nargs);
static bool findsubstring (char *part, builtin_t name);
static void testescape (void);
static bool colonp (symbol_t name);
static bool keywordp (object *obj);
static void backtrace (symbol_t name);
static object *eval (object *form, object *env);
static void pserial (char c);
static void pcharacter (uint8_t c, pfun_t pfun);
static void plispstr (symbol_t name, pfun_t pfun);
static void printstring (object *form, pfun_t pfun);
static void pbuiltin (builtin_t name, pfun_t pfun);
static void pradix40 (symbol_t name, pfun_t pfun);
static void printsymbol (object *form, pfun_t pfun);
static void psymbol (symbol_t name, pfun_t pfun);
static void pfstring (const char *s, pfun_t pfun);
static void pint (int i, pfun_t pfun);
static void pintbase (uint32_t i, uint8_t base, pfun_t pfun);
static void printhex4 (int i, pfun_t pfun);
static double tento (int n);
static void pmantissa (float f, pfun_t pfun);
static void pfloat (float f, pfun_t pfun);
static void pln (pfun_t pfun);
static void pfl (pfun_t pfun);
static void plist (object *form, pfun_t pfun);
static void pstream (object *form, pfun_t pfun);
static void printobject (object *form, pfun_t pfun);
static void prin1object (object *form, pfun_t pfun);
static int glibrary (void);
static void gserial_flush (void);
static int gserial (void);
static object *nextitem (gfun_t gfun);
static object *readrest (gfun_t gfun);
static int glast (void);
static object *readmain (gfun_t gfun);
static object *read (gfun_t gfun);
static void initenv (void);
static void repl (object *env);
static void ulisperror (void);
static void loadfile (const char *path);
static void usage (void);

// Error handling

static int modbacktrace (int n) {
  return (n+BACKTRACESIZE) % BACKTRACESIZE;
}

static void printbacktrace (void) {
  if (TraceStart != TraceTop) pserial('[');
  int tracesize = modbacktrace(TraceTop-TraceStart);
  for (int i=1; i<=tracesize; i++) {
    printsymbol(symbol(Backtrace[modbacktrace(TraceTop-i)]), pserial);
    if (i!=tracesize) pfstring(" <- ", pserial);
  }
  if (TraceStart != TraceTop) pserial(']');
}

static void errorsub (symbol_t fname, const char *string) {
  pfl(pserial); pfstring("Error", pserial);
  if (TraceStart != TraceTop) pserial(' ');
  printbacktrace();
  pfstring(": ", pserial);
  if (fname != sym(NIL)) {
    pserial('\'');
    psymbol(fname, pserial);
    pserial('\''); pserial(' ');
  }
  pfstring(string, pserial);
}

static void errorend (void) { GCStack = NULL; longjmp(*handler, 1); }

static void stackoverflow (void) {
  Context = NIL;
  error2("stack overflow");
}

static void errorsym (symbol_t fname, const char *string, object *symbol) {
  if (!tstflag(MUFFLEERRORS)) {
    errorsub(fname, string);
    pserial(':'); pserial(' ');
    printobject(symbol, pserial);
    pln(pserial);
  }
  errorend();
}

static void errorsym2 (symbol_t fname, const char *string) {
  if (!tstflag(MUFFLEERRORS)) {
    errorsub(fname, string);
    pln(pserial);
  }
  errorend();
}

static void error (const char *string, object *symbol) {
  errorsym(sym(Context), string, symbol);
}

static void error2 (const char *string) {
  errorsym2(sym(Context), string);
}

static void formaterr (object *formatstr, const char *string, uint8_t p) {
  pln(pserial); indent(4, ' ', pserial); printstring(formatstr, pserial); pln(pserial);
  indent(p+5, ' ', pserial); pserial('^');
  error2(string);
  pln(pserial);
  errorend();
}

// Save space as these are used multiple times
static const char notanumber[] = "argument is not a number";
static const char notaninteger[] = "argument is not an integer";
static const char notastring[] = "argument is not a string";
static const char notalist[] = "argument is not a list";
static const char notasymbol[] = "argument is not a symbol";
static const char notproper[] = "argument is not a proper list";
static const char toomanyargs[] = "too many arguments";
static const char toofewargs[] = "too few arguments";
static const char noargument[] = "missing argument";
static const char divisionbyzero[] = "division by zero";
static const char indexnegative[] = "index can't be negative";
static const char invalidarg[] = "invalid argument";
static const char invalidkey[] = "invalid keyword";
static const char illegalclause[] = "illegal clause";
static const char illegalfn[] = "illegal function";
static const char invalidpin[] = "invalid pin";
static const char oddargs[] = "odd number of arguments";
static const char indexrange[] = "index out of range";
static const char canttakecar[] = "can't take car";
static const char canttakecdr[] = "can't take cdr";

// Set up workspace

static void initworkspace (void) {
  Freelist = NULL;
  for (int i=WORKSPACESIZE-1; i>=0; i--) {
    object *obj = &Workspace[i];
    car(obj) = NULL;
    cdr(obj) = Freelist;
    Freelist = obj;
    Freespace++;
  }
}

static object *myalloc (void) {
  if (Freespace == 0) { Context = NIL; error2("no room"); }
  object *temp = Freelist;
  Freelist = cdr(Freelist);
  Freespace--;
  return temp;
}

static void myfree (object *obj) {
  car(obj) = NULL;
  cdr(obj) = Freelist;
  Freelist = obj;
  Freespace++;
}

// Make each type of object

static object *number (int n) {
  object *ptr = myalloc();
  ptr->type = NUMBER;
  ptr->integer = n;
  return ptr;
}

static object *makefloat (float f) {
  object *ptr = myalloc();
  ptr->type = FLOAT;
  ptr->single_float = f;
  return ptr;
}

static object *character (uint8_t c) {
  object *ptr = myalloc();
  ptr->type = CHARACTER;
  ptr->chars = c;
  return ptr;
}

static object *cons (object *arg1, object *arg2) {
  object *ptr = myalloc();
  ptr->car = arg1;
  ptr->cdr = arg2;
  return ptr;
}

static object *symbol (symbol_t name) {
  object *ptr = myalloc();
  ptr->type = SYMBOL;
  ptr->name = name;
  return ptr;
}

static object *bsymbol (builtin_t name) {
  return intern(twist(name+BUILTINS));
}

static object *codehead (int entry) {
  object *ptr = myalloc();
  ptr->type = CODE;
  ptr->integer = entry;
  return ptr;
}

static object *intern (symbol_t name) {
  #if !defined(BOARD_HAS_PSRAM)
  for (unsigned int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && obj->name == name) return obj;
  }
  #endif
  return symbol(name);
}

static bool eqsymbols (object *obj, char *buffer) {
  object *arg = cdr(obj);
  int i = 0;
  while (!(arg == NULL && buffer[i] == 0)) {
    if (arg == NULL || buffer[i] == 0) return false;
    chars_t test = 0; int shift = 24;
    for (int j=0; j<4; j++, i++) {
      if (buffer[i] == 0) break;
      test = test | buffer[i]<<shift;
      shift = shift - 8;
    }
    if (arg->chars != test) return false;
    arg = car(arg);
  }
  return true;
}

static object *internlong (char *buffer) {
  #if !defined(BOARD_HAS_PSRAM)
  for (unsigned int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && longsymbolp(obj) && eqsymbols(obj, buffer)) return obj;
  }
  #endif
  object *obj = lispstring(buffer);
  obj->type = SYMBOL;
  return obj;
}

static object *stream (uint8_t streamtype, uint8_t address) {
  object *ptr = myalloc();
  ptr->type = STREAM;
  ptr->integer = streamtype<<8 | address;
  return ptr;
}

static object *newstring (void) {
  object *ptr = myalloc();
  ptr->type = STRING;
  ptr->chars = 0;
  return ptr;
}

// Features

static const char floatingpoint[] = ":floating-point";
static const char arrays[] = ":arrays";
static const char doc[] = ":documentation";
static const char machinecode[] = ":machine-code";
static const char errorhandling[] = ":error-handling";
static const char gfx[] = ":gfx";
static const char sdcard[] = ":sd-card";
#if defined(__arm__)
static const char arm[] = ":arm";
#endif

static object *features (void) {
  object *result = NULL;
  #if defined(__arm__)
  push(internlong((char *)arm), result);
  #endif
  push(internlong((char *)sdcard), result);
  push(internlong((char *)gfx), result);
  push(internlong((char *)errorhandling), result);
  push(internlong((char *)machinecode), result);
  push(internlong((char *)doc), result);
  push(internlong((char *)arrays), result);
  push(internlong((char *)floatingpoint), result);
  return result;
}

// Garbage collection

// Marks everything that obj reaches. The way back up is kept in the cells
// being traced, by reversing the pointer just followed (Deutsch, Schorr and
// Waite), so marking uses no C stack however deep the structure: the stack
// shares its memory with the workspace, and nothing stops it growing into
// the cells. A pair is traced car first; its car then holds the cell above,
// and once the car is done its cdr holds it, with VISITBIT set in the car.
// An array header keeps its type and traces its cdr alone.

static void markobject (object *obj) {
  object *above = NULL;
  for (;;) {
    // Downwards, until there is nothing new below
    while (obj != NULL && !marked(obj)) {
      uintptr_t type = obj->type;
      mark(obj);
      if (type >= PAIR || type == ZZERO) {
        object *below = car(obj);
        car(obj) = (object *)((uintptr_t)above | MARKBIT);
        above = obj;
        obj = (object *)((uintptr_t)below & ~(uintptr_t)MARKBIT);
      } else if (type == ARRAY) {
        object *below = cdr(obj);
        cdr(obj) = above;
        above = obj;
        obj = below;
      } else {
        if (type == STRING || (type == SYMBOL && longsymbolp(obj))) {
          object *chars = cdr(obj);
          while (chars != NULL) {
            object *next = car(chars);
            mark(chars);
            chars = next;
          }
        }
        break;
      }
    }
    // Upwards, restoring each cell, to one whose cdr is still to trace
    for (;;) {
      if (above == NULL) return;
      uintptr_t state = (uintptr_t)car(above);
      if (state & VISITBIT) {
        object *up = cdr(above);
        cdr(above) = obj;
        if (state != (ARRAY | MARKBIT)) car(above) = (object *)(state & ~(uintptr_t)VISITBIT);
        obj = above;
        above = up;
      } else {
        object *below = cdr(above);
        cdr(above) = (object *)(state & ~(uintptr_t)MARKBIT);
        car(above) = (object *)((uintptr_t)obj | MARKBIT | VISITBIT);
        obj = below;
        break;
      }
    }
  }
}

static void sweep (void) {
  Freelist = NULL;
  Freespace = 0;
  for (int i=WORKSPACESIZE-1; i>=0; i--) {
    object *obj = &Workspace[i];
    if (!marked(obj)) myfree(obj); else unmark(obj);
  }
}

static void gc (object *form, object *env) {
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  markobject(form);
  markobject(env);
  sweep();
}

// Compact image

static void movepointer (object *from, object *to) {
  uintptr_t limit = ((uintptr_t)(from) - (uintptr_t)(Workspace))/sizeof(object);
  for (uintptr_t i=0; i<=limit; i++) {
    object *obj = &Workspace[i];
    unsigned int type = (obj->type) & ~MARKBIT;
    if (marked(obj) && (type >= ARRAY || type==ZZERO || (type == SYMBOL && longsymbolp(obj)))) {
      if (car(obj) == (object *)((uintptr_t)from | MARKBIT))
        car(obj) = (object *)((uintptr_t)to | MARKBIT);
      if (cdr(obj) == from) cdr(obj) = to;
    }
  }
  // Fix strings and long symbols
  for (uintptr_t i=0; i<=limit; i++) {
    object *obj = &Workspace[i];
    if (marked(obj)) {
      unsigned int type = (obj->type) & ~MARKBIT;
      if (type == STRING || (type == SYMBOL && longsymbolp(obj))) {
        obj = cdr(obj);
        while (obj != NULL) {
          if (cdr(obj) == to) cdr(obj) = from;
          obj = (object *)((uintptr_t)(car(obj)) & ~MARKBIT);
        }
      }
    }
  }
}

static uintptr_t compactimage (object **arg) {
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  object *firstfree = Workspace;
  while (marked(firstfree)) firstfree++;
  object *obj = &Workspace[WORKSPACESIZE-1];
  while (firstfree < obj) {
    if (marked(obj)) {
      car(firstfree) = car(obj);
      cdr(firstfree) = cdr(obj);
      unmark(obj);
      movepointer(obj, firstfree);
      if (GlobalEnv == obj) GlobalEnv = firstfree;
      if (GCStack == obj) GCStack = firstfree;
      if (*arg == obj) *arg = firstfree;
      while (marked(firstfree)) firstfree++;
    }
    obj--;
  }
  sweep();
  return firstfree - Workspace;
}

// Make SD card filename

static char *MakeFilename (object *arg, char *buffer) {
  int i = 0;
  do {
    char c = nthchar(arg, i);
    if (c == '\0') break;
    buffer[i++] = c;
  } while (i<PATHSIZE-1);
  buffer[i] = '\0';
  return buffer;
}

// Save-image and load-image

// An image is a header of IMAGEWORDS words, the machine-code area and the
// compacted cells, all in the word size and byte order of the program that
// wrote it. Cells hold absolute addresses, so the header records the
// workspace address they were written from and loadimage() rebases them.

#define IMAGEMAGIC ((uintptr_t)0x31694c75 + sizeof(object))  // "uLi1" and the cell size
#define IMAGEWORDS 6
static const char defaultimage[] = "ULISP.IMG";

static void imagewrite (const void *data, unsigned int bytes) {
  if (!sys_fwrite(SYS_FTMP, data, bytes)) { sys_fclose(SYS_FTMP); error2("not enough room"); }
}

static int saveimage (object *arg) {
  char buffer[PATHSIZE];
  const char *path = defaultimage;
  if (stringp(arg)) { path = MakeFilename(arg, buffer); arg = NULL; }
  else if (!(arg == NULL || listp(arg) || symbolp(arg))) error(invalidarg, arg);
  unsigned int imagesize = compactimage(&arg);
  if (!sys_fopen(SYS_FTMP, path, SYS_WRITE)) error2("problem saving image or invalid filename");
  uintptr_t header[IMAGEWORDS] = { IMAGEMAGIC, (uintptr_t)Workspace, imagesize,
    (uintptr_t)arg, (uintptr_t)GlobalEnv, (uintptr_t)GCStack };
  imagewrite(header, sizeof header);
  imagewrite(MyCode, CODESIZE);
  imagewrite(Workspace, imagesize * sizeof(object));
  if (!sys_fclose(SYS_FTMP)) error2("not enough room");
  return imagesize;
}

static object *rebase (object *ptr, intptr_t delta) {
  return (ptr == NULL) ? NULL : (object *)((uintptr_t)ptr + delta);
}

// Moves the addresses in the first imagesize cells by delta. The cells of a
// string or long symbol chain hold characters where a pair holds its cdr, and
// nothing in a chain cell says so; the first pass finds every chain from its
// header and marks it, and the second pass rebases the pairs that remain.
static void rebaseimage (unsigned int imagesize, intptr_t delta) {
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    uintptr_t type = obj->type;
    if (type == STRING || (type == SYMBOL && longsymbolp(obj))) {
      obj = cdr(obj) = rebase(cdr(obj), delta);
      while (obj != NULL && !marked(obj)) {
        object *next = rebase(car(obj), delta);
        car(obj) = next;
        mark(obj);
        obj = next;
      }
    }
  }
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    if (marked(obj)) { unmark(obj); continue; }
    uintptr_t type = obj->type;
    if (type >= PAIR || type == ZZERO) car(obj) = rebase(car(obj), delta);
    if (type >= PAIR || type == ZZERO || type == ARRAY) cdr(obj) = rebase(cdr(obj), delta);
  }
}

// An image is checked in the file, before a cell reaches the workspace, so
// that a damaged or foreign one is refused with the workspace as it was.
// The check reads the cells in blocks and follows each string chain by
// seeking, and it keeps one bit for each cell on the stack to record the
// cells that chains own. What it establishes is what rebaseimage(), the
// collector and the printer rely on: every pointer is null or the address
// of a cell the image holds; a chain ends and its cells are no pair's
// target; and a code block, a stream and a builtin symbol stay inside their
// tables. How the cells fit together as Lisp data is not examined.

#define IMAGEBLOCK 16  // Cells read at a time
#define chainbit(bits, i)  ((bits)[(i)>>3] & (1u << ((i) & 7)))
#define setchain(bits, i)  ((bits)[(i)>>3] |= (uint8_t)(1u << ((i) & 7)))

static bool imagecells (unsigned int first, object *cells, unsigned int count) {
  size_t offset = IMAGEWORDS * sizeof(uintptr_t) + CODESIZE + (size_t)first * sizeof(object);
  return sys_fseek(SYS_FTMP, offset) && sys_fread(SYS_FTMP, cells, count * sizeof(object));
}

// True when p is the address of one of the cells, which are cells long from base
static bool imagepointer (uintptr_t base, unsigned int cells, uintptr_t p, unsigned int *index) {
  if (p < base) return false;
  uintptr_t offset = p - base;
  if (offset % sizeof(object) != 0 || offset / sizeof(object) >= cells) return false;
  *index = offset / sizeof(object);
  return true;
}

static const char imagerange[] = "image pointer out of range";
static const char imagelinked[] = "image pair points into a string";
static const char imageinvalid[] = "image cell is invalid";

// NULL when p is the address of a cell that no string chain owns
static const char *imagetarget (uintptr_t base, unsigned int cells, const uint8_t *chains, uintptr_t p) {
  unsigned int index;
  if (!imagepointer(base, cells, p, &index)) return imagerange;
  return chainbit(chains, index) ? imagelinked : NULL;
}

// Returns NULL for a sound image, or what is wrong with it
static const char *checkimage (const uintptr_t *header, unsigned int cells) {
  uintptr_t base = header[1];
  // Above the type codes, so no address reads as one, and without wrapping
  if (base <= PAIR || cells > (UINTPTR_MAX - base) / sizeof(object)) return "not an image this uLisp can load";
  size_t bytes = (cells + 7) / 8;
  if ((uintptr_t)__builtin_frame_address(0) < StackLimit + bytes + 1024) return "no room";
  uint8_t chains[bytes + 1];
  memset(chains, 0, bytes);
  object block[IMAGEBLOCK];
  // Every string chain: in range, no cell twice (which also ends a loop)
  for (unsigned int first=0; first<cells; first+=IMAGEBLOCK) {
    unsigned int count = (cells - first < IMAGEBLOCK) ? cells - first : IMAGEBLOCK;
    if (!imagecells(first, block, count)) return "image is truncated";
    for (unsigned int k=0; k<count; k++) {
      object *obj = &block[k];
      if (!(obj->type == STRING || (obj->type == SYMBOL && longsymbolp(obj)))) continue;
      uintptr_t p = (uintptr_t)cdr(obj);
      while (p != 0) {
        unsigned int index;
        if (!imagepointer(base, cells, p, &index)) return imagerange;
        if (chainbit(chains, index)) return "image string chain is shared or loops";
        setchain(chains, index);
        object link;
        if (!imagecells(index, &link, 1)) return "image is truncated";
        p = (uintptr_t)car(&link);
      }
    }
  }
  // The other cells, by type; the chains are known now
  for (unsigned int first=0; first<cells; first+=IMAGEBLOCK) {
    unsigned int count = (cells - first < IMAGEBLOCK) ? cells - first : IMAGEBLOCK;
    if (!imagecells(first, block, count)) return "image is truncated";
    for (unsigned int k=0; k<count; k++) {
      if (chainbit(chains, first + k)) continue;
      object *obj = &block[k];
      uintptr_t type = obj->type, cd = (uintptr_t)cdr(obj);
      const char *fault = NULL;
      if (type == ZZERO || type >= PAIR) {
        if (type != ZZERO) fault = imagetarget(base, cells, chains, type);
        if (fault == NULL && cd != 0) fault = imagetarget(base, cells, chains, cd);
      } else if (type == ARRAY) {
        fault = imagetarget(base, cells, chains, cd);
      } else if (type == SYMBOL) {
        if (longsymbolp(obj)) continue;  // A chain header, checked above
        symbol_t name = obj->name;
        if (untwist(name) >= BUILTINS && untwist(name) - BUILTINS >= tablesize(0) + tablesize(1))
          fault = imageinvalid;
      } else if (type == CODE) {
        if (startblock(obj) > endblock(obj) || endblock(obj) > CODESIZE) fault = imageinvalid;
      } else if (type == STREAM) {
        if ((unsigned int)obj->integer >> 8 > GFXSTREAM || (obj->integer & 0xFF) > 1) fault = imageinvalid;
      } else if (type == STRING) {
        continue;
      } else if (!(type == NUMBER || type == CHARACTER || type == FLOAT)) {
        fault = imageinvalid;
      }
      if (fault != NULL) return fault;
    }
  }
  // The roots: a null, or a cell that is not part of a chain
  for (int r=3; r<IMAGEWORDS; r++) {
    const char *fault = (header[r] == 0) ? NULL : imagetarget(base, cells, chains, header[r]);
    if (fault != NULL) return fault;
  }
  return NULL;
}

// Returns the image size and stores the function the image runs at startup
static int loadimagefile (const char *path, object **autorun) {
  uintptr_t header[IMAGEWORDS];
  if (!sys_fopen(SYS_FTMP, path, SYS_READ)) error2("problem loading image or invalid filename");
  bool ok = sys_fread(SYS_FTMP, header, sizeof header) && header[0] == IMAGEMAGIC
    && header[2] <= WORKSPACESIZE;
  if (!ok) { sys_fclose(SYS_FTMP); error2("not an image this uLisp can load"); }
  unsigned int imagesize = header[2];
  const char *fault = checkimage(header, imagesize);
  if (fault != NULL) { sys_fclose(SYS_FTMP); error2(fault); }
  intptr_t delta = (uintptr_t)Workspace - header[1];
  ok = sys_fseek(SYS_FTMP, sizeof header) && sys_fread(SYS_FTMP, MyCode, CODESIZE)
    && sys_fread(SYS_FTMP, Workspace, imagesize * sizeof(object));
  sys_fclose(SYS_FTMP);
  if (!ok) {  // The workspace is part old and part new: start again
    initworkspace(); initenv();
    error2("image is truncated");
  }
  rebaseimage(imagesize, delta);
  *autorun = rebase((object *)header[3], delta);
  GlobalEnv = rebase((object *)header[4], delta);
  GCStack = rebase((object *)header[5], delta);
  gc(*autorun, NULL);
  return imagesize;
}

static int loadimage (object *arg) {
  char buffer[PATHSIZE];
  object *autorun;
  const char *path = defaultimage;
  if (stringp(arg)) path = MakeFilename(arg, buffer);
  else if (arg != NULL) error(invalidarg, arg);
  return loadimagefile(path, &autorun);
}

// Tracing

static int tracing (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) return i+1;
    i++;
  }
  return 0;
}

static void trace (symbol_t name) {
  if (tracing(name)) error("already being traced", symbol(name));
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == 0) { TraceFn[i] = name; TraceDepth[i] = 0; return; }
    i++;
  }
  error2("already tracing " stringify(TRACEMAX) " functions");
}

static void untrace (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) { TraceFn[i] = 0; return; }
    i++;
  }
  error("not tracing", symbol(name));
}

// Helper functions

static bool consp (object *x) {
  if (x == NULL) return false;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define atom(x) (!consp(x))

static bool listp (object *x) {
  if (x == NULL) return true;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define improperp(x) (!listp(x))

static object *quote (object *arg) {
  return cons(bsymbol(QUOTE), cons(arg,NULL));
}

// Radix 40 encoding

static builtin_t builtin (symbol_t name) {
  return (builtin_t)(untwist(name) - BUILTINS);
}

static symbol_t sym (builtin_t x) {
  return twist(x + BUILTINS);
}

static int8_t toradix40 (char ch) {
  if (ch == 0) return 0;
  if (ch >= '0' && ch <= '9') return ch-'0'+1;
  if (ch == '-') return 37;
  if (ch == '*') return 38;
  if (ch == '$') return 39;
  ch = ch | 0x20;
  if (ch >= 'a' && ch <= 'z') return ch-'a'+11;
  return -1; // Invalid
}

static char fromradix40 (char n) {
  if (n >= 1 && n <= 10) return '0'+n-1;
  if (n >= 11 && n <= 36) return 'a'+n-11;
  if (n == 37) return '-';
  if (n == 38) return '*';
  if (n == 39) return '$';
  return 0;
}

static uint32_t pack40 (char *buffer) {
  uint32_t x = 0; int j = 0;
  for (int i=0; i<6; i++) {
    x = x * 40 + toradix40(buffer[j]);
    if (buffer[j] != 0) j++;
  }
  return x;
}

static bool valid40 (char *buffer) {
  int t = 11;
  for (int i=0; i<6; i++) {
    if (toradix40(buffer[i]) < t) return false;
    if (buffer[i] == 0) break;
    t = 0;
  }
  return true;
}

static int8_t digitvalue (char d) {
  if (d>='0' && d<='9') return d-'0';
  d = d | 0x20;
  if (d>='a' && d<='f') return d-'a'+10;
  return 16;
}

static int checkinteger (object *obj) {
  if (!integerp(obj)) error(notaninteger, obj);
  return obj->integer;
}

static int checkbitvalue (object *obj) {
  if (!integerp(obj)) error(notaninteger, obj);
  int n = obj->integer;
  if (n & ~1) error("argument is not a bit value", obj);
  return n;
}

static float checkintfloat (object *obj) {
  if (integerp(obj)) return (float)obj->integer;
  if (!floatp(obj)) error(notanumber, obj);
  return obj->single_float;
}

static int checkchar (object *obj) {
  if (!characterp(obj)) error("argument is not a character", obj);
  return obj->chars;
}

static object *checkstring (object *obj) {
  if (!stringp(obj)) error(notastring, obj);
  return obj;
}

static int isstream (object *obj) {
  if (!streamp(obj)) error("not a stream", obj);
  return obj->integer;
}

static int isbuiltin (object *obj, builtin_t n) {
  return symbolp(obj) && obj->name == sym(n);
}

static bool builtinp (symbol_t name) {
  return (untwist(name) >= BUILTINS);
}

static int checkkeyword (object *obj) {
  if (!keywordp(obj)) error("argument is not a keyword", obj);
  builtin_t kname = builtin(obj->name);
  uint8_t context = getminmax(kname);
  if (context != 0 && context != Context) error(invalidkey, obj);
  return ((int)lookupfn(kname));
}

static void checkargs (object *args) {
  int nargs = listlength(args);
  checkminmax(Context, nargs);
}

static bool eq (object *arg1, object *arg2) {
  if (arg1 == arg2) return true;  // Same object
  if ((arg1 == nil) || (arg2 == nil)) return false;  // Not both values
  #if !defined(BOARD_HAS_PSRAM)
  if (arg1->cdr != arg2->cdr) return false;  // Different values
  if (symbolp(arg1) && symbolp(arg2)) return true;  // Same symbol
  #else
  if (symbolp(arg1) && symbolp(arg2)) return eqsymbol(arg1->name, arg2->name);  // Same symbol?
  if (arg1->cdr != arg2->cdr) return false;  // Different values
  #endif
  if (integerp(arg1) && integerp(arg2)) return true;  // Same integer
  if (floatp(arg1) && floatp(arg2)) return true; // Same float
  if (characterp(arg1) && characterp(arg2)) return true;  // Same character
  return false;
}

static bool equal (object *arg1, object *arg2) {
  checkstack();
  if (stringp(arg1) && stringp(arg2)) return (stringcompare(cons(arg1, cons(arg2, nil)), false, false, true) != -1);
  if (consp(arg1) && consp(arg2)) return (equal(car(arg1), car(arg2)) && equal(cdr(arg1), cdr(arg2)));
  return eq(arg1, arg2);
}

static int listlength (object *list) {
  int length = 0;
  while (list != NULL) {
    if (improperp(list)) error2(notproper);
    list = cdr(list);
    length++;
  }
  return length;
}

static object *checkarguments (object *args, int min, int max) {
  if (args == NULL) error2(noargument);
  args = first(args);
  if (!listp(args)) error(notalist, args);
  int length = listlength(args);
  if (length < min) error(toofewargs, args);
  if (length > max) error(toomanyargs, args);
  return args;
}

// Mathematical helper functions

static object *add_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult + checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

static object *subtract_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult - checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

static object *negate (object *arg) {
  if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(-(float)result);
    else return number(-result);
  } else if (floatp(arg)) return makefloat(-(arg->single_float));
  else error(notanumber, arg);
  return nil;
}

static object *multiply_floats (object *args, float fresult) {
  while (args != NULL) {
   object *arg = car(args);
    fresult = fresult * checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

static object *divide_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    float f = checkintfloat(arg);
    if (f == 0.0) error2(divisionbyzero);
    fresult = fresult / f;
    args = cdr(args);
  }
  return makefloat(fresult);
}

static object *remmod (object *args, bool mod) {
  object *arg1 = first(args);
  object *arg2 = second(args);
  if (integerp(arg1) && integerp(arg2)) {
    int divisor = arg2->integer;
    if (divisor == 0) error2(divisionbyzero);
    int dividend = arg1->integer;
    int remainder = (divisor == -1) ? 0 : dividend % divisor;  // INT_MIN % -1 overflows
    if (mod && (dividend<0) != (divisor<0)) remainder = remainder + divisor;
    return number(remainder);
  } else {
    float fdivisor = checkintfloat(arg2);
    if (fdivisor == 0.0) error2(divisionbyzero);
    float fdividend = checkintfloat(arg1);
    float fremainder = fmodf(fdividend , fdivisor);
    if (mod && (fdividend<0) != (fdivisor<0)) fremainder = fremainder + fdivisor;
    return makefloat(fremainder);
  }
}

static object *compare (object *args, bool lt, bool gt, bool eq) {
  object *arg1 = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg1) && integerp(arg2)) {
      if (!lt && ((arg1->integer) < (arg2->integer))) return nil;
      if (!eq && ((arg1->integer) == (arg2->integer))) return nil;
      if (!gt && ((arg1->integer) > (arg2->integer))) return nil;
    } else {
      if (!lt && (checkintfloat(arg1) < checkintfloat(arg2))) return nil;
      if (!eq && (checkintfloat(arg1) == checkintfloat(arg2))) return nil;
      if (!gt && (checkintfloat(arg1) > checkintfloat(arg2))) return nil;
    }
    arg1 = arg2;
    args = cdr(args);
  }
  return tee;
}

static int intpower (int base, int exp) {
  int result = 1;
  while (exp) {
    if (exp & 1) result = result * base;
    exp = exp / 2;
    base = base * base;
  }
  return result;
}

// Association lists

static object *testargument (object *args) {
  object *test = bsymbol(EQ);
  if (args != NULL) {
    if (cdr(args) == NULL) error2("unpaired keyword");
    if ((isbuiltin(first(args), TEST))) test = second(args);
    else error("unsupported keyword", first(args));
  }
  return test;
}

static object *delassoc (object *key, object **alist) {
  object *list = *alist;
  object *prev = NULL;
  while (list != NULL) {
    object *pair = first(list);
    if (eq(key,car(pair))) {
      if (prev == NULL) *alist = cdr(list);
      else cdr(prev) = cdr(list);
      return key;
    }
    prev = list;
    list = cdr(list);
  }
  return nil;
}

// Array utilities

static int nextpower2 (int n) {
  n--; n |= n >> 1; n |= n >> 2; n |= n >> 4;
  n |= n >> 8; n |= n >> 16; n++;
  return n<2 ? 2 : n;
}

static object *buildarray (int n, int s, object *def) {
  checkstack();
  int s2 = s>>1;
  if (s2 == 1) {
    if (n == 2) return cons(def, def);
    else if (n == 1) return cons(def, NULL);
    else return NULL;
  } else if (n >= s2) return cons(buildarray(s2, s2, def), buildarray(n - s2, s2, def));
  else return cons(buildarray(n, s2, def), nil);
}

static object *makearray (object *dims, object *def, bool bitp) {
  int size = 1;
  bool toobig = false;  // The product passed INT_MAX; a later zero dimension still empties it
  object *dimensions = dims;
  while (dims != NULL) {
    int d = car(dims)->integer;
    if (d < 0) error2("dimension can't be negative");
    if (d == 0) { size = 0; toobig = false; }
    else if (size > INT_MAX / d) toobig = true;
    else size = size * d;
    dims = cdr(dims);
  }
  // Bit array identified by making first dimension negative
  if (bitp) {
    size = size / (sizeof(int)*8) + (size % (sizeof(int)*8) != 0);
    car(dimensions) = number(-(car(dimensions)->integer));
  }
  // The tree takes about one cell for each element, or each word of bits
  if (toobig || (unsigned int)size > WORKSPACESIZE) { Context = NIL; error2("no room"); }
  object *ptr = myalloc();
  ptr->type = ARRAY;
  object *tree = nil;
  if (size != 0) tree = buildarray(size, nextpower2(size), def);
  ptr->cdr = cons(tree, dimensions);
  return ptr;
}

static object **arrayref (object *array, int index, int size) {
  int mask = nextpower2(size)>>1;
  object **p = &car(cdr(array));
  while (mask) {
    if ((index & mask) == 0) p = &(car(*p)); else p = &(cdr(*p));
    mask = mask>>1;
  }
  return p;
}

static object **getarray (object *array, object *subs, object *env, int *bit) {
  int index = 0, size = 1, s;
  *bit = -1;
  bool bitp = false;
  object *dims = cddr(array);
  while (dims != NULL && subs != NULL) {
    int d = car(dims)->integer;
    if (d < 0) { d = -d; bitp = true; }
    if (env) s = checkinteger(eval(car(subs), env)); else s = checkinteger(car(subs));
    if (s < 0 || s >= d) error("subscript out of range", car(subs));
    size = size * d;
    index = index * d + s;
    dims = cdr(dims); subs = cdr(subs);
  }
  if (dims != NULL) error2("too few subscripts");
  if (subs != NULL) error2("too many subscripts");
  if (bitp) {
    size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    *bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
    index = index>>(sizeof(int)==4 ? 5 : 4);
  }
  return arrayref(array, index, size);
}

static void rslice (object *array, int size, int slice, object *dims, object *args) {
  checkstack();
  int d = first(dims)->integer;
  for (int i = 0; i < d; i++) {
    int index = slice * d + i;
    if (!consp(args)) error2("initial contents don't match array type");
    if (cdr(dims) == NULL) {
      object **p = arrayref(array, index, size);
      *p = car(args);
    } else rslice(array, size, index, cdr(dims), car(args));
    args = cdr(args);
  }
}

static object *readarray (int d, object *args) {
  object *list = args;
  object *dims = NULL; object *head = NULL;
  int size = 1;
  for (int i = 0; i < d; i++) {
    if (!listp(list)) error2("initial contents don't match array type");
    int l = listlength(list);
    if (dims == NULL) { dims = cons(number(l), NULL); head = dims; }
    else { cdr(dims) = cons(number(l), NULL); dims = cdr(dims); }
    size = size * l;
    if (list != NULL) list = car(list);
  }
  object *array = makearray(head, NULL, false);
  rslice(array, size, 0, head, args);
  return array;
}

static object *readbitarray (gfun_t gfun) {
  char ch = gfun();
  object *head = NULL;
  object *tail = NULL;
  while (!issp(ch) && !isbr(ch)) {
    if (ch != '0' && ch != '1') error2("illegal character in bit array");
    object *cell = cons(number(ch - '0'), NULL);
    if (head == NULL) head = cell;
    else tail->cdr = cell;
    tail = cell;
    ch = gfun();
  }
  LastChar = ch;
  int size = listlength(head);
  object *array = makearray(cons(number(size), NULL), number(0), true);
  size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
  int index = 0;
  while (head != NULL) {
    object **loc = arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size);
    int bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
    *loc = number((((*loc)->integer) & ~(1<<bit)) | (car(head)->integer)<<bit);
    index++;
    head = cdr(head);
  }
  return array;
}

static void pslice (object *array, int size, int slice, object *dims, pfun_t pfun, bool bitp) {
  checkstack();
  bool spaces = true;
  if (slice == -1) { spaces = false; slice = 0; }
  int d = first(dims)->integer;
  if (d < 0) d = -d;
  for (int i = 0; i < d; i++) {
    if (i && spaces) pfun(' ');
    int index = slice * d + i;
    if (cdr(dims) == NULL) {
      if (bitp) pint(((*arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size))->integer)>>
        (index & (sizeof(int)==4 ? 0x1F : 0x0F)) & 1, pfun);
      else printobject(*arrayref(array, index, size), pfun);
    } else { pfun('('); pslice(array, size, index, cdr(dims), pfun, bitp); pfun(')'); }
    testescape();
  }
}

static void printarray (object *array, pfun_t pfun) {
  object *dimensions = cddr(array);
  object *dims = dimensions;
  bool bitp = false;
  int size = 1, n = 0;
  while (dims != NULL) {
    int d = car(dims)->integer;
    if (d < 0) { bitp = true; d = -d; }
    size = size * d;
    dims = cdr(dims); n++;
  }
  if (bitp) size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
  pfun('#');
  if (n == 1 && bitp) { pfun('*'); pslice(array, size, -1, dimensions, pfun, bitp); }
  else {
    if (n > 1) { pint(n, pfun); pfun('A'); }
    pfun('('); pslice(array, size, 0, dimensions, pfun, bitp); pfun(')');
  }
}

// String utilities

static void indent (uint8_t spaces, char ch, pfun_t pfun) {
  for (uint8_t i=0; i<spaces; i++) pfun(ch);
}

static object *startstring (void) {
  object *string = newstring();
  GlobalString = string;
  GlobalStringTail = string;
  return string;
}

static object *princtostring (object *arg) {
  object *obj = startstring();
  prin1object(arg, pstr);
  return obj;
}

static void buildstring (char ch, object** tail) {
  object* cell;
  if (cdr(*tail) == NULL) {
    cell = myalloc(); cdr(*tail) = cell;
  } else if (((*tail)->chars & 0xFFFFFF) == 0) {
    (*tail)->chars |= ch<<16; return;
  } else if (((*tail)->chars & 0xFFFF) == 0) {
    (*tail)->chars |= ch<<8; return;
  } else if (((*tail)->chars & 0xFF) == 0) {
    (*tail)->chars |= ch; return;
  } else {
    cell = myalloc(); car(*tail) = cell;
  }
  car(cell) = NULL; cell->chars = ch<<24; *tail = cell;
}

static object *readstring (uint8_t delim, bool esc, gfun_t gfun) {
  object *obj = newstring();
  object *tail = obj;
  int ch = gfun();
  if (ch == -1) return nil;
  while ((ch != delim) && (ch != -1)) {
    if (esc && ch == '\\') ch = gfun();
    buildstring(ch, &tail);
    ch = gfun();
  }
  return obj;
}

static int stringlength (object *form) {
  int length = 0;
  form = cdr(form);
  while (form != NULL) {
    int chars = form->chars;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      if (chars>>i & 0xFF) length++;
    }
    form = car(form);
  }
  return length;
}

static object **getcharplace (object *string, int n, int *shift) {
  object **arg = &cdr(string);
  int top;
  if (sizeof(int) == 4) { top = n>>2; *shift = 3 - (n&3); }
  else { top = n>>1; *shift = 1 - (n&1); }
  *shift = - (*shift + 2);
  for (int i=0; i<top; i++) {
    if (*arg == NULL) break;
    arg = &car(*arg);
  }
  return arg;
}

static uint8_t nthchar (object *string, int n) {
  int shift;
  object **arg = getcharplace(string, n, &shift);
  if (*arg == NULL) return 0;
  return (((*arg)->chars)>>((-shift-2)<<3)) & 0xFF;
}

static int gstr (void) {
  char c = nthchar(GlobalString, GlobalStringIndex++);
  if (c != 0) return c;
  return -1;
}

static void pstr (char c) {
  buildstring(c, &GlobalStringTail);
}

static object *lispstring (char *s) {
  object *obj = newstring();
  object *tail = obj;
  while(1) {
    char ch = *s++;
    if (ch == 0) break;
    if (ch == '\\') ch = *s++;
    buildstring(ch, &tail);
  }
  return obj;
}

static int stringcompare (object *args, bool lt, bool gt, bool eq) {
  object *arg1 = checkstring(first(args));
  object *arg2 = checkstring(second(args));
  arg1 = cdr(arg1); arg2 = cdr(arg2);
  int m = 0; chars_t a = 0, b = 0;
  while ((arg1 != NULL) || (arg2 != NULL)) {
    if (arg1 == NULL) return lt ? m : -1;
    if (arg2 == NULL) return gt ? m : -1;
    a = arg1->chars; b = arg2->chars;
    if (a < b) { if (lt) { m = m + sizeof(int); while (a != b) { m--; a = a >> 8; b = b >> 8; } return m; } else return -1; }
    if (a > b) { if (gt) { m = m + sizeof(int); while (a != b) { m--; a = a >> 8; b = b >> 8; } return m; } else return -1; }
    arg1 = car(arg1); arg2 = car(arg2);
    m = m + sizeof(int);
  }
  if (eq) { m = m - sizeof(int); while (a != 0) { m++; a = a << 8;} return m;} else return -1;
}

static object *documentation (object *arg, object *env) {
  if (arg == NULL) return nil;
  if (!symbolp(arg)) error(notasymbol, arg);
  object *pair = findpair(arg, env);
  if (pair != NULL) {
    object *val = cdr(pair);
    if (listp(val) && first(val)->name == sym(LAMBDA) && cdr(val) != NULL && cddr(val) != NULL) {
      if (stringp(third(val))) return third(val);
    }
  }
  symbol_t docname = arg->name;
  if (!builtinp(docname)) return nil;
  int length = sys_docopen(builtin(docname));
  if (length <= 0) return nil;
  object *obj = startstring();
  while (length-- > 0) pstr(sys_fgetc(SYS_FTMP));
  sys_fclose(SYS_FTMP);
  return obj;
}

static object *apropos (object *arg, bool print) {
  char buf[17], buf2[33];
  char *part = cstring(princtostring(arg), buf, 17);
  object *result = cons(NULL, NULL);
  object *ptr = result;
  // User-defined?
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    object *val = cdr(pair);
    char *full = cstring(princtostring(var), buf2, 33);
    if (strstr(full, part) != NULL) {
      if (print) {
        printsymbol(var, pserial); pserial(' '); pserial('(');
        if (consp(val) && isbuiltin(car(val), LAMBDA)) pfstring("user function", pserial);
        else if (consp(val) && car(val)->type == CODE) pfstring("code", pserial);
        else pfstring("user symbol", pserial);
        pserial(')'); pln(pserial);
      } else {
        cdr(ptr) = cons(var, NULL); ptr = cdr(ptr);
      }
    }
    globals = cdr(globals);
    testescape();
  }
  // Built-in?
  int entries = tablesize(0) + tablesize(1);
  for (int i = 0; i < entries; i++) {
    if (findsubstring(part, (builtin_t)i)) {
      if (print) {
        uint8_t fntype = fntype(i);
        pbuiltin((builtin_t)i, pserial); pserial(' '); pserial('(');
        if (fntype == FUNCTIONS) pfstring("function", pserial);
        else if (fntype == SPECIAL_FORMS || fntype == TAIL_FORMS) pfstring("special form", pserial);
        else pfstring("symbol/keyword", pserial);
        pserial(')'); pln(pserial);
      } else {
        cdr(ptr) = cons(bsymbol(i), NULL); ptr = cdr(ptr);
      }
    }
    testescape();
  }
  return cdr(result);
}

static char *cstring (object *form, char *buffer, int buflen) {
  form = cdr(checkstring(form));
  int index = 0;
  while (form != NULL) {
    int chars = form->integer;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (ch) {
        if (index >= buflen-1) error2("no room for string");
        buffer[index++] = ch;
      }
    }
    form = car(form);
  }
  buffer[index] = '\0';
  return buffer;
}

static object *value (symbol_t n, object *env) {
  while (env != NULL) {
    object *pair = car(env);
    #if !defined(BOARD_HAS_PSRAM)
    if (pair != NULL && car(pair)->name == n) return pair;
    #else
    if (pair != NULL && eqsymbol(car(pair)->name, n)) return pair;
    #endif
    env = cdr(env);
  }
  return nil;
}

static object *findpair (object *var, object *env) {
  symbol_t name = var->name;
  object *pair = value(name, env);
  if (pair == NULL) pair = value(name, GlobalEnv);
  return pair;
}

static bool boundp (object *var, object *env) {
  if (!symbolp(var)) error(notasymbol, var);
  return (findpair(var, env) != NULL);
}

static object *findvalue (object *var, object *env) {
  object *pair = findpair(var, env);
  if (pair == NULL) error("unknown variable", var);
  return pair;
}

// Handling closures

static object *closure (int tc, symbol_t name, object *function, object *args, object **env) {
  object *state = car(function);
  function = cdr(function);
  int trace = tracing(name);
  if (trace) {
    indent(TraceDepth[trace-1]<<1, ' ', pserial);
    pint(TraceDepth[trace-1]++, pserial);
    pserial(':'); pserial(' '); pserial('('); printsymbol(symbol(name), pserial);
  }
  object *params = first(function);
  if (!listp(params)) errorsym(name, notalist, params);
  function = cdr(function);
  // Dropframe
  if (tc) {
    if (*env != NULL && car(*env) == NULL) {
      pop(*env);
      while (*env != NULL && car(*env) != NULL) pop(*env);
    } else push(nil, *env);
  }
  // Push state
  while (consp(state)) {
    object *pair = first(state);
    push(pair, *env);
    state = cdr(state);
  }
  // Add arguments to environment
  bool optional = false;
  while (params != NULL) {
    object *value;
    object *var = first(params);
    if (isbuiltin(var, OPTIONAL)) optional = true;
    else {
      if (consp(var)) {
        if (!optional) errorsym(name, "invalid default value", var);
        if (args == NULL) value = eval(second(var), *env);
        else { value = first(args); args = cdr(args); }
        var = first(var);
        if (!symbolp(var)) errorsym(name, "illegal optional parameter", var);
      } else if (!symbolp(var)) {
        errorsym(name, "illegal function parameter", var);
      } else if (isbuiltin(var, AMPREST)) {
        params = cdr(params);
        var = first(params);
        value = args;
        args = NULL;
      } else {
        if (args == NULL) {
          if (optional) value = nil;
          else errorsym2(name, toofewargs);
        } else { value = first(args); args = cdr(args); }
      }
      push(cons(var,value), *env);
      if (trace) { pserial(' '); printobject(value, pserial); }
    }
    params = cdr(params);
  }
  if (args != NULL) errorsym2(name, toomanyargs);
  if (trace) { pserial(')'); pln(pserial); }
  // Do an implicit progn
  if (tc) push(nil, *env);
  return tf_progn(function, *env);
}

static object *apply (object *function, object *args, object *env) {
  checkstack();
  if (symbolp(function)) {
    builtin_t fname = builtin(function->name);
    if ((fname < ENDFUNCTIONS) && (fntype(fname) == FUNCTIONS)) {
      Context = fname;
      checkargs(args);
      return ((fn_ptr_type)lookupfn(fname))(args, env);
    } else function = eval(function, env);
  }
  if (consp(function) && isbuiltin(car(function), LAMBDA)) {
    object *result = closure(0, sym(NIL), function, args, &env);
    return eval(result, env);
  }
  if (consp(function) && isbuiltin(car(function), CLOSURE)) {
    function = cdr(function);
    object *result = closure(0, sym(NIL), function, args, &env);
    return eval(result, env);
  }
  error(illegalfn, function);
  return NULL;
}

// In-place operations

static object **place (object *args, object *env, int *bit) {
  *bit = -1;
  if (atom(args)) return &cdr(findvalue(args, env));
  object* function = first(args);
  if (symbolp(function)) {
    symbol_t sname = function->name;
    if (sname == sym(CAR) || sname == sym(FIRST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(canttakecar, value);
      return &car(value);
    }
    if (sname == sym(CDR) || sname == sym(REST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(canttakecdr, value);
      return &cdr(value);
    }
    if (sname == sym(NTH)) {
      int index = checkinteger(eval(second(args), env));
      object *list = eval(third(args), env);
      if (atom(list)) { Context = NTH; error("second argument is not a list", list); }
      int i = index;
      while (i > 0) {
        list = cdr(list);
        if (list == NULL) { Context = NTH; error(indexrange, number(index)); }
        i--;
      }
      return &car(list);
    }
    if (sname == sym(CHAR)) {
      int index = checkinteger(eval(third(args), env));
      object *string = checkstring(eval(second(args), env));
      object **loc = getcharplace(string, index, bit);
      if ((*loc) == NULL || (((((*loc)->chars)>>((-(*bit)-2)<<3)) & 0xFF) == 0)) { Context = CHAR; error(indexrange, number(index)); }
      return loc;
    }
    if (sname == sym(AREF)) {
      object *array = eval(second(args), env);
      if (!arrayp(array)) { Context = AREF; error("first argument is not an array", array); }
      return getarray(array, cddr(args), env, bit);
    }
  }
  error2("illegal place");
  return nil;
}

// Checked car and cdr

static object *carx (object *arg) {
  if (!listp(arg)) error(canttakecar, arg);
  if (arg == nil) return nil;
  return car(arg);
}

static object *cdrx (object *arg) {
  if (!listp(arg)) error(canttakecdr, arg);
  if (arg == nil) return nil;
  return cdr(arg);
}

static object *cxxxr (object *args, uint8_t pattern) {
  object *arg = first(args);
  while (pattern != 1) {
    if ((pattern & 1) == 0) arg = carx(arg); else arg = cdrx(arg);
    pattern = pattern>>1;
  }
  return arg;
}

// Mapping helper functions

static object *mapcl (object *args, object *env, bool mapl) {
  object *function = first(args);
  args = cdr(args);
  object *result = first(args);
  protect(result);
  object *params = cons(NULL, NULL);
  protect(params);
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         unprotect(); unprotect();
         return result;
      }
      if (improperp(list)) error(notproper, list);
      object *item = mapl ? list : first(list);
      object *obj = cons(item, NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    apply(function, cdr(params), env);
  }
}

static void mapcarfun (object *result, object **tail) {
  object *obj = cons(result,NULL);
  cdr(*tail) = obj; *tail = obj;
}

static void mapcanfun (object *result, object **tail) {
  if (cdr(*tail) != NULL) error(notproper, *tail);
  while (consp(result)) {
    cdr(*tail) = result; *tail = result;
    result = cdr(result);
  }
}

static object *mapcarcan (object *args, object *env, mapfun_t fun, bool maplist) {
  checkstack();
  object *function = first(args);
  args = cdr(args);
  object *params = cons(NULL, NULL);
  protect(params);
  object *head = cons(NULL, NULL);
  protect(head);
  object *tail = head;
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         unprotect(); unprotect();
         return cdr(head);
      }
      if (improperp(list)) error(notproper, list);
      object *item = maplist ? list : first(list);
      object *obj = cons(item, NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    object *result = apply(function, cdr(params), env);
    fun(result, &tail);
  }
}

static object *dobody (object *args, object *env, bool star) {
  object *varlist = first(args), *endlist = second(args);
  object *head = cons(NULL, NULL);
  protect(head);
  object *ptr = head;
  object *newenv = env;
  protect(newenv);
  while (varlist != NULL) {
    object *varform = first(varlist);
    object *var, *init = NULL, *step = NULL;
    if (atom(varform)) var = varform;
    else {
      var = first(varform);
      varform = cdr(varform);
      if (varform != NULL) {
        init = eval(first(varform), env);
        varform = cdr(varform);
        if (varform != NULL) step = cons(first(varform), NULL);
      }
    }
    object *pair = cons(var, init);
    unprotect(); // newenv
    push(pair, newenv);
    protect(newenv);
    if (star) env = newenv;
    object *cell = cons(cons(step, pair), NULL);
    cdr(ptr) = cell; ptr = cdr(ptr);
    varlist = cdr(varlist);
  }
  env = newenv;
  head = cdr(head);
  object *endtest = first(endlist), *results = cdr(endlist);
  while (eval(endtest, env) == NULL) {
    object *forms = cddr(args);
    while (forms != NULL) {
      object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        unprotect(); // newenv
        unprotect(); // head
        return result;
      }
      forms = cdr(forms);
    }
    object *varlist = head;
    int count = 0;
    while (varlist != NULL) {
      object *varform = first(varlist);
      object *step = car(varform), *pair = cdr(varform);
      if (step != NULL) {
        object *val = eval(first(step), env);
        if (star) {
          cdr(pair) = val;
        } else {
          push(val, GCStack);
          push(pair, GCStack);
          count++;
        }
      }
      varlist = cdr(varlist);
    }
    while (count > 0) {
      cdr(car(GCStack)) = car(cdr(GCStack));
      pop(GCStack); pop(GCStack);
      count--;
    }
  }
  unprotect(); // newenv
  unprotect(); // head
  return eval(tf_progn(results, env), env);
}

// Streams


static void SDwrite (char c) { sys_fputc(SYS_FOUT, c); }
static void gfxwrite (char c) { gfx_write(c); }
static int SDread (void) { return sys_fgetc(SYS_FIN); }

// Stream writing functions

static pfun_t pfun_serial (uint8_t address) {
  (void) address;
  return pserial;
}

static pfun_t pfun_string (uint8_t address) {
  (void) address;
  return pstr;
}

static pfun_t pfun_sd (uint8_t address) {
  (void) address;
  return SDwrite;
}

static pfun_t pfun_gfx (uint8_t address) {
  (void) address;
  return gfxwrite;
}

// Stream reading functions

static gfun_t gfun_serial (uint8_t address) {
  (void) address;
  return gserial;
}

static gfun_t gfun_sd (uint8_t address) {
  (void) address;
  return SDread;
}

// Stream names used by printobject
static const char serialstreamname[] = "serial";
static const char sdstreamname[] = "sd";
static const char stringstreamname[] = "string";
static const char gfxstreamname[] = "gfx";

// Stream lookup table - needs to be in same order as enum stream
static const stream_entry_t stream_table[] = {
  { serialstreamname, pfun_serial, gfun_serial },
  { sdstreamname, pfun_sd, gfun_sd },
  { stringstreamname, pfun_string, NULL },
  { gfxstreamname, pfun_gfx, NULL },
};

// Stream table cross-reference functions

static stream_entry_t *streamtables[] = {stream_table, NULL};

static const stream_entry_t *streamtable (int n) {
  return streamtables[n];
}

static pfun_t pstreamfun (object *args) {
  nstream_t nstream = SERIALSTREAM;
  int address = 0;
  pfun_t pfun = pserial;
  if (args != NULL && first(args) != NULL) {
    int stream = isstream(first(args));
    nstream = stream>>8; address = stream & 0xFF;
  }
  bool n = nstream<USERSTREAMS;
  pstream_ptr_t streamfunction = streamtable(n?0:1)[n?nstream:nstream-USERSTREAMS].pfunptr;
  pfun = streamfunction(address);
  return pfun;
}

static gfun_t gstreamfun (object *args) {
  nstream_t nstream = SERIALSTREAM;
  int address = 0;
  gfun_t gfun = gserial;
  if (args != NULL) {
    int stream = isstream(first(args));
    nstream = stream>>8; address = stream & 0xFF;
  }
  bool n = nstream<USERSTREAMS;
  gstream_ptr_t streamfunction = streamtable(n?0:1)[n?nstream:nstream-USERSTREAMS].gfunptr;
  gfun = streamfunction(address);
  return gfun;
}

// Check pins - these are board-specific not processor-specific

static void checkanalogread (int pin) {
  if (!(pin>=26 && pin<=29)) error(invalidpin, number(pin));
}

// Note

static const int scale[] = {4186,4435,4699,4978,5274,5588,5920,6272,6645,7040,7459,7902};

static void playnote (int pin, int note, int octave) {
  (void) pin;
  int oct = octave + note/12;
  int prescaler = 8 - oct;
  if (prescaler<0 || prescaler>8) error("octave out of range", number(oct));
  sys_tone(scale[note%12]>>prescaler);
}

static void nonote (int pin) {
  (void) pin;
  sys_tone(0);
}

// Sleep

static void doze (int secs) {
  sys_delay(1000 * secs);
}

// Prettyprint

static const int PPINDENT = 2;
static const int PPWIDTH = 52;
static const int GFXPPWIDTH = 52; // 320 pixel wide screen
static int ppwidth = PPWIDTH;

static void pcount (char c) {
  if (c == '\n') PrintCount++;
  PrintCount++;
}

static uint8_t atomwidth (object *obj) {
  PrintCount = 0;
  printobject(obj, pcount);
  return PrintCount;
}

static uint8_t basewidth (object *obj, uint8_t base) {
  PrintCount = 0;
  pintbase(obj->integer, base, pcount);
  return PrintCount;
}

static bool quoted (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(QUOTE) && consp(cdr(obj)) && cddr(obj) == NULL);
}

static int subwidth (object *obj, int w) {
  checkstack();
  if (atom(obj)) return w - atomwidth(obj);
  if (quoted(obj)) obj = car(cdr(obj));
  return subwidthlist(obj, w - 1);
}

static int subwidthlist (object *form, int w) {
  while (form != NULL && w >= 0) {
    if (atom(form)) return w - (2 + atomwidth(form));
    w = subwidth(car(form), w - 1);
    form = cdr(form);
  }
  return w;
}

static void superprint (object *form, int lm, pfun_t pfun) {
  checkstack();
  if (atom(form)) {
    if (isbuiltin(form, NOTHING)) printsymbol(form, pfun);
    else printobject(form, pfun);
  } else if (quoted(form)) {
    pfun('\'');
    superprint(car(cdr(form)), lm + 1, pfun);
  } else {
    lm = lm + PPINDENT;
    bool fits = (subwidth(form, ppwidth - lm - PPINDENT) >= 0);
    int special = 0, extra = 0; bool separate = true;
    object *arg = car(form);
    if (symbolp(arg) && builtinp(arg->name)) {
      uint8_t minmax = getminmax(builtin(arg->name));
      if (minmax == 0327 || minmax == 0313) special = 2; // defun, setq, setf, defvar
      else if (minmax == 0317 || minmax == 0017 || minmax == 0117 || minmax == 0123) special = 1;
    }
    while (form != NULL) {
      if (atom(form)) { pfstring(" . ", pfun); printobject(form, pfun); pfun(')'); return; }
      else if (separate) {
        pfun('(');
        separate = false;
      } else if (special) {
        pfun(' ');
        special--;
      } else if (fits) {
        pfun(' ');
      } else { pln(pfun); indent(lm, ' ', pfun); }
      superprint(car(form), lm+extra, pfun);
      form = cdr(form);
    }
    pfun(')');
    testescape();
  }
}

static object *edit (object *fun) {
  checkstack();
  while (1) {
    if (tstflag(EXITEDITOR)) return fun;
    char c = gserial();
    if (c == 'q') setflag(EXITEDITOR);
    else if (c == 'b') return fun;
    else if (c == 'r') fun = read(gserial);
    else if (c == '\n') { pfl(pserial); superprint(fun, 0, pserial); pln(pserial); }
    else if (c == 'c') fun = cons(read(gserial), fun);
    else if (atom(fun)) pserial('!');
    else if (c == 'd') fun = cons(car(fun), edit(cdr(fun)));
    else if (c == 'a') fun = cons(edit(car(fun)), cdr(fun));
    else if (c == 'x') fun = cdr(fun);
    else pserial('?');
  }
}

// Assembler

static object *call (int entry, int nargs, object *args, object *env) {
  (void) env;
  int param[4] = {0, 0, 0, 0};
  for (int i=0; i<nargs; i++) {
    object *arg = first(args);
    if (integerp(arg)) param[i] = arg->integer;
    else param[i] = (uintptr_t)arg;
    args = cdr(args);
  }
  int w;
  if (!sys_callcode(&MyCode[entry], param, &w)) error2("machine code needs an ARM Thumb processor");
  return number(w);
}

static void putcode (object *arg, int origin, int pc) {
  int code = checkinteger(arg);
  MyCode[origin+pc] = code & 0xff;
  MyCode[origin+pc+1] = (code>>8) & 0xff;
  printhex4(pc, pserial);
  printhex4(code, pserial);
}

static int assemble (int pass, int origin, object *entries, object *env, object *pcpair) {
  int pc = 0; cdr(pcpair) = number(pc);
  while (entries != NULL) {
    object *arg = first(entries);
    if (symbolp(arg)) {
      if (pass == 2) {
        printhex4(pc, pserial);
        indent(5, ' ', pserial);
        printobject(arg, pserial); pln(pserial);
      } else {
        object *pair = findvalue(arg, env);
        cdr(pair) = number(pc);
      }
    } else {
      object *argval = eval(arg, env);
      if (listp(argval)) {
        object *arglist = argval;
        while (arglist != NULL) {
          if (pass == 2) {
            putcode(first(arglist), origin, pc);
            if (arglist == argval) superprint(arg, 0, pserial);
            pln(pserial);
          }
          pc = pc + 2;
          cdr(pcpair) = number(pc);
          arglist = cdr(arglist);
        }
      } else if (integerp(argval)) {
        if (pass == 2) {
          putcode(argval, origin, pc);
          superprint(arg, 0, pserial); pln(pserial);
        }
        pc = pc + 2;
        cdr(pcpair) = number(pc);
      } else error("illegal entry", arg);
    }
    entries = cdr(entries);
  }
  // Round up to multiple of 4 to give code size
  if (pc%4 != 0) pc = pc + 4 - pc%4;
  return pc;
}

// Special forms

static object *sp_quote (object *args, object *env) {
  (void) env;
  return first(args);
}

static object *sp_or (object *args, object *env) {
  while (args != NULL) {
    object *val = eval(car(args), env);
    if (val != NULL) return val;
    args = cdr(args);
  }
  return nil;
}

static object *sp_defun (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  object *val = cons(bsymbol(LAMBDA), cdr(args));
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

static object *sp_defvar (object *args, object *env) {
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  object *val = NULL;
  args = cdr(args);
  if (args != NULL) { setflag(NOESC); val = eval(first(args), env); clrflag(NOESC); }
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

static object *sp_setq (object *args, object *env) {
  object *arg = nil; builtin_t setq = Context;
  while (args != NULL) {
    if (cdr(args) == NULL) { Context = setq; error2(oddargs); }
    object *pair = findvalue(first(args), env);
    arg = eval(second(args), env);
    cdr(pair) = arg;
    args = cddr(args);
  }
  return arg;
}

static object *sp_loop (object *args, object *env) {
  object *start = args;
  for (;;) {
    args = start;
    while (args != NULL) {
      object *result = eval(car(args),env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        return result;
      }
      args = cdr(args);
    }
    testescape();
  }
}

static object *sp_push (object *args, object *env) {
  int bit;
  object *item = eval(first(args), env);
  object **loc = place(second(args), env, &bit);
  if (bit != -1) error2(invalidarg);
  push(item, *loc);
  return *loc;
}

static object *sp_pop (object *args, object *env) {
  int bit;
  object *arg = first(args);
  if (arg == NULL) error2(invalidarg);
  object **loc = place(arg, env, &bit);
  if (bit < -1) error(invalidarg, arg);
  if (!consp(*loc)) error(notalist, *loc);
  object *result = car(*loc);
  pop(*loc);
  return result;
}

// Accessors

static object *sp_incf (object *args, object *env) {
  int bit;
  object **loc = place(first(args), env, &bit);
  if (bit < -1) error2(notanumber);
  args = cdr(args);

  object *x = *loc;
  object *inc = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int increment;
    if (inc == NULL) increment = 1; else increment = checkbitvalue(inc);
    int newvalue = (((*loc)->integer)>>bit & 1) + increment;

    if (newvalue & ~1) error2("result is not a bit value");
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(inc)) {
    float increment;
    float value = checkintfloat(x);

    if (inc == NULL) increment = 1.0; else increment = checkintfloat(inc);

    *loc = makefloat(value + increment);
  } else if (integerp(x) && (integerp(inc) || inc == NULL)) {
    int increment;
    int value = x->integer;

    if (inc == NULL) increment = 1; else increment = inc->integer;

    if (increment < 1) {
      if (INT_MIN - increment > value) *loc = makefloat((float)value + (float)increment);
      else *loc = number(value + increment);
    } else {
      if (INT_MAX - increment < value) *loc = makefloat((float)value + (float)increment);
      else *loc = number(value + increment);
    }
  } else error2(notanumber);
  return *loc;
}

static object *sp_decf (object *args, object *env) {
  int bit;
  object **loc = place(first(args), env, &bit);
  if (bit < -1) error2(notanumber);
  args = cdr(args);

  object *x = *loc;
  object *dec = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int decrement;
    if (dec == NULL) decrement = 1; else decrement = checkbitvalue(dec);
    int newvalue = (((*loc)->integer)>>bit & 1) - decrement;

    if (newvalue & ~1) error2("result is not a bit value");
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(dec)) {
    float decrement;
    float value = checkintfloat(x);

    if (dec == NULL) decrement = 1.0; else decrement = checkintfloat(dec);

    *loc = makefloat(value - decrement);
  } else if (integerp(x) && (integerp(dec) || dec == NULL)) {
    int decrement;
    int value = x->integer;

    if (dec == NULL) decrement = 1; else decrement = dec->integer;

    if (decrement < 1) {
      if (INT_MAX + decrement < value) *loc = makefloat((float)value - (float)decrement);
      else *loc = number(value - decrement);
    } else {
      if (INT_MIN + decrement > value) *loc = makefloat((float)value - (float)decrement);
      else *loc = number(value - decrement);
    }
  } else error2(notanumber);
  return *loc;
}

static object *sp_setf (object *args, object *env) {
  int bit; builtin_t setf = Context;
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) { Context = setf; error2(oddargs); }
    object **loc = place(first(args), env, &bit);
    arg = eval(second(args), env);
    if (bit == -1) *loc = arg;
    else if (bit < -1) (*loc)->chars = ((*loc)->chars & ~(0xff<<((-bit-2)<<3))) | checkchar(arg)<<((-bit-2)<<3);
    else *loc = number((checkinteger(*loc) & ~(1<<bit)) | checkbitvalue(arg)<<bit);
    args = cddr(args);
  }
  return arg;
}

// Other special forms

static object *sp_dolist (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  object *list = eval(second(params), env);
  protect(list); // Don't GC the list
  object *pair = cons(var,nil);
  push(pair,env);
  params = cddr(params);
  args = cdr(args);
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    cdr(pair) = first(list);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        unprotect();
        return result;
      }
      forms = cdr(forms);
    }
    list = cdr(list);
  }
  cdr(pair) = nil;
  unprotect();
  if (params == NULL) return nil;
  return eval(car(params), env);
}

static object *sp_dotimes (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  int count = checkinteger(eval(second(params), env));
  int index = 0;
  params = cddr(params);
  object *pair = cons(var,number(0));
  push(pair,env);
  args = cdr(args);
  while (index < count) {
    cdr(pair) = number(index);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        return result;
      }
      forms = cdr(forms);
    }
    index++;
  }
  cdr(pair) = number(index);
  if (params == NULL) return nil;
  return eval(car(params), env);
}

static object *sp_do (object *args, object *env) {
  return dobody(args, env, false);
}

static object *sp_dostar (object *args, object *env) {
  return dobody(args, env, true);
}

static object *sp_trace (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *var = first(args);
    if (!symbolp(var)) error(notasymbol, var);
    trace(var->name);
    args = cdr(args);
  }
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
    i++;
  }
  return args;
}

static object *sp_untrace (object *args, object *env) {
  (void) env;
  if (args == NULL) {
    int i = 0;
    while (i < TRACEMAX) {
      if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
      TraceFn[i] = 0;
      i++;
    }
  } else {
    while (args != NULL) {
      object *var = first(args);
      if (!symbolp(var)) error(notasymbol, var);
      untrace(var->name);
      args = cdr(args);
    }
  }
  return args;
}

static object *sp_formillis (object *args, object *env) {
  object *param = checkarguments(args, 0, 1);
  unsigned long start = sys_millis();
  unsigned long now, total = 0;
  if (param != NULL) total = checkinteger(eval(first(param), env));
  eval(tf_progn(cdr(args),env), env);
  do {
    now = sys_millis() - start;
    testescape();
  } while (now < total);
  if (now <= INT_MAX) return number(now);
  return nil;
}

static object *sp_time (object *args, object *env) {
  unsigned long start = sys_millis();
  object *result = eval(first(args), env);
  unsigned long elapsed = sys_millis() - start;
  printobject(result, pserial);
  pfstring("\nTime: ", pserial);
  if (elapsed < 1000) {
    pint(elapsed, pserial);
    pfstring(" ms\n", pserial);
  } else {
    elapsed = elapsed+50;
    pint(elapsed/1000, pserial);
    pserial('.'); pint((elapsed/100)%10, pserial);
    pfstring(" s\n", pserial);
  }
  return bsymbol(NOTHING);
}

static object *sp_withoutputtostring (object *args, object *env) {
  object *params = checkarguments(args, 1, 1);
  object *var = first(params);
  object *pair = cons(var, stream(STRINGSTREAM, 0));
  push(pair,env);
  object *string = startstring();
  protect(string);
  object *forms = cdr(args);
  eval(tf_progn(forms,env), env);
  unprotect();
  return string;
}

static object *sp_withsdcard (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  params = cdr(params);
  if (params == NULL) error2("no filename specified");
  builtin_t temp = Context;
  object *filename = eval(first(params), env);
  Context = temp;
  if (!stringp(filename)) error("filename is not a string", filename);
  params = cdr(params);
  int mode = 0;
  if (params != NULL && first(params) != NULL) mode = checkinteger(first(params));
  char buffer[PATHSIZE];
  int file = (mode >= 1) ? SYS_FOUT : SYS_FIN;
  if (!sys_fopen(file, MakeFilename(filename, buffer), (mode == 1) ? SYS_APPEND : (mode == 2) ? SYS_WRITE : SYS_READ)) {
    if (mode >= 1) error2("problem writing to file or invalid filename");
    error2("problem reading from file or invalid filename");
  }
  object *pair = cons(var, stream(SDSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  sys_fclose(file);
  return result;
}

// Assembler

static object *sp_defcode (object *args, object *env) {
  setflag(NOESC);
  object *var = first(args);
  object *params = second(args);
  if (!symbolp(var)) error("not a symbol", var);

  // Make parameters into synonyms for registers r0, r1, etc
  int regn = 0;
  while (params != NULL) {
    if (regn > 3) error("more than 4 parameters", var);
    object *regpair = cons(car(params), bsymbol((builtin_t)((toradix40('r')*40+toradix40('0')+regn)*2560000))); // Symbol for r0 etc
    push(regpair,env);
    regn++;
    params = cdr(params);
  }

  // Make *pc* a local variable for program counter
  object *pcpair = cons(bsymbol(PSTAR), number(0));
  push(pcpair,env);

  args = cdr(args);

  // Make labels into local variables
  object *entries = cdr(args);
  while (entries != NULL) {
    object *arg = first(entries);
    if (symbolp(arg)) {
      object *pair = cons(arg,number(0));
      push(pair,env);
    }
    entries = cdr(entries);
  }

  // First pass
  int origin = 0;
  int codesize = assemble(1, origin, cdr(args), env, pcpair);

  // See if it will fit
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = car(globals);
    if (pair != NULL && car(pair) != var && consp(cdr(pair))) { // Exclude me if I already exist
      object *codeid = second(pair);
      if (codeid->type == CODE) {
        codesize = codesize + endblock(codeid) - startblock(codeid);
      }
    }
    globals = cdr(globals);
  }
  if (codesize > CODESIZE) error("not enough room for code", var);

  // Compact the code block, removing gaps
  origin = 0;
  object *block = 0;
  int smallest;

  do {
    smallest = CODESIZE;
    globals = GlobalEnv;
    while (globals != NULL) {
      object *pair = car(globals);
      if (pair != NULL && car(pair) != var && consp(cdr(pair))) { // Exclude me if I already exist
        object *codeid = second(pair);
        if (codeid->type == CODE) {
          if (startblock(codeid) < smallest && startblock(codeid) >= origin) {
            smallest = startblock(codeid);
            block = codeid;
          }
        }
      }
      globals = cdr(globals);
    }

    // Compact fragmentation if necessary
    if (smallest == origin) origin = endblock(block); // No gap
    else if (smallest < CODESIZE) { // Slide block down
      int target = origin;
      for (int i=startblock(block); i<endblock(block); i++) {
        MyCode[target] = MyCode[i];
        target++;
      }
      block->integer = target<<16 | origin;
      origin = target;
    }

  } while (smallest < CODESIZE);

  // Second pass - origin is first free location
  codesize = assemble(2, origin, cdr(args), env, pcpair);

  object *val = cons(codehead((origin+codesize)<<16 | origin), args);
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  clrflag(NOESC);
  return var;
}

// Tail-recursive forms

static object *tf_progn (object *args, object *env) {
  if (args == NULL) return nil;
  object *more = cdr(args);
  while (more != NULL) {
    object *result = eval(car(args),env);
    if (tstflag(RETURNFLAG)) return quote(result);
    args = more;
    more = cdr(args);
  }
  return car(args);
}

static object *tf_if (object *args, object *env) {
  if (args == NULL || cdr(args) == NULL) error2(toofewargs);
  if (eval(first(args), env) != nil) return second(args);
  args = cddr(args);
  return (args != NULL) ? first(args) : nil;
}

static object *tf_cond (object *args, object *env) {
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(illegalclause, clause);
    object *test = eval(first(clause), env);
    object *forms = cdr(clause);
    if (test != nil) {
      if (forms == NULL) return quote(test); else return tf_progn(forms, env);
    }
    args = cdr(args);
  }
  return nil;
}

static object *tf_when (object *args, object *env) {
  if (args == NULL) error2(noargument);
  if (eval(first(args), env) != nil) return tf_progn(cdr(args),env);
  else return nil;
}

static object *tf_unless (object *args, object *env) {
  if (args == NULL) error2(noargument);
  if (eval(first(args), env) != nil) return nil;
  else return tf_progn(cdr(args),env);
}

static object *tf_case (object *args, object *env) {
  object *test = eval(first(args), env);
  args = cdr(args);
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(illegalclause, clause);
    object *key = car(clause);
    object *forms = cdr(clause);
    if (consp(key)) {
      while (key != NULL) {
        if (eq(test,car(key))) return tf_progn(forms, env);
        key = cdr(key);
      }
    } else if (eq(test,key) || eq(key,tee)) return tf_progn(forms, env);
    args = cdr(args);
  }
  return nil;
}

static object *tf_and (object *args, object *env) {
  if (args == NULL) return tee;
  object *more = cdr(args);
  while (more != NULL) {
    if (eval(car(args), env) == NULL) return nil;
    args = more;
    more = cdr(args);
  }
  return car(args);
}

// Core functions

static object *fn_not (object *args, object *env) {
  (void) env;
  return (first(args) == nil) ? tee : nil;
}

static object *fn_cons (object *args, object *env) {
  (void) env;
  return cons(first(args), second(args));
}

static object *fn_atom (object *args, object *env) {
  (void) env;
  return atom(first(args)) ? tee : nil;
}

static object *fn_listp (object *args, object *env) {
  (void) env;
  return listp(first(args)) ? tee : nil;
}

static object *fn_consp (object *args, object *env) {
  (void) env;
  return consp(first(args)) ? tee : nil;
}

static object *fn_symbolp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (arg == NULL || symbolp(arg)) ? tee : nil;
}

static object *fn_arrayp (object *args, object *env) {
  (void) env;
  return arrayp(first(args)) ? tee : nil;
}

static object *fn_boundp (object *args, object *env) {
  return boundp(first(args), env) ? tee : nil;
}

static object *fn_keywordp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!symbolp(arg)) return nil;
  if (colonp(arg->name)) return tee;
  if (keywordp(arg)) return (number((int)lookupfn(builtin(arg->name))));
  return nil;
}

static object *fn_setfn (object *args, object *env) {
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) error2(oddargs);
    object *pair = findvalue(first(args), env);
    arg = second(args);
    cdr(pair) = arg;
    args = cddr(args);
  }
  return arg;
}

static object *fn_streamp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return streamp(arg) ? tee : nil;
}

static object *fn_eq (object *args, object *env) {
  (void) env;
  return eq(first(args), second(args)) ? tee : nil;
}

static object *fn_equal (object *args, object *env) {
  (void) env;
  return equal(first(args), second(args)) ? tee : nil;
}

// List functions

static object *fn_car (object *args, object *env) {
  (void) env;
  return carx(first(args));
}

static object *fn_cdr (object *args, object *env) {
  (void) env;
  return cdrx(first(args));
}

static object *fn_caar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b100);
}

static object *fn_cadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b101);
}

static object *fn_cdar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b110);
}

static object *fn_cddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b111);
}

static object *fn_caaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1000);
}

static object *fn_caadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1001);
}

static object *fn_cadar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1010);
}

static object *fn_caddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1011);
}

static object *fn_cdaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1100);
}

static object *fn_cdadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1101);
}

static object *fn_cddar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1110);
}

static object *fn_cdddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1111);
}

static object *fn_length (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (listp(arg)) return number(listlength(arg));
  if (stringp(arg)) return number(stringlength(arg));
  if (!(arrayp(arg) && cdr(cddr(arg)) == NULL)) error("argument is not a list, 1d array, or string", arg);
  return number(abs(first(cddr(arg))->integer));
}

static object *fn_arraydimensions (object *args, object *env) {
  (void) env;
  object *array = first(args);
  if (!arrayp(array)) error("argument is not an array", array);
  object *dimensions = cddr(array);
  return (first(dimensions)->integer < 0) ? cons(number(-(first(dimensions)->integer)), cdr(dimensions)) : dimensions;
}

static object *fn_list (object *args, object *env) {
  (void) env;
  return args;
}

static object *fn_copylist (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!listp(arg)) error(notalist, arg);
  object *result = cons(NULL, NULL);
  object *ptr = result;
  while (consp(arg)) {
    cdr(ptr) = cons(car(arg), cdr(arg));
    ptr = cdr(ptr); arg = cdr(arg);
  }
  return cdr(result);
}

static object *fn_makearray (object *args, object *env) {
  (void) env;
  object *def = nil;
  bool bitp = false;
  object *dims = first(args);
  if (dims == NULL) error2("dimensions can't be nil");
  else if (atom(dims)) dims = cons(dims, NULL);
  args = cdr(args);
  while (args != NULL && cdr(args) != NULL) {
    object *var = first(args);
    if (isbuiltin(first(args), INITIALELEMENT)) def = second(args);
    else if (isbuiltin(first(args), ELEMENTTYPE) && isbuiltin(second(args), BIT)) bitp = true;
    else error("argument not recognised", var);
    args = cddr(args);
  }
  if (bitp) {
    if (def == nil) def = number(0);
    else def = number(-checkbitvalue(def)); // 1 becomes all ones
  }
  return makearray(dims, def, bitp);
}

static object *fn_reverse (object *args, object *env) {
  (void) env;
  object *list = first(args);
  object *result = NULL;
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    push(first(list),result);
    list = cdr(list);
  }
  return result;
}

static object *fn_nth (object *args, object *env) {
  (void) env;
  int n = checkinteger(first(args));
  if (n < 0) error(indexnegative, first(args));
  object *list = second(args);
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    if (n == 0) return car(list);
    list = cdr(list);
    n--;
  }
  return nil;
}

static object *fn_aref (object *args, object *env) {
  (void) env;
  int bit;
  object *array = first(args);
  if (!arrayp(array)) error("first argument is not an array", array);
  object *loc = *getarray(array, cdr(args), 0, &bit);
  if (bit == -1) return loc;
  else return number((loc->integer)>>bit & 1);
}

static object *fn_assoc (object *args, object *env) {
  (void) env;
  object *key = first(args);
  object *list = second(args);
  object *test = testargument(cddr(args));
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    object *pair = first(list);
    if (!listp(pair)) error("element is not a list", pair);
    if (pair != NULL && apply(test, cons(key, cons(car(pair), NULL)), env) != NULL) return pair;
    list = cdr(list);
  }
  return nil;
}

static object *fn_member (object *args, object *env) {
  (void) env;
  object *item = first(args);
  object *list = second(args);
  object *test = testargument(cddr(args));
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    if (apply(test, cons(item, cons(car(list), NULL)), env) != NULL) return list;
    list = cdr(list);
  }
  return nil;
}

static object *fn_apply (object *args, object *env) {
  object *previous = NULL;
  object *last = args;
  while (cdr(last) != NULL) {
    previous = last;
    last = cdr(last);
  }
  object *arg = car(last);
  if (!listp(arg)) error(notalist, arg);
  cdr(previous) = arg;
  return apply(first(args), cdr(args), env);
}

static object *fn_funcall (object *args, object *env) {
  return apply(first(args), cdr(args), env);
}

static object *fn_append (object *args, object *env) {
  (void) env;
  object *head = NULL;
  object *tail;
  while (args != NULL) {
    object *list = first(args);
    if (!listp(list)) error(notalist, list);
    while (consp(list)) {
      object *obj = cons(car(list), cdr(list));
      if (head == NULL) head = obj;
      else cdr(tail) = obj;
      tail = obj;
      list = cdr(list);
      if (cdr(args) != NULL && improperp(list)) error(notproper, first(args));
    }
    args = cdr(args);
  }
  return head;
}

static object *fn_mapc (object *args, object *env) {
  return mapcl(args, env, false);
}

static object *fn_mapl (object *args, object *env) {
  return mapcl(args, env, true);
}

static object *fn_mapcar (object *args, object *env) {
  return mapcarcan(args, env, mapcarfun, false);
}

static object *fn_mapcan (object *args, object *env) {
  return mapcarcan(args, env, mapcanfun, false);
}

static object *fn_maplist (object *args, object *env) {
  return mapcarcan(args, env, mapcarfun, true);
}

static object *fn_mapcon (object *args, object *env) {
  return mapcarcan(args, env, mapcanfun, true);
}

// Arithmetic functions

static object *fn_add (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    object *arg = car(args);
    if (floatp(arg)) return add_floats(args, (float)result);
    else if (integerp(arg)) {
      int val = arg->integer;
      if (val < 1) { if (INT_MIN - val > result) return add_floats(args, (float)result); }
      else { if (INT_MAX - val < result) return add_floats(args, (float)result); }
      result = result + val;
    } else error(notanumber, arg);
    args = cdr(args);
  }
  return number(result);
}

static object *fn_subtract (object *args, object *env) {
  (void) env;
  object *arg = car(args);
  args = cdr(args);
  if (args == NULL) return negate(arg);
  else if (floatp(arg)) return subtract_floats(args, arg->single_float);
  else if (integerp(arg)) {
    int result = arg->integer;
    while (args != NULL) {
      arg = car(args);
      if (floatp(arg)) return subtract_floats(args, result);
      else if (integerp(arg)) {
        int val = (car(args))->integer;
        if (val < 1) { if (INT_MAX + val < result) return subtract_floats(args, result); }
        else { if (INT_MIN + val > result) return subtract_floats(args, result); }
        result = result - val;
      } else error(notanumber, arg);
      args = cdr(args);
    }
    return number(result);
  } else error(notanumber, arg);
  return nil;
}

static object *fn_multiply (object *args, object *env) {
  (void) env;
  int result = 1;
  while (args != NULL){
    object *arg = car(args);
    if (floatp(arg)) return multiply_floats(args, result);
    else if (integerp(arg)) {
      int64_t val = result * (int64_t)(arg->integer);
      if ((val > INT_MAX) || (val < INT_MIN)) return multiply_floats(args, result);
      result = val;
    } else error(notanumber, arg);
    args = cdr(args);
  }
  return number(result);
}

static object *fn_divide (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  args = cdr(args);
  // One argument
  if (args == NULL) {
    if (floatp(arg)) {
      float f = arg->single_float;
      if (f == 0.0) error2(divisionbyzero);
      return makefloat(1.0 / f);
    } else if (integerp(arg)) {
      int i = arg->integer;
      if (i == 0) error2(divisionbyzero);
      else if (i == 1) return number(1);
      else return makefloat(1.0 / i);
    } else error(notanumber, arg);
  }
  // Multiple arguments
  if (floatp(arg)) return divide_floats(args, arg->single_float);
  else if (integerp(arg)) {
    int result = arg->integer;
    while (args != NULL) {
      arg = car(args);
      if (floatp(arg)) {
        return divide_floats(args, result);
      } else if (integerp(arg)) {
        int i = arg->integer;
        if (i == 0) error2(divisionbyzero);
        if ((result % i) != 0) return divide_floats(args, result);
        if ((result == INT_MIN) && (i == -1)) return divide_floats(args, result);
        result = result / i;
        args = cdr(args);
      } else error(notanumber, arg);
    }
    return number(result);
  } else error(notanumber, arg);
  return nil;
}

static object *fn_mod (object *args, object *env) {
  (void) env;
  return remmod(args, true);
}

static object *fn_rem (object *args, object *env) {
  (void) env;
  return remmod(args, false);
}

static object *fn_oneplus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) + 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MAX) return makefloat((arg->integer) + 1.0);
    else return number(result + 1);
  } else error(notanumber, arg);
  return nil;
}

static object *fn_oneminus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) - 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat((arg->integer) - 1.0);
    else return number(result - 1);
  } else error(notanumber, arg);
  return nil;
}

static object *fn_abs (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return makefloat(fabsf(arg->single_float));
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(fabsf((float)result));
    else return number(abs(result));
  } else error(notanumber, arg);
  return nil;
}

static object *fn_random (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (integerp(arg)) {
    if (arg->integer <= 0) error("limit must be positive", arg);
    return number(sys_random() % (uint32_t)arg->integer);
  }
  else if (floatp(arg)) return makefloat((float)(sys_random() >> 8) * arg->single_float / 16777216.0f);
  else error(notanumber, arg);
  return nil;
}

static object *fn_maxfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) > (result->integer)) result = arg;
    } else if ((checkintfloat(arg) > checkintfloat(result))) result = arg;
    args = cdr(args);
  }
  return result;
}

static object *fn_minfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) < (result->integer)) result = arg;
    } else if ((checkintfloat(arg) < checkintfloat(result))) result = arg;
    args = cdr(args);
  }
  return result;
}

// Arithmetic comparisons

static object *fn_noteq (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *nargs = args;
    object *arg1 = first(nargs);
    nargs = cdr(nargs);
    while (nargs != NULL) {
      object *arg2 = first(nargs);
      if (integerp(arg1) && integerp(arg2)) {
        if ((arg1->integer) == (arg2->integer)) return nil;
      } else if ((checkintfloat(arg1) == checkintfloat(arg2))) return nil;
      nargs = cdr(nargs);
    }
    args = cdr(args);
  }
  return tee;
}

static object *fn_numeq (object *args, object *env) {
  (void) env;
  return compare(args, false, false, true);
}

static object *fn_less (object *args, object *env) {
  (void) env;
  return compare(args, true, false, false);
}

static object *fn_lesseq (object *args, object *env) {
  (void) env;
  return compare(args, true, false, true);
}

static object *fn_greater (object *args, object *env) {
  (void) env;
  return compare(args, false, true, false);
}

static object *fn_greatereq (object *args, object *env) {
  (void) env;
  return compare(args, false, true, true);
}

static object *fn_plusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) > 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) > 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

static object *fn_minusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) < 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) < 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

static object *fn_zerop (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) == 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) == 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

static object *fn_oddp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(first(args));
  return ((arg & 1) == 1) ? tee : nil;
}

static object *fn_evenp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(first(args));
  return ((arg & 1) == 0) ? tee : nil;
}

// Number functions

static object *fn_integerp (object *args, object *env) {
  (void) env;
  return integerp(first(args)) ? tee : nil;
}

static object *fn_numberp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (integerp(arg) || floatp(arg)) ? tee : nil;
}

// Floating-point functions

static object *fn_floatfn (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (floatp(arg)) ? arg : makefloat((float)(arg->integer));
}

static object *fn_floatp (object *args, object *env) {
  (void) env;
  return floatp(first(args)) ? tee : nil;
}

static object *fn_sin (object *args, object *env) {
  (void) env;
  return makefloat(sinf(checkintfloat(first(args))));
}

static object *fn_cos (object *args, object *env) {
  (void) env;
  return makefloat(cosf(checkintfloat(first(args))));
}

static object *fn_tan (object *args, object *env) {
  (void) env;
  return makefloat(tanf(checkintfloat(first(args))));
}

static object *fn_asin (object *args, object *env) {
  (void) env;
  return makefloat(asinf(checkintfloat(first(args))));
}

static object *fn_acos (object *args, object *env) {
  (void) env;
  return makefloat(acosf(checkintfloat(first(args))));
}

static object *fn_atan (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float div = 1.0;
  args = cdr(args);
  if (args != NULL) div = checkintfloat(first(args));
  return makefloat(atan2f(checkintfloat(arg), div));
}

static object *fn_sinh (object *args, object *env) {
  (void) env;
  return makefloat(sinhf(checkintfloat(first(args))));
}

static object *fn_cosh (object *args, object *env) {
  (void) env;
  return makefloat(coshf(checkintfloat(first(args))));
}

static object *fn_tanh (object *args, object *env) {
  (void) env;
  return makefloat(tanhf(checkintfloat(first(args))));
}

static object *fn_exp (object *args, object *env) {
  (void) env;
  return makefloat(expf(checkintfloat(first(args))));
}

static object *fn_sqrt (object *args, object *env) {
  (void) env;
  return makefloat(sqrtf(checkintfloat(first(args))));
}

static object *fn_log (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float fresult = logf(checkintfloat(arg));
  args = cdr(args);
  if (args == NULL) return makefloat(fresult);
  else return makefloat(fresult / logf(checkintfloat(first(args))));
}

static object *fn_expt (object *args, object *env) {
  (void) env;
  object *arg1 = first(args); object *arg2 = second(args);
  float float1 = checkintfloat(arg1);
  float value = logf(fabsf(float1)) * checkintfloat(arg2);
  if (integerp(arg1) && integerp(arg2) && ((arg2->integer) >= 0) && (fabsf(value) < 21.4875))
    return number(intpower(arg1->integer, arg2->integer));
  if (float1 < 0) {
    if (integerp(arg2)) return makefloat((arg2->integer & 1) ? -expf(value) : expf(value));
    else error2("invalid result");
  }
  return makefloat(expf(value));
}

static object *fn_ceiling (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg) && integerp(arg2)) {
      int num = arg->integer, den = arg2->integer, quo = num/den, rem = num-(quo*den);
      if (((num<0) != (den<0)) || rem==0) return number(quo); else return number(quo+1);
    } else return number(ceilf(checkintfloat(arg) / checkintfloat(first(args))));
  } else {
    if (integerp(arg)) return number(arg->integer);
    else return number(ceilf(checkintfloat(arg)));
  }
}

static object *fn_floor (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg) && integerp(arg2)) {
      int num = arg->integer, den = arg2->integer, quo = num/den, rem = num-(quo*den);
      if (((num<0) == (den<0)) || rem==0) return number(quo); else return number(quo-1);
    } else return number(floorf(checkintfloat(arg) / checkintfloat(first(args))));
  } else {
    if (integerp(arg)) return number(arg->integer);
    else return number(floorf(checkintfloat(arg)));
  }
}

static object *fn_truncate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg) && integerp(arg2)) return number(arg->integer / arg2->integer);
    else return number((int)(checkintfloat(arg) / checkintfloat(first(args))));
  } else {
    if (integerp(arg)) return number(arg->integer);
    else return number((int)(checkintfloat(arg)));
  }
}

static object *fn_round (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(roundf(checkintfloat(arg) / checkintfloat(first(args))));
  else return number(roundf(checkintfloat(arg)));
}

// Characters

static object *fn_char (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!stringp(arg)) error(notastring, arg);
  object *n = second(args);
  char c = nthchar(arg, checkinteger(n));
  if (c == 0) error(indexrange, n);
  return character(c);
}

static object *fn_charcode (object *args, object *env) {
  (void) env;
  return number(checkchar(first(args)));
}

static object *fn_codechar (object *args, object *env) {
  (void) env;
  return character(checkinteger(first(args)));
}

static object *fn_characterp (object *args, object *env) {
  (void) env;
  return characterp(first(args)) ? tee : nil;
}

// Strings

static object *fn_stringp (object *args, object *env) {
  (void) env;
  return stringp(first(args)) ? tee : nil;
}

static object *fn_stringeq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, false, true);
  return m == -1 ? nil : tee;
}

static object *fn_stringless (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, false, false);
  return m == -1 ? nil : number(m);
}

static object *fn_stringgreater (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, true, false);
  return m == -1 ? nil : number(m);
}

static object *fn_stringnoteq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, true, false);
  return m == -1 ? nil : number(m);
}

static object *fn_stringlesseq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, false, true);
  return m == -1 ? nil : number(m);
}

static object *fn_stringgreatereq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, true, true);
  return m == -1 ? nil : number(m);
}

static object *fn_sort (object *args, object *env) {
  object *arg = first(args);
  if (!listp(arg)) error(notalist, arg);
  if (arg == NULL) return nil;
  object *list = cons(nil, arg);
  protect(list);
  object *predicate = second(args);
  object *compare = cons(NULL, cons(NULL, NULL));
  protect(compare);
  object *ptr = cdr(list);
  while (cdr(ptr) != NULL) {
    object *go = list;
    while (go != ptr) {
      car(compare) = car(cdr(ptr));
      car(cdr(compare)) = car(cdr(go));
      if (apply(predicate, compare, env)) break;
      go = cdr(go);
    }
    if (go != ptr) {
      object *obj = cdr(ptr);
      cdr(ptr) = cdr(obj);
      cdr(obj) = cdr(go);
      cdr(go) = obj;
    } else ptr = cdr(ptr);
  }
  unprotect(); unprotect();
  return cdr(list);
}

static object *fn_stringfn (object *args, object *env) {
  return fn_princtostring(args, env);
}

static object *fn_concatenate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (builtin(arg->name) != STRINGFN) error2("only supports strings");
  args = cdr(args);
  object *result = newstring();
  object *tail = result;
  while (args != NULL) {
    object *obj = checkstring(first(args));
    obj = cdr(obj);
    while (obj != NULL) {
      uint32_t quad = obj->chars;
      while (quad != 0) {
         char ch = quad>>((sizeof(int)-1)*8) & 0xFF;
         buildstring(ch, &tail);
         quad = quad<<8;
      }
      obj = car(obj);
    }
    args = cdr(args);
  }
  return result;
}

static object *fn_subseq (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  int start = checkinteger(second(args)), end;
  if (start < 0) error(indexnegative, second(args));
  args = cddr(args);
  if (listp(arg)) {
    int length = listlength(arg);
    if (args != NULL) end = checkinteger(car(args)); else end = length;
    if (start > end || end > length) error2(indexrange);
    object *result = cons(NULL, NULL);
    object *ptr = result;
    for (int x = 0; x < end; x++) {
      if (x >= start) { cdr(ptr) = cons(car(arg), NULL); ptr = cdr(ptr); }
      arg = cdr(arg);
    }
    return cdr(result);
  } else if (stringp(arg)) {
    int length = stringlength(arg);
    if (args != NULL) end = checkinteger(car(args)); else end = length;
    if (start > end || end > length) error2(indexrange);
    object *result = newstring();
    object *tail = result;
    for (int i=start; i<end; i++) {
      char ch = nthchar(arg, i);
      buildstring(ch, &tail);
    }
    return result;
  } else error2("argument is not a list or string");
  return nil;
}

static object *fn_search (object *args, object *env) {
  (void) env;
  object *pattern = first(args);
  object *target = second(args);
  if (pattern == NULL) return number(0);
  else if (target == NULL) return nil;

  else if (listp(pattern) && listp(target)) {
    object *test = testargument(cddr(args));
    int l = listlength(target);
    int m = listlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      object *target1 = target;
      while (pattern != NULL && apply(test, cons(car(target1), cons(car(pattern), NULL)), env) != NULL) {
        pattern = cdr(pattern);
        target1 = cdr(target1);
      }
      if (pattern == NULL) return number(i);
      pattern = first(args); target = cdr(target);
    }
    return nil;

  } else if (stringp(pattern) && stringp(target)) {
    if (cddr(args) != NULL) error2("keyword argument not supported for strings");
    int l = stringlength(target);
    int m = stringlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      int j = 0;
      while (j < m && nthchar(target, i+j) == nthchar(pattern, j)) j++;
      if (j == m) return number(i);
    }
    return nil;
  } else error2("arguments are not both lists or strings");
  return nil;
}

static object *fn_readfromstring (object *args, object *env) {
  (void) env;
  object *arg = checkstring(first(args));
  if (stringlength(arg) == 0) error2("zero length string");
  GlobalString = arg;
  GlobalStringIndex = 0;
  object *val = readmain(gstr);
  return val;
}

static object *fn_princtostring (object *args, object *env) {
  (void) env;
  return princtostring(first(args));
}

static object *fn_prin1tostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring();
  printobject(arg, pstr);
  return obj;
}

// Bitwise operators

static object *fn_logand (object *args, object *env) {
  (void) env;
  int result = -1;
  while (args != NULL) {
    result = result & checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

static object *fn_logior (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result | checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

static object *fn_logxor (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result ^ checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

static object *fn_lognot (object *args, object *env) {
  (void) env;
  int result = checkinteger(car(args));
  return number(~result);
}

static object *fn_ash (object *args, object *env) {
  (void) env;
  int value = checkinteger(first(args));
  int count = checkinteger(second(args));
  if (count >= 0) return number(value << count);
  else return number(value >> abs(count));
}

static object *fn_logbitp (object *args, object *env) {
  (void) env;
  int index = checkinteger(first(args));
  int value = checkinteger(second(args));
  return (((value >> index) & 1) == 1) ? tee : nil;
}

// System functions

static object *fn_eval (object *args, object *env) {
  return eval(first(args), env);
}

static object *fn_return (object *args, object *env) {
  (void) env;
  setflag(RETURNFLAG);
  if (args == NULL) return nil; else return first(args);
}

static object *fn_globals (object *args, object *env) {
  (void) args, (void) env;
  object *result = cons(NULL, NULL);
  object *ptr = result;
  object *arg = GlobalEnv;
  while (arg != NULL) {
    cdr(ptr) = cons(car(car(arg)), NULL); ptr = cdr(ptr);
    arg = cdr(arg);
  }
  return cdr(result);
}

static object *fn_locals (object *args, object *env) {
  (void) args;
  return env;
}

static object *fn_makunbound (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  delassoc(var, &GlobalEnv);
  return var;
}

static object *fn_break (object *args, object *env) {
  (void) args;
  pfstring("\nBreak!\n", pserial);
  BreakLevel++;
  repl(env);
  BreakLevel--;
  return nil;
}

static object *fn_read (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return readmain(gfun);
}

static object *fn_prin1 (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  printobject(obj, pfun);
  return obj;
}

static object *fn_print (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  pln(pfun);
  printobject(obj, pfun);
  pfun(' ');
  return obj;
}

static object *fn_princ (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  prin1object(obj, pfun);
  return obj;
}

static object *fn_terpri (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  pln(pfun);
  return nil;
}

static object *fn_readbyte (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  if (gfun == gserial) gserial_flush();
  int c = gfun();
  return (c == -1) ? nil : number(c);
}

static object *fn_readline (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  if (gfun == gserial) gserial_flush();
  return readstring('\n', false, gfun);
}

static object *fn_writebyte (object *args, object *env) {
  (void) env;
  int c = checkinteger(first(args));
  pfun_t pfun = pstreamfun(cdr(args));
  (pfun)(c);
  return nil;
}

static object *fn_writestring (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printstring(obj, pfun);
  Flags = temp;
  return nil;
}

static object *fn_writeline (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printstring(obj, pfun);
  pln(pfun);
  Flags = temp;
  return nil;
}

static object *fn_gc (object *args, object *env) {
  if (args == NULL || first(args) != NULL) {
    int initial = Freespace;
    unsigned long start = sys_micros();
    gc(args, env);
    unsigned long elapsed = sys_micros() - start;
    pfstring("Space: ", pserial);
    pint(Freespace - initial, pserial);
    pfstring(" bytes, Time: ", pserial);
    pint(elapsed, pserial);
    pfstring(" us\n", pserial);
  } else gc(args, env);
  return nil;
}

static object *fn_room (object *args, object *env) {
  (void) args, (void) env;
  return number(Freespace);
}

static object *fn_backtrace (object *args, object *env) {
  (void) env;
  if (args == NULL) return (tstflag(BACKTRACE)) ? tee : nil;
  if (first(args) == NULL) clrflag(BACKTRACE); else setflag(BACKTRACE);
  return first(args);
}

static object *fn_saveimage (object *args, object *env) {
  if (args != NULL) args = eval(first(args), env);
  return number(saveimage(args));
}

static object *fn_loadimage (object *args, object *env) {
  (void) env;
  if (args != NULL) args = first(args);
  return number(loadimage(args));
}

static object *fn_cls (object *args, object *env) {
  (void) args, (void) env;
  pserial(12);
  return nil;
}

// Pin and timing procedures

static object *fn_pinmode (object *args, object *env) {
  (void) env; int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(first(args));
  int pm = SYS_INPUT;
  arg = second(args);
  if (keywordp(arg)) pm = checkkeyword(arg);
  else if (integerp(arg)) {
    int mode = arg->integer;
    if (mode == 1) pm = SYS_OUTPUT; else if (mode == 2) pm = SYS_INPUT_PULLUP;
    else if (mode == 4) pm = SYS_INPUT_PULLDOWN;
  } else if (arg != nil) pm = SYS_OUTPUT;
  sys_pinmode(pin, pm);
  return nil;
}

static object *fn_digitalread (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(arg);
  if (sys_digitalread(pin) != 0) return tee; else return nil;
}

static object *fn_digitalwrite (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(arg);
  arg = second(args);
  int mode;
  if (keywordp(arg)) mode = checkkeyword(arg);
  else if (integerp(arg)) mode = arg->integer ? 1 : 0;
  else mode = (arg != nil) ? 1 : 0;
  sys_digitalwrite(pin, mode);
  return arg;
}

static object *fn_analogread (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else {
    pin = checkinteger(arg);
    checkanalogread(pin);
  }
  return number(sys_analogread(pin));
}

static object *fn_delay (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  unsigned long start = sys_millis();
  unsigned long total = checkinteger(arg1);
  do testescape();
  while (sys_millis() - start < total);
  return arg1;
}

static object *fn_millis (object *args, object *env) {
  (void) args, (void) env;
  return number(sys_millis());
}

static object *fn_sleep (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  doze(checkinteger(arg1));
  return arg1;
}

static object *fn_note (object *args, object *env) {
  (void) env;
  static int pin = 255;
  if (args != NULL) {
    pin = checkinteger(first(args));
    int note = 48, octave = 0;
    if (cdr(args) != NULL) {
      note = checkinteger(second(args));
      if (cddr(args) != NULL) octave = checkinteger(third(args));
    }
    playnote(pin, note, octave);
  } else nonote(pin);
  return nil;
}

static object *fn_register (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  int addr;
  if (keywordp(arg)) addr = checkkeyword(arg);
  else addr = checkinteger(first(args));
  if (cdr(args) == NULL) return number(*(uint32_t *)(uintptr_t)(uint32_t)addr);
  (*(uint32_t *)(uintptr_t)(uint32_t)addr) = checkinteger(second(args));
  return second(args);
}

// Tree Editor

static object *fn_edit (object *args, object *env) {
  object *fun = first(args);
  object *pair = findvalue(fun, env);
  clrflag(EXITEDITOR);
  object *arg = edit(eval(fun, env));
  cdr(pair) = arg;
  return arg;
}

// Pretty printer

static object *fn_pprint (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
  pln(pfun);
  superprint(obj, 0, pfun);
  ppwidth = PPWIDTH;
  return bsymbol(NOTHING);
}

static object *fn_pprintall (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    object *val = cdr(pair);
    pln(pfun);
    if (consp(val) && symbolp(car(val)) && builtin(car(val)->name) == LAMBDA) {
      superprint(cons(bsymbol(DEFUN), cons(var, cdr(val))), 0, pfun);
    } else if (consp(val) && car(val)->type == CODE) {
      superprint(cons(bsymbol(DEFCODE), cons(var, cdr(val))), 0, pfun);
    } else {
      superprint(cons(bsymbol(DEFVAR), cons(var, cons(quote(val), NULL))), 0, pfun);
    }
    pln(pfun);
    testescape();
    globals = cdr(globals);
  }
  ppwidth = PPWIDTH;
  return bsymbol(NOTHING);
}

// Format

static object *fn_format (object *args, object *env) {
  (void) env;
  pfun_t pfun = pserial;
  object *output = first(args);
  object *obj;
  if (output == nil) { obj = startstring(); pfun = pstr; }
  else if (!eq(output, tee)) pfun = pstreamfun(args);
  object *formatstr = checkstring(second(args));
  object *save = NULL;
  args = cddr(args);
  uint16_t len = stringlength(formatstr);
  uint16_t n = 0, width = 0, w, bra = 0;
  char pad = ' ';
  bool tilde = false, mute = false, comma = false, quote = false;
  while (n < len) {
    char ch = nthchar(formatstr, n);
    char ch2 = ch & ~0x20; // force to upper case
    if (tilde) {
     if (ch == '}') {
        if (save == NULL) formaterr(formatstr, "no matching ~{", n);
        if (args == NULL) { args = cdr(save); save = NULL; } else n = bra;
        mute = false; tilde = false;
      }
      else if (!mute) {
        if (comma && quote) { pad = ch; comma = false, quote = false; }
        else if (ch == '\'') {
          if (comma) quote = true;
          else formaterr(formatstr, "quote not valid", n);
        }
        else if (ch == '~') { pfun('~'); tilde = false; }
        else if (ch >= '0' && ch <= '9') width = width*10 + ch - '0';
        else if (ch == ',') comma = true;
        else if (ch == '%') { pln(pfun); tilde = false; }
        else if (ch == '&') { pfl(pfun); tilde = false; }
        else if (ch == '^') {
          if (save != NULL && args == NULL) mute = true;
          tilde = false;
        }
        else if (ch == '{') {
          if (save != NULL) formaterr(formatstr, "can't nest ~{", n);
          if (args == NULL) formaterr(formatstr, noargument, n);
          if (!listp(first(args))) formaterr(formatstr, notalist, n);
          save = args; args = first(args); bra = n; tilde = false;
          if (args == NULL) mute = true;
        }
        else if (ch2 == 'A' || ch2 == 'S' || ch2 == 'D' || ch2 == 'G' || ch2 == 'X' || ch2 == 'B') {
          if (args == NULL) formaterr(formatstr, noargument, n);
          object *arg = first(args); args = cdr(args);
          uint8_t aw = atomwidth(arg);
          if (width < aw) w = 0; else w = width-aw;
          tilde = false;
          if (ch2 == 'A') { prin1object(arg, pfun); indent(w, pad, pfun); }
          else if (ch2 == 'S') { printobject(arg, pfun); indent(w, pad, pfun); }
          else if (ch2 == 'D' || ch2 == 'G') { indent(w, pad, pfun); prin1object(arg, pfun); }
          else if (ch2 == 'X' || ch2 == 'B') {
            if (integerp(arg)) {
              uint8_t base = (ch2 == 'B') ? 2 : 16;
              uint8_t hw = basewidth(arg, base); if (width < hw) w = 0; else w = width-hw;
              indent(w, pad, pfun); pintbase(arg->integer, base, pfun);
            } else {
              indent(w, pad, pfun); prin1object(arg, pfun);
            }
          }
          tilde = false;
        } else formaterr(formatstr, "invalid directive", n);
      }
    } else {
      if (ch == '~') { tilde = true; pad = ' '; width = 0; comma = false; quote = false; }
      else if (!mute) pfun(ch);
    }
    n++;
  }
  if (output == nil) return obj;
  else return nil;
}

// LispLibrary

static object *fn_require (object *args, object *env) {
  object *arg = first(args);
  object *globals = GlobalEnv;
  if (!symbolp(arg)) error(notasymbol, arg);
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    if (symbolp(var) && var == arg) return nil;
    globals = cdr(globals);
  }
  if (!sys_fopen(SYS_FTMP, sys_library(), SYS_READ)) return nil;
  object *line = readmain(glibrary);
  while (line != NULL) {
    // Is this the definition we want
    symbol_t fname = first(line)->name;
    if ((fname == sym(DEFUN) || fname == sym(DEFVAR)) && symbolp(second(line)) && second(line)->name == arg->name) {
      sys_fclose(SYS_FTMP);
      eval(line, env);
      return tee;
    }
    line = readmain(glibrary);
  }
  sys_fclose(SYS_FTMP);
  return nil;
}

static object *fn_listlibrary (object *args, object *env) {
  (void) args, (void) env;
  if (!sys_fopen(SYS_FTMP, sys_library(), SYS_READ)) return bsymbol(NOTHING);
  object *line = readmain(glibrary);
  while (line != NULL) {
    builtin_t bname = builtin(first(line)->name);
    if (bname == DEFUN || bname == DEFVAR) {
      printsymbol(second(line), pserial); pserial(' ');
    }
    line = readmain(glibrary);
  }
  sys_fclose(SYS_FTMP);
  return bsymbol(NOTHING);
}

// Documentation

static object *sp_help (object *args, object *env) {
  if (args == NULL) error2(noargument);
  object *docstring = documentation(first(args), env);
  if (docstring) {
    flags_t temp = Flags;
    clrflag(PRINTREADABLY);
    printstring(docstring, pserial);
    Flags = temp;
  }
  return bsymbol(NOTHING);
}

static object *fn_documentation (object *args, object *env) {
  return documentation(first(args), env);
}

static object *fn_apropos (object *args, object *env) {
  (void) env;
  apropos(first(args), true);
  return bsymbol(NOTHING);
}

static object *fn_aproposlist (object *args, object *env) {
  (void) env;
  return apropos(first(args), false);
}

// Error handling

static object *sp_unwindprotect (object *args, object *env) {
  if (args == NULL) error2(toofewargs);
  object *current_GCStack = GCStack;
  jmp_buf dynamic_handler;
  jmp_buf *previous_handler = handler;
  handler = &dynamic_handler;
  object *protected_form = first(args);
  object *volatile result = nil;

  volatile bool signaled = false;
  if (!setjmp(dynamic_handler)) {
    result = eval(protected_form, env);
  } else {
    GCStack = current_GCStack;
    signaled = true;
  }
  handler = previous_handler;

  object *protective_forms = cdr(args);
  while (protective_forms != NULL) {
    eval(car(protective_forms), env);
    if (tstflag(RETURNFLAG)) break;
    protective_forms = cdr(protective_forms);
  }

  if (!signaled) return result;
  GCStack = NULL;
  longjmp(*handler, 1);
}

static object *sp_ignoreerrors (object *args, object *env) {
  object *current_GCStack = GCStack;
  jmp_buf dynamic_handler;
  jmp_buf *previous_handler = handler;
  handler = &dynamic_handler;
  object *volatile result = nil;
  object *volatile forms = args;

  bool muffled = tstflag(MUFFLEERRORS);
  setflag(MUFFLEERRORS);
  volatile bool signaled = false;
  if (!setjmp(dynamic_handler)) {
    while (forms != NULL) {
      result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) break;
      forms = cdr(forms);
    }
  } else {
    GCStack = current_GCStack;
    signaled = true;
  }
  handler = previous_handler;
  if (!muffled) clrflag(MUFFLEERRORS);

  if (signaled) return bsymbol(NOTHING);
  else return result;
}

static object *sp_error (object *args, object *env) {
  object *message = eval(cons(bsymbol(FORMAT), cons(nil, args)), env);
  if (!tstflag(MUFFLEERRORS)) {
    flags_t temp = Flags;
    clrflag(PRINTREADABLY);
    pfstring("Error: ", pserial); printstring(message, pserial);
    Flags = temp;
    pln(pserial);
  }
  GCStack = NULL;
  longjmp(*handler, 1);
}

// SD Card utilities

static object *fn_directory (object *args, object *env) {
  (void) args, (void) env;
  if (!sys_diropen(".")) error2("problem reading directory");
  object *result = cons(NULL, NULL);
  object *ptr = result;
  const char *name;
  while ((name = sys_dirnext()) != NULL) {
    object *filename = lispstring((char*)name);
    cdr(ptr) = cons(filename, NULL);
    ptr = cdr(ptr);
  }
  return cdr(result);
}

// Graphics functions

static object *sp_withgfx (object *args, object *env) {
  object *params = checkarguments(args, 1, 1);
  object *var = first(params);
  object *pair = cons(var, stream(GFXSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  return result;
}

static object *fn_drawpixel (object *args, object *env) {
  (void) env;
  uint16_t colour = COLOR_WHITE;
  if (cddr(args) != NULL) colour = checkinteger(third(args));
  gfx_pixel(checkinteger(first(args)), checkinteger(second(args)), colour);
  return nil;
}

static object *fn_drawline (object *args, object *env) {
  (void) env;
  int params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_line(params[0], params[1], params[2], params[3], colour);
  return nil;
}

static object *fn_drawrect (object *args, object *env) {
  (void) env;
  int params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_rect(params[0], params[1], params[2], params[3], colour);
  return nil;
}

static object *fn_fillrect (object *args, object *env) {
  (void) env;
  int params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_fillrect(params[0], params[1], params[2], params[3], colour);
  return nil;
}

static object *fn_drawcircle (object *args, object *env) {
  (void) env;
  int params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_circle(params[0], params[1], params[2], colour);
  return nil;
}

static object *fn_fillcircle (object *args, object *env) {
  (void) env;
  int params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_fillcircle(params[0], params[1], params[2], colour);
  return nil;
}

static object *fn_drawroundrect (object *args, object *env) {
  (void) env;
  int params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_roundrect(params[0], params[1], params[2], params[3], params[4], colour);
  return nil;
}

static object *fn_fillroundrect (object *args, object *env) {
  (void) env;
  int params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_fillroundrect(params[0], params[1], params[2], params[3], params[4], colour);
  return nil;
}

static object *fn_drawtriangle (object *args, object *env) {
  (void) env;
  int params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_triangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  return nil;
}

static object *fn_filltriangle (object *args, object *env) {
  (void) env;
  int params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  gfx_filltriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  return nil;
}

static object *fn_drawchar (object *args, object *env) {
  (void) env;
  uint16_t colour = COLOR_WHITE, bg = COLOR_BLACK, size = 1;
  object *more = cdr(cddr(args));
  if (more != NULL) {
    colour = checkinteger(car(more));
    more = cdr(more);
    if (more != NULL) {
      bg = checkinteger(car(more));
      more = cdr(more);
      if (more != NULL) size = checkinteger(car(more));
    }
  }
  gfx_char(checkinteger(first(args)), checkinteger(second(args)), checkchar(third(args)),
    colour, bg, size);
  return nil;
}

static object *fn_setcursor (object *args, object *env) {
  (void) env;
  gfx_setcursor(checkinteger(first(args)), checkinteger(second(args)));
  return nil;
}

static object *fn_settextcolor (object *args, object *env) {
  (void) env;
  if (cdr(args) != NULL) gfx_settextcolor(checkinteger(first(args)), checkinteger(second(args)));
  else gfx_settextcolor(checkinteger(first(args)), GFX_TRANSPARENT);
  return nil;
}

static object *fn_settextsize (object *args, object *env) {
  (void) env;
  gfx_settextsize(checkinteger(first(args)));
  return nil;
}

static object *fn_settextwrap (object *args, object *env) {
  (void) env;
  gfx_settextwrap(first(args) != NULL);
  return nil;
}

static object *fn_fillscreen (object *args, object *env) {
  (void) env;
  uint16_t colour = COLOR_BLACK;
  if (args != NULL) colour = checkinteger(first(args));
  gfx_fillscreen(colour);
  return nil;
}

// PicoCalc extras

static object *fn_getkey (object *args, object *env) {
  (void) env, (void) args;
  return character(sys_getkey());
}

static object *fn_readpixel (object *args, object *env) {
  (void) env;
  return number(gfx_readpixel(checkinteger(first(args)), checkinteger(second(args))));
  return nil;
}

// Write two bytes, least significant byte first
static void writeTwo (uint16_t word) {
  sys_fputc(SYS_FTMP, word & 0xFF); sys_fputc(SYS_FTMP, (word >> 8) & 0xFF);
}

// Write four bytes, least significant byte first
static void writeFour (uint32_t word) {
  writeTwo(word & 0xFFFF); writeTwo(word >> 16);
}

static void savebmp (object *arg) {
  uint16_t width = GFX_WIDTH, height = GFX_HEIGHT;
  char buffer[PATHSIZE];
  if (!sys_fopen(SYS_FTMP, MakeFilename(checkstring(arg), buffer), SYS_WRITE))
    error2("problem writing to file or invalid filename");
  //
  // File header: 14 bytes
  sys_fputc(SYS_FTMP, 'B'); sys_fputc(SYS_FTMP, 'M');
  writeFour(14+40+width*height*2);            // File size in bytes
  writeFour(0);
  writeFour(14+40);                           // Offset to image data from start
  //
  // Image header: 40 bytes
  writeFour(40);                              // Header size
  writeFour(width);                           // Image width
  writeFour(height);                          // Image height
  writeTwo(1);                                // Planes
  writeTwo(16);                               // Bits per pixel
  for (int i=0; i<6; i++) writeFour(0);       // No compression; sizes, resolution and colours ignored
  //
  // Image data: width * height * 2 bytes
  for (int y=height-1; y>=0; y--) {
    for (int x=0; x<width; x++) {
      uint16_t rgb = gfx_readpixel(x,y);
      writeTwo((rgb & 0xFFC0)>>1 | (rgb & 0x1F)); // Convert to 555 format
    }
  }
  if (!sys_fclose(SYS_FTMP)) error2("not enough room");
}

static object *fn_savebmp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  savebmp(arg);
  return arg;
}

// Built-in symbol lookup table

static const tbl_entry_t lookup_table[] = {
#define N(e, name, fn, minmax, doc) [e] = { name, fn, minmax },
#define B(name, fn, minmax, doc) { name, fn, minmax },
#define K(name, value, context) { name, (fn_ptr_type)(uintptr_t)(value), context },
#include "builtins.def"
#undef N
#undef B
#undef K
};

// An N line out of place leaves a hole in the table or overwrites an entry
_Static_assert(arraysize(lookup_table) == 0
#define N(e, name, fn, minmax, doc) + 1
#define B(name, fn, minmax, doc) + 1
#define K(name, value, context) + 1
#include "builtins.def"
#undef N
#undef B
#undef K
  , "builtins.def: the N lines must come first and unbroken");

// Table cross-reference functions

static tbl_entry_t *tables[] = {lookup_table, NULL};
static const unsigned int tablesizes[] = { arraysize(lookup_table), 0 };

static const tbl_entry_t *table (int n) {
  return tables[n];
}

static unsigned int tablesize (int n) {
  return tablesizes[n];
}

// Table lookup functions

static builtin_t lookupbuiltin (char* c) {
  unsigned int start = tablesize(0);
  for (int n=1; n>=0; n--) {
    int entries = tablesize(n);
    for (int i=0; i<entries; i++) {
      if (strcasecmp(c, (char*)(table(n)[i].string)) == 0)
        return (builtin_t)(start + i);
    }
    start = 0;
  }
  return ENDFUNCTIONS;
}

static intptr_t lookupfn (builtin_t name) {
  bool n = name<tablesize(0);
  return (intptr_t)table(n?0:1)[n?name:name-tablesize(0)].fptr;
}

static uint8_t getminmax (builtin_t name) {
  bool n = name<tablesize(0);
  return table(n?0:1)[n?name:name-tablesize(0)].minmax;
}

static void checkminmax (builtin_t name, int nargs) {
  if (!(name < ENDFUNCTIONS)) error2("not a builtin");
  uint8_t minmax = getminmax(name);
  if (nargs<((minmax >> 3) & 0x07)) error2(toofewargs);
  if ((minmax & 0x07) != 0x07 && nargs>(minmax & 0x07)) error2(toomanyargs);
}

static bool findsubstring (char *part, builtin_t name) {
  bool n = name<tablesize(0);
  return (strstr(table(n?0:1)[n?name:name-tablesize(0)].string, part) != NULL);
}

static void testescape (void) {
  if (sys_break()) error2("escape!");
}

static bool colonp (symbol_t name) {
  if (!longnamep(name)) return false;
  object *form = (object *)name;
  if (form == NULL) return false;
  return (((form->chars)>>((sizeof(int)-1)*8) & 0xFF) == ':');
}

static bool keywordp (object *obj) {
  if (!(symbolp(obj) && builtinp(obj->name))) return false;
  builtin_t name = builtin(obj->name);
  bool n = name<tablesize(0);
  const char *s = table(n?0:1)[n?name:name-tablesize(0)].string;
  char c = s[0];
  return (c == ':');
}

static void backtrace (symbol_t name) {
  Backtrace[TraceTop] = (name == sym(NIL)) ? sym(LAMBDA) : name;
  TraceTop = modbacktrace(TraceTop+1);
  if (TraceStart == TraceTop) TraceStart = modbacktrace(TraceStart+1);
}

// Main evaluator

static object *eval (object *form, object *env) {
  int TC=0;
  EVAL:
  // Enough space?
  checkstack();
  if (Freespace <= WORKSPACESIZE>>4) gc(form, env);      // GC when 1/16 of workspace left
  // Escape
  if (tstflag(ESCAPE)) { clrflag(ESCAPE); error2("escape!");}
  if (!tstflag(NOESC)) testescape();

  if (form == NULL) return nil;

  if (form->type >= NUMBER && form->type <= STRING) return form;

  if (symbolp(form)) {
    symbol_t name = form->name;
    if (colonp(name)) return form; // Keyword
    object *pair = value(name, env);
    if (pair != NULL) return cdr(pair);
    pair = value(name, GlobalEnv);
    if (pair != NULL) return cdr(pair);
    else if (builtinp(name)) {
      if (name == sym(FEATURES)) return features();
      return form;
    }
    Context = NIL;
    error("undefined", form);
  }

  if (form->type == CODE) error2("can't evaluate CODE header");

  // It's a list
  object *function = car(form);
  object *args = cdr(form);

  if (function == NULL) error(illegalfn, function);
  if (!listp(args)) error("can't evaluate a dotted pair", args);

  // List starts with a builtin symbol?
  if (symbolp(function) && builtinp(function->name)) {
    builtin_t name = builtin(function->name);

    if ((name == LET) || (name == LETSTAR)) {
      if (args == NULL) error2(noargument);
      object *assigns = first(args);
      if (!listp(assigns)) error(notalist, assigns);
      object *forms = cdr(args);
      object *newenv = env;
      protect(newenv);
      while (assigns != NULL) {
        object *assign = car(assigns);
        if (!consp(assign)) push(cons(assign,nil), newenv);
        else if (cdr(assign) == NULL) push(cons(first(assign),nil), newenv);
        else push(cons(first(assign), eval(second(assign),env)), newenv);
        car(GCStack) = newenv;
        if (name == LETSTAR) env = newenv;
        assigns = cdr(assigns);
      }
      env = newenv;
      unprotect();
      form = tf_progn(forms,env);
      goto EVAL;
    }

    if (name == LAMBDA) {
      if (env == NULL) return form;
      object *envcopy = NULL;
      while (env != NULL) {
        object *pair = first(env);
        if (pair != NULL) push(pair, envcopy);
        env = cdr(env);
      }
      return cons(bsymbol(CLOSURE), cons(envcopy,args));
    }

    switch(fntype(name)) {
      case SPECIAL_FORMS:
        Context = name;
        checkargs(args);
        return ((fn_ptr_type)lookupfn(name))(args, env);

      case TAIL_FORMS:
        Context = name;
        checkargs(args);
        form = ((fn_ptr_type)lookupfn(name))(args, env);
        TC = 1;
        goto EVAL;
    }
  }

  // Evaluate the parameters - result in head
  int TCstart = TC;
  object *head;
  if (consp(function) && !(isbuiltin(car(function), LAMBDA) || isbuiltin(car(function), CLOSURE)
    || car(function)->type == CODE)) { Context = NIL; error(illegalfn, function); }
  if (symbolp(function)) {
    object *pair = findpair(function, env);
    if (pair != NULL) head = cons(cdr(pair), NULL); else head = cons(function, NULL);
  } else head = cons(eval(function, env), NULL);
  protect(head); // Don't GC the result list
  object *tail = head;
  form = cdr(form);
  int nargs = 0;

  while (form != NULL){
    object *obj = cons(eval(car(form),env),NULL);
    cdr(tail) = obj;
    tail = obj;
    form = cdr(form);
    nargs++;
  }

  object *fname = function;
  function = car(head);
  args = cdr(head);

  if (symbolp(function)) {
    if (!builtinp(function->name)) { Context = NIL; error(illegalfn, function); }
    builtin_t bname = builtin(function->name);
    Context = bname;
    checkminmax(bname, nargs);
    intptr_t call = lookupfn(bname);
    if (call == 0) error(illegalfn, function);
    object *result = ((fn_ptr_type)call)(args, env);
    unprotect();
    return result;
  }

  if (consp(function)) {
    symbol_t name = sym(NIL);
    if (!listp(fname)) name = fname->name;

    if (isbuiltin(car(function), LAMBDA)) {
      if (tstflag(BACKTRACE)) backtrace(name);
      form = closure(TCstart, name, function, args, &env);
      unprotect();
      int trace = tracing(name);
      if (trace || tstflag(BACKTRACE)) {
        object *result = eval(form, env);
        if (trace) {
          indent((--(TraceDepth[trace-1]))<<1, ' ', pserial);
          pint(TraceDepth[trace-1], pserial);
          pserial(':'); pserial(' ');
          printobject(fname, pserial); pfstring(" returned ", pserial);
          printobject(result, pserial); pln(pserial);
        }
        if (tstflag(BACKTRACE)) TraceTop = modbacktrace(TraceTop-1);
        return result;
      } else {
        TC = 1;
        goto EVAL;
      }
    }

    if (isbuiltin(car(function), CLOSURE)) {
      function = cdr(function);
      if (tstflag(BACKTRACE)) backtrace(name);
      form = closure(TCstart, name, function, args, &env);
      unprotect();
      if (tstflag(BACKTRACE)) {
        object *result = eval(form, env);
        TraceTop = modbacktrace(TraceTop-1);
        return result;
      } else {
        TC = 1;
        goto EVAL;
      }
    }

    if (car(function)->type == CODE) {
      int n = listlength(second(function));
      if (nargs<n) errorsym2(fname->name, toofewargs);
      if (nargs>n) errorsym2(fname->name, toomanyargs);
      uint32_t entry = startblock(car(function)) + 1;
      unprotect();
      return call(entry, n, args, env);
    }

  }
  error(illegalfn, fname); return nil;
}

// Print functions

static void pserial (char c) {
  LastPrint = c;
  sys_putc(c);
}

static const char ControlCodes[] = "Null\0SOH\0STX\0ETX\0EOT\0ENQ\0ACK\0Bell\0Backspace\0Tab\0Newline\0VT\0"
"Page\0Return\0SO\0SI\0DLE\0DC1\0DC2\0DC3\0DC4\0NAK\0SYN\0ETB\0CAN\0EM\0SUB\0Escape\0FS\0GS\0RS\0US\0Space\0";

static void pcharacter (uint8_t c, pfun_t pfun) {
  if (!tstflag(PRINTREADABLY)) pfun(c);
  else {
    pfun('#'); pfun('\\');
    if (c <= 32) {
      const char *p = ControlCodes;
      while (c > 0) {p = p + strlen(p) + 1; c--; }
      pfstring(p, pfun);
    } else if (c < 127) pfun(c);
    else pint(c, pfun);
  }
}

static void plispstr (symbol_t name, pfun_t pfun) {
  object *form = (object *)name;
  while (form != NULL) {
    int chars = form->chars;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (tstflag(PRINTREADABLY) && (ch == '"' || ch == '\\')) pfun('\\');
      if (ch) pfun(ch);
    }
    form = car(form);
  }
}

static void printstring (object *form, pfun_t pfun) {
  if (tstflag(PRINTREADABLY)) pfun('"');
  plispstr(form->name, pfun);
  if (tstflag(PRINTREADABLY)) pfun('"');
}

static void pbuiltin (builtin_t name, pfun_t pfun) {
  int n = name<tablesize(0);
  const char *s = table(n?0:1)[n?name:name-tablesize(0)].string;
  while (1) {
    char c = *s++;
    if (c == 0) return;
    pfun(c);
  }
}

static void pradix40 (symbol_t name, pfun_t pfun) {
  uint32_t x = untwist(name);
  for (int d=102400000; d>0; d = d/40) {
    uint32_t j = x/d;
    char c = fromradix40(j);
    if (c == 0) return;
    pfun(c); x = x - j*d;
  }
}

static void printsymbol (object *form, pfun_t pfun) {
  psymbol(form->name, pfun);
}

static void psymbol (symbol_t name, pfun_t pfun) {
  if (longnamep(name)) plispstr(name, pfun);
  else {
    uint32_t value = untwist(name);
    if (value < PACKEDS) error2("invalid symbol");
    else if (value >= BUILTINS) pbuiltin((builtin_t)(value-BUILTINS), pfun);
    else pradix40(name, pfun);
  }
}

static void pfstring (const char *s, pfun_t pfun) {
  while (1) {
    char c = *s++;
    if (c == 0) return;
    pfun(c);
  }
}

static void pint (int i, pfun_t pfun) {
  uint32_t j = i;
  if (i<0) { pfun('-'); j=0u-j; }
  pintbase(j, 10, pfun);
}

static void pintbase (uint32_t i, uint8_t base, pfun_t pfun) {
  int lead = 0; uint32_t p = 1000000000;
  if (base == 2) p = 0x80000000; else if (base == 16) p = 0x10000000;
  for (uint32_t d=p; d>0; d=d/base) {
    uint32_t j = i/d;
    if (j!=0 || lead || d==1) { pfun((j<10) ? j+'0' : j+'W'); lead=1;}
    i = i - j*d;
  }
}

static void printhex4 (int i, pfun_t pfun) {
  int p = 0x1000;
  for (int d=p; d>0; d=d/16) {
    int j = i/d;
    pfun((j<10) ? j+'0' : j + 'W');
    i = i - j*d;
  }
  pfun(' ');
}

// Ten to the power n. The product is exact for n up to 22 either way, where
// the library's pow() need not be: pmantissa() truncates the result.
static double tento (int n) {
  double result = 1.0, power = 10.0;
  bool inverse = n < 0;
  if (inverse) n = -n;
  for (; n != 0; n = n>>1) {
    if (n & 1) result = result * power;
    power = power * power;
  }
  return inverse ? 1.0/result : result;
}

static void pmantissa (float f, pfun_t pfun) {
  int sig = floorf(log10f(f));
  int mul = tento(5 - sig);
  int i = roundf(f * mul);
  bool point = false;
  if (i == 1000000) { i = 100000; sig++; }
  if (sig < 0) {
    pfun('0'); pfun('.'); point = true;
    for (int j=0; j < - sig - 1; j++) pfun('0');
  }
  mul = 100000;
  for (int j=0; j<7; j++) {
    int d = (int)(i / mul);
    pfun(d + '0');
    i = i - d * mul;
    if (i == 0) {
      if (!point) {
        for (int k=j; k<sig; k++) pfun('0');
        pfun('.'); pfun('0');
      }
      return;
    }
    if (j == sig && sig >= 0) { pfun('.'); point = true; }
    mul = mul / 10;
  }
}

static void pfloat (float f, pfun_t pfun) {
  if (isnan(f)) { pfstring("NaN", pfun); return; }
  if (f == 0.0) { pfun('0'); return; }
  if (isinf(f)) { pfstring("Inf", pfun); return; }
  if (f < 0) { pfun('-'); f = -f; }
  // Calculate exponent
  int e = 0;
  if (f < 1e-3 || f >= 1e5) {
    e = floorf(logf(f) / 2.302585); // log10 gives wrong result
    f = f / tento(e);
  }

  pmantissa (f, pfun);

  // Exponent
  if (e != 0) {
    pfun('e');
    pint(e, pfun);
  }
}

static void pln (pfun_t pfun) {
  pfun('\n');
}

static void pfl (pfun_t pfun) {
  if (LastPrint != '\n') pfun('\n');
}

static void plist (object *form, pfun_t pfun) {
  pfun('(');
  printobject(car(form), pfun);
  form = cdr(form);
  while (form != NULL && listp(form)) {
    pfun(' ');
    printobject(car(form), pfun);
    testescape();
    form = cdr(form);
  }
  if (form != NULL) {
    pfstring(" . ", pfun);
    printobject(form, pfun);
  }
  pfun(')');
}

static void pstream (object *form, pfun_t pfun) {
  pfun('<');
  nstream_t nstream = (form->integer)>>8;
  bool n = nstream<USERSTREAMS;
  const char *streamname = streamtable(n?0:1)[n?nstream:nstream-USERSTREAMS].streamname;
  pfstring(streamname, pfun);
  pfstring("-stream ", pfun);
  pint(form->integer & 0xFF, pfun);
  pfun('>');
}

static void printobject (object *form, pfun_t pfun) {
  checkstack();
  if (form == NULL) pfstring("nil", pfun);
  else if (listp(form) && isbuiltin(car(form), CLOSURE)) pfstring("<closure>", pfun);
  else if (listp(form)) plist(form, pfun);
  else if (integerp(form)) pint(form->integer, pfun);
  else if (floatp(form)) pfloat(form->single_float, pfun);
  else if (symbolp(form)) { if (form->name != sym(NOTHING)) printsymbol(form, pfun); }
  else if (characterp(form)) pcharacter(form->chars, pfun);
  else if (stringp(form)) printstring(form, pfun);
  else if (arrayp(form)) printarray(form, pfun);
  else if (form->type == CODE) pfstring("code", pfun);
  else if (streamp(form)) pstream(form, pfun);
  else error2("error in print");
}

static void prin1object (object *form, pfun_t pfun) {
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printobject(form, pfun);
  Flags = temp;
}

// Read functions

static int glibrary (void) {
  return sys_fgetc(SYS_FTMP);
}

static void gserial_flush (void) {
}

static int gserial (void) {
  int c = sys_getc();
  if (c == '\n' && sys_interactive()) clrflag(NOECHO);
  if (c < 0) { pfl(pserial); sys_exit(0); }  // End of input
  return c;
}

static object *nextitem (gfun_t gfun) {
  int ch = gfun();
  while(issp(ch)) ch = gfun();

  if (ch == ';') {
    do { ch = gfun(); if (ch == ';' || ch == '(') setflag(NOECHO); }
    while(ch != '(');
  }
  if (ch == '\n') ch = gfun();
  if (ch == -1) return (object *)ENDOFINPUT;
  if (ch == ')') return (object *)KET;
  if (ch == '(') return (object *)BRA;
  if (ch == '\'') return (object *)QUO;

  // Parse string
  if (ch == '"') return readstring('"', true, gfun);

  // Parse symbol, character, or number
  int index = 0, base = 10, sign = 1;
  char buffer[BUFFERSIZE];
  int bufmax = BUFFERSIZE-3; // Max index
  unsigned int result = 0;
  bool isfloat = false;
  float fresult = 0.0;

  if (ch == '+') {
    buffer[index++] = ch;
    ch = gfun();
  } else if (ch == '-') {
    sign = -1;
    buffer[index++] = ch;
    ch = gfun();
  } else if (ch == '.') {
    buffer[index++] = ch;
    ch = gfun();
    if (ch == ' ') return (object *)DOT;
    isfloat = true;
  }

  // Parse reader macros
  else if (ch == '#') {
    ch = gfun();
    char ch2 = ch & ~0x20; // force to upper case
    if (ch == '\\') { // Character
      base = 0; ch = gfun();
      if (issp(ch) || isbr(ch)) return character(ch);
      else LastChar = ch;
    } else if (ch == '|') {
      do { while (gfun() != '|'); }
      while (gfun() != '#');
      return nextitem(gfun);
    } else if (ch2 == 'B') base = 2;
    else if (ch2 == 'O') base = 8;
    else if (ch2 == 'X') base = 16;
    else if (ch == '\'') return nextitem(gfun);
    else if (ch == '.') {
      setflag(NOESC);
      object *result = eval(read(gfun), NULL);
      clrflag(NOESC);
      return result;
    }
    else if (ch == '(') { LastChar = ch; return readarray(1, read(gfun)); }
    else if (ch == '*') return readbitarray(gfun);
    else if (ch >= '1' && ch <= '9' && (gfun() & ~0x20) == 'A') return readarray(ch - '0', read(gfun));
    else error2("illegal character after #");
    ch = gfun();
  }
  int valid; // 0=undecided, -1=invalid, +1=valid
  if (ch == '.') valid = 0; else if (digitvalue(ch)<base) valid = 1; else valid = -1;
  bool isexponent = false;
  int exponent = 0, esign = 1;
  float divisor = 10.0;

  while(!issp(ch) && !isbr(ch) && ch != -1 && index < bufmax) {
    buffer[index++] = ch;
    if (base == 10 && ch == '.' && !isexponent) {
      isfloat = true;
      fresult = result;
    } else if (base == 10 && (ch == 'e' || ch == 'E')) {
      if (!isfloat) { isfloat = true; fresult = result; }
      isexponent = true;
      if (valid == 1) valid = 0; else valid = -1;
    } else if (isexponent && ch == '-') {
      esign = -esign;
    } else if (isexponent && ch == '+') {
    } else {
      int digit = digitvalue(ch);
      if (digitvalue(ch)<base && valid != -1) valid = 1; else valid = -1;
      if (isexponent) {
        if (exponent < 100000) exponent = exponent * 10 + digit;  // Past any float; no overflow
      } else if (isfloat) {
        fresult = fresult + digit / divisor;
        divisor = divisor * 10.0;
      } else {
        result = result * base + digit;
      }
    }
    ch = gfun();
  }

  buffer[index] = '\0';
  if (isbr(ch)) LastChar = ch;
  if (isfloat && valid == 1) return makefloat(fresult * sign * tento(exponent * esign));
  else if (valid == 1) {
    if (base == 10 && result > ((unsigned int)INT_MAX+(1-sign)/2))
      return makefloat((float)result*sign);
    return number(result*sign);
  } else if (base == 0) {
    if (index == 1) return character(buffer[0]);
    const char *p = ControlCodes; char c = 0;
    while (c < 33) {
      if (strcasecmp(buffer, p) == 0) return character(c);
      p = p + strlen(p) + 1; c++;
    }
    if (index == 3) return character((buffer[0]*10+buffer[1])*10+buffer[2]-5328);
    error2("unknown character");
  }

  builtin_t x = lookupbuiltin(buffer);
  if (x == NIL) return nil;
  if (x != ENDFUNCTIONS) return bsymbol(x);
  if (index <= 6 && valid40(buffer)) return intern(twist(pack40(buffer)));
  return internlong(buffer);
}

static object *readrest (gfun_t gfun) {
  checkstack();
  object *item = nextitem(gfun);
  object *head = NULL;
  object *tail = NULL;

  while (item != (object *)KET) {
    if (item == (object *)ENDOFINPUT) error2("incomplete list");
    if (item == (object *)BRA) {
      item = readrest(gfun);
    } else if (item == (object *)QUO) {
      item = cons(bsymbol(QUOTE), cons(read(gfun), NULL));
    } else if (item == (object *)DOT) {
      tail->cdr = read(gfun);
      if (readrest(gfun) != NULL) error2("malformed list");
      return head;
    } else {
      object *cell = cons(item, NULL);
      if (head == NULL) head = cell;
      else tail->cdr = cell;
      tail = cell;
      item = nextitem(gfun);
    }
  }
  return head;
}

static gfun_t GFun;

static int glast (void) {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  return GFun();
}

static object *readmain (gfun_t gfun) {
  GFun = gfun;
  if (LastChar) { LastChar = 0; error2("read can only be used with one stream at a time"); }
  LastChar = 0;
  return read(glast);
}

static object *read (gfun_t gfun) {
  checkstack();
  object *item = nextitem(gfun);
  if (item == (object *)ENDOFINPUT) return nil;
  if (item == (object *)KET) error2("incomplete list");
  if (item == (object *)BRA) return readrest(gfun);
  if (item == (object *)DOT) return read(gfun);
  if (item == (object *)QUO) return cons(bsymbol(QUOTE), cons(read(gfun), NULL));
  return item;
}

// Setup

static void initenv (void) {
  GlobalEnv = NULL;
  tee = bsymbol(TEE);
}

// Read/Evaluate/Print loop

static void repl (object *env) {
  for (;;) {
    if (!tstflag(NOECHO)) gc(NULL, env);
    pint(Freespace+1, pserial);
    if (BreakLevel) {
      pfstring(" : ", pserial);
      pint(BreakLevel, pserial);
    }
    pserial('>'); pserial(' ');
    Context = NIL;
    object *line = readmain(gserial);
    // Break handling
    if (BreakLevel) {
      if (line == nil || line == bsymbol(COLONC)) {
        pln(pserial); return;
      } else if (line == bsymbol(COLONA)) {
        pln(pserial); pln(pserial);
        GCStack = NULL;
        longjmp(*handler, 1);
      } else if (line == bsymbol(COLONB)) {
        pln(pserial); printbacktrace();
        line = bsymbol(NOTHING);
      }
    }
    if (line == (object *)KET) error2("unmatched right bracket");
    protect(line);
    pfl(pserial);
    line = eval(line, env);
    pfl(pserial);
    printobject(line, pserial);
    unprotect();
    pfl(pserial);
    pln(pserial);
  }
}

void ulisp_fail (const char *message) {
  Context = NIL;
  error2(message);
}

static void ulisperror (void) {
  // Come here after error
  sys_fclose(SYS_FIN); sys_fclose(SYS_FOUT); sys_fclose(SYS_FTMP);
  clrflag(NOESC); BreakLevel = 0; TraceStart = 0; TraceTop = 0;
  setflag(PRINTREADABLY);  // An error inside princ leaves it clear
  for (int i=0; i<TRACEMAX; i++) TraceDepth[i] = 0;
}

// Evaluates the forms of a file without printing their values
static void loadfile (const char *path) {
  if (!sys_fopen(SYS_FIN, path, SYS_READ)) {
    pfstring(path, pserial); pfstring(": cannot open\n", pserial);
    return;
  }
  object *line = readmain(SDread);
  while (line != NULL) {
    if (line == (object *)KET) error2("unmatched right bracket");
    protect(line);
    eval(line, NULL);
    unprotect();
    line = readmain(SDread);
  }
  sys_fclose(SYS_FIN);
}

static void usage (void) {
  pfstring("usage: ulisp [-w cells] [-i image] [file ...]\n", pserial);
  sys_exit(2);
}

int main (int argc, char **argv) {
  volatile int next = 1;
  const char *volatile image = NULL;
  unsigned int cells = 0;
  while (next < argc && argv[next][0] == '-' && argv[next][1] != '\0') {
    char option = argv[next++][1];
    if (next == argc) usage();
    if (option == 'w') {
      for (const char *s = argv[next]; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || cells > 100000000) usage();
        cells = cells*10 + (*s - '0');
      }
    } else if (option == 'i') image = argv[next];
    else usage();
    next++;
  }
  Workspace = sys_init(&cells, sizeof(object), &StackLimit);
  if (Workspace == NULL || cells < 256) {
    pfstring("ulisp: not enough memory for the workspace\n", pserial);
    sys_exit(1);
  }
  WorkspaceSize = cells;
  initworkspace();
  initenv();
  pfstring("uLisp 4.8f ", pserial); pln(pserial);
  if (!setjmp(toplevel_handler)) {
    if (image != NULL) {
      const char *path = image;
      object *autorun;
      image = NULL;
      loadimagefile(path, &autorun);
      if (autorun != NULL) apply(autorun, NULL, NULL);
    }
  }
  ulisperror();
  while (next < argc) loadfile(argv[next++]);
  repl(NULL);
  return 0;
}
