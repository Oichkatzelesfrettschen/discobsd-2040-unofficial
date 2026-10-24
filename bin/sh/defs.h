/*
 * UNIX shell
 */

/* error exits from various parts of shell */
#define ERROR           1
#define SYNBAD          2
/*
 * Exit statuses the sh utility page fixes for a command that the
 * search in service.c could not run: 126 once a file was found and
 * exec refused it, 127 while no file was found at all.
 */
#define EXNOEXEC        126
#define EXNOTFOUND      127
#define SIGFAIL         2000
#define SIGFLG          0200

/* command tree */
#define FPRS            0x0100
#define FINT            0x0200
#define FAMP            0x0400
#define FPIN            0x0800
#define FPOU            0x1000
#define FPCL            0x2000
#define FCMD            0x4000
#define COMMSK          0x00F0
#define CNTMSK          0x000F

#define TCOM            0x0000
#define TPAR            0x0010
#define TFIL            0x0020
#define TLST            0x0030
#define TIF             0x0040
#define TWH             0x0050
#define TUN             0x0060
#define TSW             0x0070
#define TAND            0x0080
#define TORF            0x0090
#define TFORK           0x00A0
#define TFOR            0x00B0
#define TFND            0x00C0

/* execute table */
#define SYSSET          1
#define SYSCD           2
#define SYSEXEC         3

#define SYSLOGIN        4
#define SYSNEWGRP       29

#define SYSTRAP         5
#define SYSEXIT         6
#define SYSSHFT         7
#define SYSWAIT         8
#define SYSCONT         9
#define SYSBREAK        10
#define SYSEVAL         11
#define SYSDOT          12
#define SYSRDONLY       13
#define SYSTIMES        14
#define SYSXPORT        15
#define SYSNULL         16
#define SYSREAD         17
#define SYSTST          18

#ifndef RES     /*      exclude umask code      */
#define SYSUMASK        20
#define SYSULIMIT       21
#endif

#define SYSECHO         22
#define SYSHASH         23
#define SYSPWD          24
#define SYSRETURN       25
#define SYSUNS          26
#define SYSMEM          27
#define SYSTYPE         28
#define SYSCOMMAND      30
#define SYSGETOPTS      31

/* used for input and output of shell */
#define INIO            19
#define OTIO            18

/*io nodes*/
#define USERIO          10
#define IOUFD           15
#define IODOC           16
#define IOPUT           32
#define IOAPP           64
#define IOMOV           128
#define IORDW           256
#define IOSTRIP         512
#define IOQUOTE         1024
#define INPIPE          0
#define OTPIPE          1

/* arg list terminator */
#define ENDARGS         0

#include "mac.h"
#include "mode.h"
#include "name.h"
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/*      error catching */
extern int              errno;

/* result type declarations */

struct direct;

#define free                             afree
extern void addblok(unsigned int reqd);
extern void afree(void *ap);
extern char * alloc(unsigned int nbytes);
extern void parse_alloc_begin(void);
extern void parse_alloc_commit(void);
extern void parse_alloc_rollback(void);
extern int any(char c, char *s);
extern void assnum(char **p, int i);
extern void assign(struct namnod *n, char *v);
extern void await(int i, int bckg);
extern void chktrap(void);
extern void clrsig(int i);
extern void cmdfail(char *s1, char *s2, int xno);
extern int chkopen(char *idf);
extern void chkpipe(int *pv);
extern void chkpr(void);
extern void clear_buff(void);
extern void clearup(void);
extern int cf(char *s1, char *s2);
extern BOOL chkid(char *nam);
extern struct trenod * cmd(int sym, int flg);
extern void copy(struct ionod *ioparg);
extern int create(char *s);
extern void cwd(char *dir);
extern char * cwdpath(void);
extern void dfault(struct namnod *n, char *v);
extern _Noreturn void done(void);
extern int echo(int argc, char **argv);
extern void error(char *s);
extern int estabf(char *s);
extern int execa(char **at, short pos);
extern int execute(struct trenod *argt, volatile int exec_link, int errorflg, int *pf1, int *pf2);
union execution_input_tail
{
	char **arguments;
	int descriptor;
};
extern void execute_input(char *string, union execution_input_tail tail,
    int errorflg);
