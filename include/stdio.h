/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef FILE

#define BUFSIZ  1024
extern  struct  _iobuf {
    int     _cnt;
    char    *_ptr;      /* should be unsigned char */
    char    *_base;     /* ditto */
    int     _bufsiz;
    short   _flag;
    short   _file;
    unsigned char _ub[1];   /* pushed-back byte on a string stream; ungetc.c */
} _iob[];

#define _IOREAD     01
#define _IOWRT      02
#define _IONBF      04
#define _IOMYBUF    010
#define _IOEOF      020
#define _IOERR      040
#define _IOSTRG     0100
#define _IOLBF      0200
#define _IORW       0400
#define _IOSYSLOG   01000  /* string stream carries syslog %m text */
#define _IOUNGET    02000  /* _ub holds a byte and _bufsiz the saved _cnt */

/*
 * The following definition is for ANSI C, which took them
 * from System V, which brilliantly took internal interface macros and
 * made them official arguments to setvbuf(), without renaming them.
 * Hence, these ugly _IOxxx names are *supposed* to appear in user code.
*/
#define _IOFBF      0   /* setvbuf should set fully buffered */
                        /* _IONBF and _IOLBF are used from the flags above */

#ifndef NULL
#define NULL        0
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned size_t;
#endif

#define FILE        struct _iobuf
#define EOF         (-1)

#define stdin       (&_iob[0])
#define stdout      (&_iob[1])
#define stderr      (&_iob[2])

#define SEEK_SET    0   /* set file offset to offset */
#define SEEK_CUR    1   /* set file offset to current plus offset */
#define SEEK_END    2   /* set file offset to EOF plus offset */

#define P_tmpdir    "/tmp/"
#define L_tmpnam    12  /* including the terminator for /tmp/XXXXXX */
#define L_ctermid   9   /* including the terminator for /dev/tty */

/*
 * The three counts C17 7.21.1 requires of an implementation.
 *
 * FOPEN_MAX is NSTATIC in lib/libc/stdio/findiop.c, the streams a program
 * reaches without an allocation, against the floor of eight C17 sets; a
 * ninth stream allocates and may fail, so eight is what is guaranteed.
 * FILENAME_MAX is PATH_MAX from <sys/syslimits.h>, the length namei
 * accepts, and lib/libc/stdio/fopen.c asserts the two agree. TMP_MAX
 * counts the names lib/libc/gen/mktemp.c reaches by stepping one template
 * position through 'a' to 'z', against C17's floor of 25.
 */
#define FOPEN_MAX   8
#define FILENAME_MAX 256
#define TMP_MAX     26

/*
 * C17 7.21.1 makes fpos_t an object type recording a position and, where an
 * implementation converts multibyte characters, the parse state that goes
 * with it. Every stream here is a byte stream, so the position is the whole
 * of it and a long holds the offsets lseek returns.
 */
typedef long fpos_t;

void    clearerr(FILE *);
int     feof(FILE *);
int     ferror(FILE *);
int     fileno(FILE *);

FILE    *fopen (const char *, const char *);
FILE    *fdopen (int, const char *);
FILE    *freopen (const char *, const char *, FILE *);
FILE    *popen (const char *, const char *);
int     pclose (FILE *);
FILE    *tmpfile (void);
char    *tmpnam (char [L_tmpnam]);
char    *tempnam (const char *, const char *);
char    *ctermid (char *);
int     fclose (FILE *);
long    ftell (FILE *);
int     fflush (FILE *);
int     fgetc (FILE *);
int     ungetc (int, FILE *);
int     fputc (int, FILE *);
int     fputs (const char *, FILE *);
int     puts (const char *);
char    *fgets (char *, int, FILE *);
/*
 * gets() is not declared: it cannot be given a bound, so every call is a
 * store of whatever the line holds. C11 removed it from the standard and
 * lib/libc/stdio holds no definition, so a caller fails at the implicit
 * declaration rather than at run time. fgets() takes its place.
 */
FILE    *_findiop (void);
void    _fwalk (int (*)(FILE *));
void    _cleanup (void);
int     _filbuf (FILE *);
int     _flsbuf (unsigned char, FILE *);
void    setbuf (FILE *, char *);
void    setbuffer (FILE *, char *, size_t);
void    setlinebuf (FILE *);
int     setvbuf (FILE *, char *, int, size_t);
int     fseek (FILE *, long, int);
void    rewind (FILE *);
int     fgetpos (FILE *, fpos_t *);
int     fsetpos (FILE *, const fpos_t *);
int     remove (const char *);
int     rename (const char *, const char *);
int     getw(FILE *stream);
int     putw(int w, FILE *stream);

size_t  fread (void *, size_t, size_t, FILE *);
size_t  fwrite (const void *, size_t, size_t, FILE *);

int     fprintf (FILE *, const char *, ...);
int     printf (const char *, ...);
int     sprintf (char *, const char *, ...);
int     snprintf (char *, size_t, const char *, ...);

int     fscanf (FILE *, const char *, ...);
int     scanf (const char *, ...);
int     sscanf (const char *, const char *, ...);

#ifndef _VA_LIST_
# ifdef __GNUC__
#  define va_list   __builtin_va_list   /* For Gnu C */
# endif
# ifdef __SMALLER_C__
#  define va_list   char *              /* For Smaller C */
# endif
#endif

int     vfprintf (FILE *, const char *, va_list);
int     vprintf (const char *, va_list);
int     vsprintf (char *, const char *, va_list);
int     vsnprintf (char *, size_t, const char *, va_list);

int     vfscanf (FILE *, const char *, va_list);
int     vscanf (const char *, va_list);
int     vsscanf (const char *, const char *, va_list);

int     _doprnt (const char *, va_list, FILE *);
int     _doscan (FILE *, const char *, va_list);

#ifndef _VA_LIST_
# undef va_list
#endif

void    perror (const char *);

#ifndef lint
#define getc(p)     (--(p)->_cnt>=0? (int)(*(unsigned char *)(p)->_ptr++):_filbuf(p))
#define putc(x, p)  (--(p)->_cnt >= 0 ?\
    (int)(*(unsigned char *)(p)->_ptr++ = (x)) :\
    (((p)->_flag & _IOLBF) && -(p)->_cnt < (p)->_bufsiz ?\
        ((*(p)->_ptr = (x)) != '\n' ?\
            (int)(*(unsigned char *)(p)->_ptr++) :\
            _flsbuf(*(unsigned char *)(p)->_ptr, p)) :\
        _flsbuf((unsigned char)(x), p)))
#endif /* not lint */

#define getchar()       getc(stdin)
#define putchar(x)      putc(x,stdout)
#define __sfeof(p)      (((p)->_flag&_IOEOF)!=0)
#define __sferror(p)    (((p)->_flag&_IOERR)!=0)
#define __sfileno(p)    ((p)->_file)
#define __sclearerr(p)  ((p)->_flag &= ~(_IOERR|_IOEOF))

#define feof(p)         __sfeof(p)
#define ferror(p)       __sferror(p)
#define fileno(p)       __sfileno(p)
#define clearerr(p)     __sclearerr(p)

#endif /* _FILE */
