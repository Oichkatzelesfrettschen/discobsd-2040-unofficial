/*
 * games/backgammon/subs.c decides two things about the terminal, and both
 * used to be spelled as fixed bytes:
 *
 *   readc() ends the game on the interrupt character.  RAW, which main() and
 *   teach.c select outside the V7 build, leaves the driver no chance to turn
 *   that character into SIGINT, so this test is the only exit the keyboard
 *   reaches.  A fixed 0177 there also swallowed the erase character, because
 *   <sys/ttychars.h> defaults CERASE to 0177: erasing a mistyped move quit
 *   and lost the position.
 *
 *   crterase() says whether the terminal overwrites the cell that a
 *   destructive backspace names, which decides between "\010 \010" and
 *   echoing back what was rubbed out.  That is LCRTERA in the local mode
 *   word and nothing else: backspace and DEL are two input characters, and
 *   which of them sg_erase holds says nothing about what the display does
 *   with the byte written back.  Reading the erase character for it conflates
 *   the two, so the suite drives them apart.
 *
 * The suite links the real subs.c and supplies the symbols its siblings
 * define, so what it measures is the shipped decision rather than a copy.
 */
#include <unistd.h>

#include "back.h"
#include "bg_shim.h"

/*
 * subs.c reaches these across the directory; the game's own definitions live
 * in files this gate does not link, so they stand in here with the values the
 * two decisions are read under: no cursor addressing, no case folding.
 */
int	acnt;
int	aflag;
int	bflag;
int	cflag;
int	iroll;
int	pnum;
int	rfl;
int	rflag;
int	tflag;
char	*color[] = { "White", "Red", "white", "red", 0 };

void	clend(void) {}
void	cline(void) {}
void	curmove(int r, int c) { (void)r; (void)c; }
void	fancyc(int c) { (void)c; }
void	newpos(void) {}
void	recover(char *s) { (void)s; }
void	save(int n) { (void)n; }
int	getcaps(char *s) { (void)s; return (0); }

static int
readc_child(void)
{
	return (readc());
}

/* QUIT stands apart from every byte readc() returns, all of which are
   under 128, so one call reports both which bytes end the game and what
   the rest come back as. */
#define QUIT	255

static int
fed(int byte)
{
	bg_feed(byte);
	return (bg_child(readc_child));
}

/* The character readc() ends the game on. */
static void
check_quit_character(void)
{
	tchars.t_intrc = CINTR;
	CHECK(fed(CINTR) == QUIT);

	/* CERASE, the default erase character, is not a quit character. */
	CHECK(fed(CERASE) != QUIT);

	/* A rebound interrupt character moves the exit with it. */
	tchars.t_intrc = 'q';
	CHECK(fed('q') == QUIT);
	CHECK(fed(CINTR) != QUIT);

	tchars.t_intrc = CINTR;
}

/*
 * What readc() returns for the bytes it does not quit on.  DEL is the case
 * the fixed 0177 took away: table.c and save.c can only compare it against
 * tty.sg_erase once readc() hands it back.
 */
static void
check_returned_characters(void)
{
	tchars.t_intrc = CINTR;
	cflag = 0;

	CHECK(fed(CERASE) == CERASE);

	CHECK(fed('\033') == '\n');
	CHECK(fed('\015') == '\n');

	CHECK(fed('\014') == 'R');
	CHECK(fed('a') == 'A');

	cflag = 1;
	CHECK(fed('a') == 'a');
	CHECK(fed('\014') == '\014');
	cflag = 0;
}

/*
 * crterase() answers from LCRTERA, and from nothing else.  The two questions
 * are driven against each other: every erase character the game can be given,
 * under each setting of the flag.  A crterase() that read the character would
 * pass one row of this table and fail the other.
 */
static void
check_crterase(void)
{
	static const int erasers[] = {
		'\010',		/* backspace */
		'\177',		/* DEL, which is also CERASE */
		'#',		/* the hardcopy default this convention came from */
		'\0',
		'x',
	};
	unsigned i;

	for (i = 0; i < sizeof erasers / sizeof erasers[0]; i++) {
		tty.sg_erase = (char)erasers[i];

		lflags = LCRTERA;
		CHECK(crterase() == 1);

		lflags = 0;
		CHECK(crterase() == 0);

		/* Other local modes do not stand in for it. */
		lflags = LCRTBS | LCRTKIL | LCTLECH;
		CHECK(crterase() == 0);

		lflags = LCRTERA | LCRTBS | LCRTKIL | LCTLECH;
		CHECK(crterase() == 1);
	}

	/*
	 * Backspace and DEL are distinct input characters: sg_erase holds one
	 * of them, and readc() hands back the other as an ordinary byte for
	 * table.c to compare.  Neither is consulted for the display class.
	 */
	lflags = LCRTERA;
	tty.sg_erase = '\010';
	CHECK(fed('\177') == '\177');
	tty.sg_erase = '\177';
	CHECK(fed('\010') == '\010');

	tty.sg_erase = CERASE;
}

int
main(void)
{
	bg_silence_stdout();

	tflag = 0;
	tty.sg_erase = CERASE;

	check_quit_character();
	check_returned_characters();
	check_crterase();

	return (bg_verdict("backgammon_contracts"));
}