static inline void
execute_argument_input(char **arguments, int errorflg)
{
	union execution_input_tail tail = { .arguments = &arguments[1] };

	execute_input(arguments[0], tail, errorflg);
}
static inline void
execute_descriptor_input(int descriptor, int errorflg)
{
	union execution_input_tail tail = { .descriptor = descriptor };

	execute_input(NIL, tail, errorflg);
}
static inline void
execute_string_input(char *string, int errorflg)
{
	union execution_input_tail tail = { .arguments = (char **)NIL };

	execute_input(string, tail, errorflg);
}
extern _Noreturn void exitsh(int xno);
extern int expand(char *as, int rcnt);
extern _Noreturn void failed(char *s1, char *s2);
extern _Noreturn void failure(char *s1, char *s2, int xno);
extern _Noreturn void recover_or_exit(int xno);
extern struct namnod * findnam(char *nam);
extern int findpath(char *name, int oldpath, char *path);
extern void flushb(void);
extern void freefunc(struct namnod *n);
extern void freetree(struct trenod *t);
extern void func_unhash(char *name);
extern int getarg(struct comnod *ac);
extern char * getpath(char *s);
extern void getsig(int n);
extern struct direct * getdir(int dirf);
extern int gmatch(char *s, char *p);
extern void hash_func(char *name);
extern short hash_cmd(char *name);
extern void hashpr(void);
extern void hcreate(void);
extern void initf(int fd);
extern int initio(struct ionod *iop, int save);
extern int ignsig(int n);
extern void itos(int n);
extern int length(char *as);
extern void link_iodocs(struct ionod *i);
extern char * mactrim(char *s);
extern char * make(char *v);
extern void makearg(struct argnod *args);
extern struct trenod * makefork(int flgs, struct trenod *i);
extern char * macro(char *as);
extern char * movstr(char *a, char *b);
extern char * movstrn(char *a, char *b, int n);
extern void namscan(void (*fn)(struct namnod *));
extern int nextc(char quote);
extern char * nextpath(char *path);
extern void oldsigs(void);
extern int options(int argc, char **argv);
extern int pathopen(char *path, char *name);
extern short pathlook(char *com, int flg, struct argnod *arg);
extern int pop(void);
extern int poptemp(void);
extern void post(int pcsid);
extern void postclr(void);
extern void prc(char c);
extern void prc_buff(char c);
extern void prf(struct trenod *t);
extern void prl(long n);
extern void prn(int n);
extern void prn_buff(int n);
extern void prp(void);
extern void prprompt(char *as);
extern void prs(char *as);
extern void prs_buff(char *s);
extern void prs_cntl(char *s);
extern void prt(long t);
extern void push(struct fileblk *af);
extern int readc(void);
extern int readvar(char **names);
extern void rename(int f1, int f2);
extern void replace(char **a, char *v);
extern void reset_dir(void);
extern void restore(int last);
extern void rmfunctmp(void);
extern void rmtemp(struct ionod *base);
extern char ** scan(int argn);
extern int savefd(int fd);
extern void set_dotpath(void);
extern void setargs(char **argi);
extern char * setbrk(int incr);
extern char ** setenvv(void);
extern void setlist(struct argnod *arg, int xp);
extern void setmail(char *mailpath);
extern void setup_env(void);
extern void settmp(void);
extern int skipc(void);
extern char * simple(char *s);
extern void stakchk(void);
extern void stdsigs(void);
extern int stoi(char *icp);
extern void subst(int in, int ot);
extern void swap_iodoc_nm(struct ionod *i);
extern int syslook(char *w, struct sysnod *syswds, int n);
extern void tdystak(char *x);
extern int test(int argn, char **com);
extern int tmpfil(struct tempblk *tb);
extern void trim(char *at);
extern void unset_name(char *name);
extern int word(void);
extern void zapcd(void);
extern void zaphash(void);
extern void cwdprint(void);
extern void what_is_path(char *name);
extern struct namnod * lookup(char *nam);
struct command_env;
extern struct command_env * command_beginlist(void);
extern void command_setlist(struct command_env *environment, struct argnod *arg);
extern void command_unwind_input(void);
extern int command_recovery_allowed(void);
extern void command_restorelist(struct command_env *environment);
extern void command_name_changed(struct namnod *node);
extern void command_attribute_changed(struct namnod *node, int attributes);
extern void getoptsreset(char *value);
extern struct dolnod * freeargs(struct dolnod *blk);
extern struct dolnod * useargs(void);
extern char * catpath(char *path, char *name);
extern int special_builtin(int internal);
extern short command_pathlook(char *name, char *path);
extern int command_report(char *name, int verbose, char *path);
extern int execa_path(char **at, short pos, char *search_path);
extern void exname(struct namnod *n);
extern void printnam(struct namnod *n);
extern void printro(struct namnod *n);
extern void printexp(struct namnod *n);

#define attrib(n,f)             (n->namflg |= f)
#define round(a,b)              (((int)(((char *)(a)+b)-1))&~((b)-1))
#define closepipe(x)    (close(x[INPIPE]), close(x[OTPIPE]))
#define eq(a,b)                 (cf(a,b)==0)
#define max(a,b)                ((a)>(b)?(a):(b))
#define assert(x)               ;

/* temp files and io */
extern int                              output;
extern int                              ioset;
extern struct ionod             *iotemp;        /* files to be deleted sometime */
extern struct ionod             *fiotemp;       /* function files to be deleted sometime */
extern struct ionod             *iopend;        /* documents waiting to be read at NL */
extern struct fdsave    fdmap[];


/* substitution */
extern int                              dolc;
extern char                             **dolv;
extern struct dolnod    *argfor;
extern struct argnod    *gchain;

/* stak stuff */
#include "stak.h"

