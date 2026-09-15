/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Public domain (Unlicense); see LICENSE in this directory.
 */

/*
 * The upstream ATARI/UNIXPC/TCAP selection is gone: window.c targets one
 * console, a fixed 80x24 ANSI/VT100 terminal reached through this port's
 * sgtty ioctls, and HOSTBUILD swaps only those ioctls for termios so the
 * pty smoke test can drive the same editor.
 */

#include <stdlib.h>
#include <string.h>

/*
 * FILELENG is the whole edit buffer, one flat malloc, and the single knob
 * for this program's footprint in a 96 kbyte process. 24000 leaves room
 * for ~25 kbytes of text and data, ~8 kbytes of bss (the chars table and
 * its strings, Redobuff/Undobuff/Insbuff, getcbuff), the two 1920-byte
 * screen buffers, and yankline's copy of whatever a d-count deletes.
 */
#define FILELENG 24000

#define NORMAL 0
#define CMDLINE 1
#define INSERT 2
#define APPEND 3
#define FORWARD 4
#define BACKWARD 5
#define WORDSEP " \t\n()[]{},;:'\"-="

#define CHANGED Changed=1
#define UNCHANGED Changed=0

/* The interpretation of one unprintable byte, as filetonext splices it
 * into the screen: ch_size columns wide, spelled by ch_str when wider
 * than one column. */
struct charinfo {
	char ch_size;
	char *ch_str;
};

extern struct charinfo chars[];

extern int State;
extern int Rows;
extern int Columns;
extern char *Realscreen;
extern char *Nextscreen;
extern char *Filename;
extern char *Filemem;
extern char *Filemax;
extern char *Fileend;
extern char *Topchar;
extern char *Botchar;
extern char *Curschar;
extern char *Insstart;
extern int Cursrow, Curscol, Cursvcol;
extern int Prenum;
extern int Debug;
extern int Changed;
extern int Binary;
extern char Redobuff[], Undobuff[], Insbuff[];
extern char *Uncurschar, *Insptr;
extern int Ninsert, Undelchars;

/*
 * Prototypes for every function defined in cmdline.c edit.c help.c
 * hexchars.c linefunc.c main.c misccmds.c normal.c window.c, one block
 * per source file, in definition order.
 */

/* cmdline.c */
void readcmdline(int firstc);
void badcmd(void);
void gotocmd(int clr, int fresh, int firstc);
void message(char *s);
int writeit(char *fname);
void filemess(char *s);

/* edit.c */
void edit(void);
void insertchar(int c);
int gethexchar(void);
void getout(void);
void cursupdate(void);
void scrolldown(int nlines);
int oneright(void);
int oneleft(void);
void beginline(void);
int oneup(int n);
int onedown(int n);

/* help.c */
void help(void);
void longline(char *p);

/* hexchars.c */
void octchars(void);
void hexchars(void);
void decchars(void);
int hextoint(int c);

/* linefunc.c */
char *nextline(char *curr);
char *prevline(char *curr);
char *coladvance(char *p, int col);
char *alloc(unsigned size);
char *strsave(char *string);
char *ssearch(int dir, char *str);
void dosearch(int dir, char *str);
void repsearch(void);
char *fwdsearch(char *str);
char *bcksearch(char *str);

/* main.c */
int main(int argc, char **argv);
void filetonext(void);
void nexttoscreen(void);
void updatescreen(void);
void screenclear(void);
void filealloc(void);
void screenalloc(void);
int readfile(char *fname, char *fromp, int nochangename);
void stuffin(char *s);
void addtobuff(char *s, char c1, char c2, char c3, char c4, char c5, char c6);
int vgetc(void);
int vpeekc(void);
int anyinput(void);

/* misccmds.c */
void opencmd(void);
int issepchar(char c);
int cntlines(char *pbegin, char *pend);
void fileinfo(void);
void gotoline(int n);
void yankline(int n);
void putline(int k);
void inschar(int c);
void insstr(char *s);
void appchar(int c);
int canincrease(int n);
void delchar(void);
void delword(int deltrailing);
void delline(int nlines);

/* normal.c */
void normal(int c);
void tabinout(int inout, int num);
void startinsert(char *initstr);
void resetundo(void);

/* window.c */
void windinit(void);
void windrestore(void);
void windgoto(int r, int c);
void windexit(int r);
void windclear(void);
void windrefresh(void);
int windgetc(void);
void windstr(char *s);
void windputc(int c);
void beep(void);
