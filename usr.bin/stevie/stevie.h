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

char *strsave(), *alloc();
char *nextline(), *prevline(), *coladvance(), *ssearch();
char *fwdsearch(), *bcksearch();