/* string constants */
extern char                             atline[];
extern char                             readmsg[];
extern char                             colon[];
extern char                             minus[];
extern char                             nullstr[];
extern char                             sptbnl[];
extern char                             unexpected[];
extern char                             endoffile[];
extern char                             synmsg[];

/* name tree and words */
extern struct sysnod    reserved[];
extern int                              no_reserved;
extern struct sysnod    commands[];
extern int                              no_commands;

extern int                              wdval;
extern int                              wdnum;
extern int                              fndef;
extern int                              nohash;
extern struct argnod    *wdarg;
extern int                              wdset;
extern int                              wdquote;
extern BOOL                             reserv;

/* prompting */
extern char                             stdprompt[];
extern char                             supprompt[];
extern char                             profile[];
extern char                             sysprofile[];

/* built in names */
extern struct namnod    fngnod;
extern struct namnod    cdpnod;
extern struct namnod    ifsnod;
extern struct namnod    homenod;
extern struct namnod    mailnod;
extern struct namnod    pathnod;
extern struct namnod    ps1nod;
extern struct namnod    ps2nod;
extern struct namnod    mchknod;
extern struct namnod    acctnod;
extern struct namnod    mailpnod;

/* special names */
extern char                             flagadr[];
extern char                             *pcsadr;
extern char                             *pidadr;
extern char                             *cmdadr;

extern char                             defpath[];

/* names always present */
extern char                             mailname[];
extern char                             homename[];
extern char                             pathname[];
extern char                             cdpname[];
extern char                             ifsname[];
extern char                             ps1name[];
extern char                             ps2name[];
extern char                             mchkname[];
extern char                             acctname[];
extern char                             mailpname[];

/* transput */
extern char                             tmpout[];
extern char                             *tmpnam;
extern int                              serial;

#define TMPNAM          7

extern struct fileblk   *standin;

#define input           (standin->fdes)
#define eof                     (standin->feof)

extern int                              peekc;
extern int                              peekn;
extern char                             *comdiv;
extern char                             devnull[];

/* flags */
#define noexec          01
#define sysflg          01
#define intflg          02
#define prompt          04
#define setflg          010
#define errflg          020
#define ttyflg          040
#define forked          0100
#define oneflg          0200
#define rshflg          0400
#define waiting         01000
#define stdflg          02000
#define STDFLG          's'
#define execpr          04000
#define readpr          010000
#define keyflg          020000
#define hashflg         040000
#define nofngflg        0200000
#define exportflg       0400000

extern long                             flags;
extern int                              rwait;  /* flags read waiting */

/* error exits from various parts of shell */
#include <setjmp.h>
extern jmp_buf                  subshell;
extern jmp_buf                  errshell;

/* fault handling */
#include "brkincr.h"

extern unsigned                 brkincr;

#define MINTRAP         0
#define MAXTRAP         26

#define TRAPSET         2
#define SIGSET          4
#define SIGMOD          8
#define SIGCAUGHT	16

extern _Noreturn void done(void);
extern void fault(int sig);
extern BOOL				trapnote;
extern char				*trapcom[];
extern BOOL				trapflg[];

/* name tree and words */
extern char				**environ;
extern char				numbuf[];
extern char				export[];
extern char				duperr[];
extern char				readonly[];

/* execflgs */
extern int				exitval;
extern int				retval;
extern BOOL				execbrk;
extern int				loopcnt;
extern int				breakcnt;
extern int				funcnt;
extern jmp_buf				*command_errshell;

/* messages */
extern char				mailmsg[];
extern char				coredump[];
extern char				badopt[];
extern char				badparam[];
extern char				unset[];
extern char				badsub[];
extern char				nospace[];
extern char				nostack[];
extern char				notfound[];
extern char				badtrap[];
extern char				baddir[];
extern char				badshift[];
extern char				restricted[];
extern char				execpmsg[];
extern char				notid[];
extern char 			badulimit[];
extern char				wtfailed[];
extern char				badcreate[];
extern char				nofork[];
extern char				noswap[];
extern char				piperr[];
extern char				badopen[];
extern char				badnum[];
extern char				arglist[];
extern char				txtbsy[];
extern char				toobig[];
extern char				badexec[];
extern char				badfile[];
extern char				badreturn[];
extern char				badexport[];
extern char				badunset[];
extern char				nohome[];
extern char				badperm[];

/*	'builtin' error messages	*/

extern char				btest[];
extern char				badop[];

/*	fork constant	*/

#define FORKLIM 2               /* was 32: max retry timeout for fork() */

#include "ctype.h"

extern int				wasintr;	/* used to tell if break or delete is hit
		   					 *  while executing a wait */
extern int				eflag;


/*
 * Find out if it is time to go away.
 * `trapnote' is set to SIGSET when fault is seen and
 * no trap has been set.
 */

#define	sigchk()	if (trapnote & SIGSET)	\
                                exitsh(exitval ? exitval : SIGFAIL)

#define exitset()	retval = exitval

#define ENDPATH 27      /* see msg.c/defpath */
