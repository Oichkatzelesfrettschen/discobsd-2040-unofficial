#ifndef _CTYPE_H_
#define _CTYPE_H_

#define _U      01
#define _L      02
#define _N      04
#define _S      010
#define _P      020
#define _C      040
#define _X      0100
#define _B      0200

/*
 * One class byte for EOF, then one per unsigned char value, so the macros
 * index it with EOF or any unsigned char value: the argument domain of
 * C17 7.4p1.  The library has no setlocale(), so the table is constant.
 */
extern const char _ctype_[];

/*
 * C17 7.4.  Every function exists in libc, so a name may be parenthesized,
 * #undef'd or used as a pointer.
 */
int     isalnum(int);
int     isalpha(int);
int     isblank(int);
int     iscntrl(int);
int     isdigit(int);
int     isgraph(int);
int     islower(int);
int     isprint(int);
int     ispunct(int);
int     isspace(int);
int     isupper(int);
int     isxdigit(int);
int     tolower(int);
int     toupper(int);

int     isascii(int);
int     toascii(int);

/*
 * Each macro evaluates its argument once, as C17 7.1.4p1 requires of a
 * macro that masks a function.  isblank, tolower and toupper have no such
 * expression and are functions alone.
 */
#define isalpha(c)      ((_ctype_+1)[(int)(c)]&(_U|_L))
#define isupper(c)      ((_ctype_+1)[(int)(c)]&_U)
#define islower(c)      ((_ctype_+1)[(int)(c)]&_L)
#define isdigit(c)      ((_ctype_+1)[(int)(c)]&_N)
#define isxdigit(c)     ((_ctype_+1)[(int)(c)]&(_N|_X))
#define isspace(c)      ((_ctype_+1)[(int)(c)]&_S)
#define ispunct(c)      ((_ctype_+1)[(int)(c)]&_P)
#define isalnum(c)      ((_ctype_+1)[(int)(c)]&(_U|_L|_N))
#define isprint(c)      ((_ctype_+1)[(int)(c)]&(_P|_U|_L|_N|_B))
#define isgraph(c)      ((_ctype_+1)[(int)(c)]&(_P|_U|_L|_N))
#define iscntrl(c)      ((_ctype_+1)[(int)(c)]&_C)
#define isascii(c)      ((unsigned)(c)<=0177)
#define toascii(c)      ((c)&0177)

#endif /* !_CTYPE_H_ */
