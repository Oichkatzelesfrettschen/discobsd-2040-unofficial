/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"
#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
#include "sym.h"
#include "hash.h"

static int      parent;
static int      getopts_index = 1;
static int      getopts_offset;
static void     execprint();
extern short    topfd;

#define COMMAND_EXECUTE 0
#define COMMAND_SMALL_V 1
#define COMMAND_BIG_V 2
#define COMMAND_EMPTY 3
#define COMMAND_BAD -1

void
getoptsreset(value)
	char *value;
{
	while (*value == '0')
		value++;
	if (value[0] == '1' && value[1] == 0)
	{
		getopts_index = 1;
		getopts_offset = 0;
	}
}

static int
getopts_assign(name, value)
	char *name;
	char *value;
{
	struct namnod *node;

	node = lookup(name);
	if (node == &mchknod && !digit(value[0]))
	{
		prs("getopts");
		prs(colon);
		prs(name);
		prs(colon);
		prs(badnum);
		newline();
		return(0);
	}
	assign(node, value);
	return(1);
}

static int
getopts_assignable(name)
	char *name;
{
	struct namnod *node;
	char *reason;

	reason = NIL;
	if (!chkid(name))
		reason = notid;
	else if ((node = findnam(name)) && (node->namflg & N_RDONLY))
		reason = wtfailed;
	if (reason == NIL)
		return(1);
	prs("getopts");
	prs(colon);
	prs(name);
	prs(colon);
	prs(reason);
	newline();
	return(0);
}

static void
getopts_publish_index()
{
	if (getopts_index == 1)
		return;
	itos(getopts_index);
	assign(lookup("OPTIND"), numbuf);
}

static int
getopts_finish(name)
	char *name;
{
	if (!getopts_assign(name, "?"))
		return(2);
	unset_name("OPTARG");
	getopts_publish_index();
	return(1);
}

static int
getopts_builtin(argn, arguments)
	int argn;
	char **arguments;
{
	char **option_arguments;
	char *option_string;
	char *argument;
	char *match;
	char *diagnostic = NIL;
	char result[2];
	int argument_count;
	int option;
	int silent;

	if (argn < 3)
	{
		prs("getopts: usage: getopts optstring name [arg ...]\n");
		return(2);
	}
	if (!getopts_assignable(arguments[2]) ||
	    !getopts_assignable("OPTIND") ||
	    !getopts_assignable("OPTARG"))
		return(2);
	option_string = arguments[1];
	silent = option_string[0] == ':';
	match = option_string + silent;
	if (argn > 3)
	{
		option_arguments = &arguments[3];
		argument_count = argn - 3;
	}
	else
	{
		option_arguments = &dolv[1];
		argument_count = dolc;
	}

	if (getopts_offset < 0)
		return(getopts_finish(arguments[2]));
	if (getopts_offset == 0)
	{
		if (getopts_index > argument_count)
		{
			getopts_offset = -1;
			return(getopts_finish(arguments[2]));
		}
		argument = option_arguments[getopts_index - 1];
		if (argument[0] != '-' || argument[1] == 0)
		{
			getopts_offset = -1;
			return(getopts_finish(arguments[2]));
		}
		getopts_index++;
		if (argument[1] == '-' && argument[2] == 0)
		{
			getopts_offset = -1;
			return(getopts_finish(arguments[2]));
		}
		getopts_offset = 1;
	}
	argument = option_arguments[getopts_index - 2];
	option = argument[getopts_offset++];
	if (argument[getopts_offset] == 0)
		getopts_offset = 0;
	result[0] = option;

	while (*match && *match != option)
	{
		match++;
		if (*match == ':')
			match++;
	}
	if (*match == 0 || option == ':')
	{
		result[0] = '?';
		if (silent)
		{
			char option_argument[2];

			option_argument[0] = option;
			option_argument[1] = 0;
			getopts_assign("OPTARG", option_argument);
		}
		else
		{
			unset_name("OPTARG");
			diagnostic = "illegal option -- ";
		}
	}
	else if (match[1] == ':')
	{
		if (getopts_offset)
		{
			getopts_assign("OPTARG", &argument[getopts_offset]);
			getopts_offset = 0;
		}
		else if (getopts_index <= argument_count)
			getopts_assign("OPTARG", option_arguments[getopts_index++ - 1]);
		else
		{
			char option_argument[2];

			option_argument[0] = option;
			option_argument[1] = 0;
			result[0] = silent ? ':' : '?';
			if (silent)
				getopts_assign("OPTARG", option_argument);
			else
			{
				unset_name("OPTARG");
				diagnostic = "option requires an argument -- ";
			}
		}
	}
	else
	{
		result[0] = option;
		unset_name("OPTARG");
	}
	if (diagnostic)
	{
		prs("getopts: ");
		prs(diagnostic);
		prc(option);
		prc(NL);
	}
	result[1] = 0;
	if (!getopts_assign(arguments[2], result))
		return(2);
	getopts_publish_index();
	return(0);
}

static int
command_options(argn, arguments, first, standard_path)
	int argn;
	char **arguments;
	int *first;
	int *standard_path;
{
	int index = 1;
	int mode = COMMAND_EXECUTE;
	char *option;

	*standard_path = 0;
	while (index < argn && arguments[index][0] == '-' &&
	    arguments[index][1] != 0)
	{
		option = &arguments[index][1];
		if (option[0] == '-' && option[1] == 0)
		{
			index++;
			break;
		}
		while (*option)
		{
			if (*option == 'p')
				*standard_path = 1;
			else if (*option == 'v')
				mode = COMMAND_SMALL_V;
			else if (*option == 'V')
				mode = COMMAND_BIG_V;
			else
				return(COMMAND_BAD);
			option++;
		}
		index++;
	}
	*first = index;
	if (index == argn)
		return(mode == COMMAND_EXECUTE ? COMMAND_EMPTY : COMMAND_BAD);
	if (mode != COMMAND_EXECUTE && index + 1 != argn)
		return(COMMAND_BAD);
	return(mode);
}

struct command_context
{
	jmp_buf error;
	jmp_buf *previous;
	struct command_context *previous_context;
	struct command_env *environment;
	struct fileblk *input_checkpoint;
	char *path;
	short io;
	short wrapped;
	short recoverable;
	pid_t owner_pid;
	int saved_loopcnt;
	int saved_breakcnt;
	BOOL saved_execbrk;
};

static struct command_context *command_current;

#define ERREXIT_SUPPRESSED (-1)
#define ERREXIT_CONTEXT(errorflg) \
	((errorflg) < 0 ? ERREXIT_SUPPRESSED : (errorflg) | eflag)

int
command_recovery_allowed()
{
	return(command_current && command_current->recoverable &&
	    command_current->owner_pid == getpid());
}

void
command_unwind_input()
{
	if (command_current == NIL)
		return;
	while (standin != command_current->input_checkpoint && pop())
		;
}

static void
command_context_end(context)
	struct command_context *context;
{
	if (context->environment)
		command_restorelist(context->environment);
	command_errshell = context->previous;
	command_current = context->previous_context;
	free(context);
}

/* ========     command execution       ========*/

int
execute(argt, exec_link, errorflg, pf1, pf2)
struct trenod   *argt;
volatile int exec_link;
int     errorflg;
int     *pf1, *pf2;
{
	/*
	 * `stakbot' is preserved by this routine
	 */
	register struct trenod  *t;
	char            *sav = savstak();

	sigchk();
	if (errorflg <= 0)
		flags &= ~errflg;

	if ((t = argt) && execbrk == 0)
	{
		register int    treeflgs;
		volatile int            type;
		register char   **com;
		struct command_context *volatile command_state = NIL;
		volatile short          pos;
		volatile int            linked;
		int                     execflg;

		linked = exec_link >> 1;
		execflg = exec_link & 01;

		treeflgs = t->tretyp;
		type = treeflgs & COMMSK;

		switch (type)
		{
		case TFND:
			{
				struct fndnod   *f = (struct fndnod *)t;
				struct namnod   *n = lookup(f->fndnam);

				exitval = 0;

				if (n->namflg & N_RDONLY)
					failed(n->namid, wtfailed);
				command_name_changed(n);

				if (n->namflg & N_FUNCTN)
					freefunc(n);
				else
				{
					free(n->namval);
					free(n->namenv);

					n->namval = NIL;
					n->namflg &= ~(N_EXPORT | N_ENVCHG);
				}

				if (funcnt)
					f->fndval->tretyp++;

				n->namenv = (char *)f->fndval;
				attrib(n, N_FUNCTN);
				hash_func(n->namid);
				break;
			}

		case TCOM:
			{
				char    *a1;
				volatile int argn;
				int     internal;
				struct argnod   *schain = gchain;
				struct ionod *volatile io = t->treio;
				short   cmdhash = 0;
				short   comtype;

				exitval = 0;

				gchain = NIL;
				argn = getarg(t);
				com = scan(argn);
				a1 = com[1];
				gchain = schain;

				if (argn != 0)
					cmdhash = pathlook(com[0], 1, comptr(t)->comset);

				if (argn == 0 ||
				    ((comtype = hashtype(cmdhash)) == BUILTIN &&
				    hashdata(cmdhash) != SYSCOMMAND))
					setlist(comptr(t)->comset, 0);

				if (argn && (flags&noexec) == 0)
				{
					/* print command if execpr */

					if (flags & execpr)
						execprint(com);

					command_dispatch:
					if (comtype == NOTFOUND)
					{
						if (command_state && command_state->wrapped)
						{
							command_state->io = topfd;
							initio(io, 1);
						}
						pos = hashdata(cmdhash);
						if (pos == 1)
							cmdfail(*com, notfound, EXNOTFOUND);
						else if (pos == 2)
							cmdfail(*com, badexec, EXNOEXEC);
						else
							cmdfail(*com, badperm, EXNOEXEC);
						if (command_state && command_state->io >= 0)
						{
							restore(command_state->io);
							command_state->io = -1;
						}
						if (command_state)
						{
							command_context_end(command_state);
							command_state = NIL;
						}
						break;
					}

					else if (comtype == PATH_COMMAND)
					{
						pos = -1;
					}

					else if (comtype & (COMMAND | REL_COMMAND))
					{
						pos = hashdata(cmdhash);
					}

					else if (comtype == BUILTIN)
					{
						short index;
							int command_catch;

						internal = hashdata(cmdhash);
							if (internal == SYSCOMMAND)
							{
								int first;
								int standard_path;
								int mode;

								if (!command_state)
								{
									command_state = (struct command_context *)
									    alloc(sizeof *command_state);
									command_state->previous = command_errshell;
									command_state->previous_context = command_current;
									command_state->environment = NIL;
									command_state->input_checkpoint = standin;
									command_state->path = NIL;
									command_state->io = -1;
									command_state->wrapped = 0;
									command_state->recoverable = 1;
									command_state->owner_pid = getpid();
									command_state->saved_loopcnt = loopcnt;
									command_state->saved_breakcnt = breakcnt;
									command_state->saved_execbrk = execbrk;
									command_current = command_state;
									command_errshell = &command_state->error;
									if (setjmp(command_state->error))
									{
										loopcnt = command_state->saved_loopcnt;
										breakcnt = command_state->saved_breakcnt;
										execbrk = command_state->saved_execbrk;
										if (command_state->io >= 0)
											restore(command_state->io);
										command_context_end(command_state);
										command_state = NIL;
										flushb();
										chktrap();
										break;
									}
									command_state->environment = command_beginlist();
									command_state->recoverable = 0;
									command_setlist(command_state->environment,
									    comptr(t)->comset);
									command_state->recoverable = 1;
								}
								mode = command_options(argn, com, &first,
								    &standard_path);
								if (mode == COMMAND_BAD)
								{
									command_state->io = topfd;
									initio(io, 1);
									prs("command");
									prs(colon);
									prs(badopt);
									newline();
									exitval = EXNOTFOUND;
									flushb();
									restore(command_state->io);
									command_state->io = -1;
									command_context_end(command_state);
									command_state = NIL;
									chktrap();
									break;
								}
								if (mode == COMMAND_EMPTY)
								{
									command_state->io = topfd;
									initio(io, 1);
									exitval = 0;
									restore(command_state->io);
									command_state->io = -1;
									command_context_end(command_state);
									command_state = NIL;
									break;
								}
								if (!any('/', com[first]))
								{
									if (standard_path)
										command_state->path = defpath + 1;
									else if (command_state->path == NIL)
										command_state->path = getpath(com[first]);
								}
								if (mode == COMMAND_SMALL_V ||
								    mode == COMMAND_BIG_V)
								{
									command_state->io = topfd;
									initio(io, 1);
									exitval = command_report(com[first],
									    mode == COMMAND_BIG_V, command_state->path);
									flushb();
									restore(command_state->io);
									command_state->io = -1;
									command_context_end(command_state);
									command_state = NIL;
									chktrap();
									break;
								}
								com += first;
								argn -= first;
								a1 = com[1];
								command_state->wrapped = 1;
								cmdhash = command_pathlook(com[0],
								    command_state->path);
								comtype = hashtype(cmdhash);
								goto command_dispatch;
							}
							if (command_state)
								command_state->io = topfd;
						index = initio(io, (internal != SYSEXEC));
							command_catch = command_state != NIL;

						switch (internal)
						{
						case SYSDOT:
							if (a1)
							{
								register int    f;

								if ((f = pathopen(getpath(a1), a1)) < 0)
									failed(a1, notfound);
								else
									execexp(NIL, f, errorflg);
							}
							break;

						case SYSTIMES:
							{
									struct tms t;

									times(&t);
									prt(t.tms_cutime);
								prc_buff(SP);
									prt(t.tms_cstime);
								prc_buff(NL);
							}
							break;

						case SYSEXIT:
							flags |= forked;        /* force exit */
							exitsh(a1 ? stoi(a1) : retval);

						case SYSNULL:
							io = NIL;
							break;

						case SYSCONT:
							if (loopcnt)
							{
								execbrk = breakcnt = 1;
								if (a1)
									breakcnt = stoi(a1);
								if (breakcnt > loopcnt)
									breakcnt = loopcnt;
								else
									breakcnt = -breakcnt;
							}
							break;

						case SYSBREAK:
							if (loopcnt)
							{
								execbrk = breakcnt = 1;
								if (a1)
									breakcnt = stoi(a1);
								if (breakcnt > loopcnt)
									breakcnt = loopcnt;
							}
							break;

						case SYSTRAP:
							if (a1)
							{
								BOOL    clear;

								if ((clear = digit(*a1)) == 0)
									++com;
								while (*++com)
								{
									int     i;

									if ((i = stoi(*com)) >= MAXTRAP || i < MINTRAP)
										failed(*com, badtrap);
									else if (clear)
										clrsig(i);
									else
									{
										replace(&trapcom[i], a1);
										if (*a1)
											getsig(i);
										else
											ignsig(i);
									}
								}
							}
							else    /* print out current traps */
							{
								int     i;

								for (i = 0; i < MAXTRAP; i++)
								{
									if (trapcom[i])
									{
										prn_buff(i);
										prs_buff(colon);
										prs_buff(trapcom[i]);
										prc_buff(NL);
									}
								}
							}
							break;

						case SYSEXEC:
							com++;
							ioset = 0;
							io = NIL;
							if (a1 == NIL)
							{
								break;
							}
							if (command_state)
								command_state->recoverable = 0;
							/* FALLTHROUGH */

						case SYSLOGIN:
							if (command_state)
								command_state->recoverable = 0;
							oldsigs();
							execa(com, -1);
							done();

						case SYSNEWGRP:
							if (command_state)
								command_state->recoverable = 0;
							if (flags & rshflg)
								failed(com[0], restricted);
							else
							{
								flags |= forked;        /* force bad exec to terminate shell */
								oldsigs();
								execa(com, -1);
								done();
							}

						case SYSCD:
							if (flags & rshflg)
								failed(com[0], restricted);
							else if ((a1 && *a1) || (a1 == NIL && (a1 = homenod.namval)))
							{
								char *cdpath;
								char *dir;
								int f;

								if ((cdpath = cdpnod.namval) == NIL ||
								     *a1 == '/' ||
								     cf(a1, ".") == 0 ||
								     cf(a1, "..") == 0 ||
							     (*a1 == '.' &&
							     (a1[1] == '/' || (a1[1] == '.' && a1[2] == '/'))))
									cdpath = nullstr;

								do
								{
									dir = cdpath;
									cdpath = catpath(cdpath,a1);
								}
								while ((f = (chdir(curstak()) < 0)) && cdpath);

								if (f)
									failed(a1, baddir);
								else
								{
									cwd(curstak());
									if (cf(nullstr, dir) &&
									    *dir != ':' &&
										any('/', curstak()) &&
										flags & prompt)
									{
										prs_buff(curstak());
										prc_buff(NL);
									}
								}
								zapcd();
							}
							else
							{
								if (a1)
									failed(a1, baddir);
								else
									error(nohome);
							}

							break;

						case SYSSHFT:
							{
								int places;

								places = a1 ? stoi(a1) : 1;

								if ((dolc -= places) < 0)
								{
									dolc = 0;
									error(badshift);
								}
								else
									dolv += places;
							}

							break;

						case SYSWAIT:
							await(a1 ? stoi(a1) : -1, 1);
							break;

						case SYSREAD:
							rwait = 1;
							exitval = readvar(&com[1]);
							rwait = 0;
							break;

						case SYSSET:
							if (a1)
							{
								int     argc;

								argc = options(argn, com);
								if (argc > 1)
									setargs(com + argn - argc);
							}
							else if (comptr(t)->comset == NIL)
							{
								/*
								 * scan name chain and print
								 */
								namscan(printnam);
							}
							break;

						case SYSRDONLY:
							exitval = 0;
							if (a1)
							{
								while (*++com)
								{
									struct namnod *node = lookup(*com);

									command_attribute_changed(node, N_RDONLY);
									attrib(node, N_RDONLY);
								}
							}
							else
								namscan(printro);

							break;

						case SYSXPORT:
							{
								struct namnod   *n;

								exitval = 0;
								if (a1)
								{
									while (*++com)
									{
										n = lookup(*com);
										if (n->namflg & N_FUNCTN)
											error(badexport);
										else
									{
										command_attribute_changed(n, N_EXPORT);
											attrib(n, N_EXPORT);
									}
									}
								}
								else
									namscan(printexp);
							}
							break;

						case SYSEVAL:
							if (a1)
								execexp(a1, &com[2], errorflg);
							break;

#ifndef RES
#ifdef ACCOUNT
						case SYSULIMIT:
							{
								long int i;
								long ulimit();
								int command = 2;

								if (*a1 == '-')
								{       switch(a1[1])
									{
										case 'f':
											command = 2;
											break;

#ifdef rt
										case 'p':
											command = 5;
											break;

#endif

										default:
											error(badopt);
									}
									a1 = com[2];
								}
								if (a1)
								{
									int c;

									i = 0;
									while ((c = *a1++) >= '0' && c <= '9')
									{
										i = (i * 10) + (long)(c - '0');
										if (i < 0)
											error(badulimit);
									}
									if (c || i < 0)
										error(badulimit);
								}
								else
								{
									i = -1;
									command--;
								}

								if ((i = ulimit(command,i)) < 0)
									error(badulimit);

								if (command == 1 || command == 4)
								{
									prl(i);
									prc_buff('\n');
								}
								break;
							}
#endif
						case SYSUMASK:
							if (a1)
							{
								int c, i;

								i = 0;
								while ((c = *a1++) >= '0' && c <= '7')
									i = (i << 3) + c - '0';
								umask(i);
							}
							else
							{
								int i, j;

								umask(i = umask(0));
								prc_buff('0');
								for (j = 6; j >= 0; j -= 3)
									prc_buff(((i >> j) & 07) +'0');
								prc_buff(NL);
							}
							break;

#endif

						case SYSTST:
							exitval = test(argn, com);
							break;

						case SYSECHO:
							exitval = echo(argn, com);
							break;

						case SYSHASH:
							exitval = 0;

							if (a1)
							{
								if (a1[0] == '-')
								{
									if (a1[1] == 'r')
										zaphash();
									else
										error(badopt);
								}
								else
								{
									while (*++com)
									{
										if (hashtype(hash_cmd(*com)) == NOTFOUND)
											failed(*com, notfound);
									}
								}
							}
							else
								hashpr();

							break;

						case SYSPWD:
							{
								exitval = 0;
								cwdprint();
							}
							break;

						case SYSRETURN:
							if (funcnt == 0)
								error(badreturn);

							execbrk = 1;
							exitval = (a1 ? stoi(a1) : retval);
							break;

						case SYSTYPE:
							exitval = 0;
							if (a1)
							{
								while (*++com)
									what_is_path(*com);
							}
							break;

							case SYSGETOPTS:
								exitval = getopts_builtin(argn, com);
								break;

						case SYSUNS:
							exitval = 0;
							if (a1)
							{
								while (*++com)
									unset_name(*com);
							}
							break;

						default:
							prs_buff("unknown builtin\n");
						}


						flushb();
						restore(index);
						if (command_state)
							command_state->io = -1;
						if (command_catch)
						{
							command_context_end(command_state);
							command_state = NIL;
						}
						chktrap();
						break;
					}

					else if (comtype == FUNCTION)
					{
						struct namnod *n;
						short index;
						jmp_buf function_context;
						jmp_buf *previous_command_context;

						n = findnam(com[0]);

						funcnt++;
						if (command_state)
							command_state->io = topfd;
						index = initio(io, 1);
						setargs(com);
						previous_command_context = command_errshell;
						if (previous_command_context)
						{
							command_errshell = &function_context;
							if (setjmp(function_context))
							{
								command_errshell = previous_command_context;
								execbrk = 0;
								restore(index);
								funcnt--;
								longjmp(*previous_command_context, 1);
							}
						}
						execute((struct trenod *)(n->namenv), exec_link, errorflg, pf1, pf2);
						if (previous_command_context)
							command_errshell = previous_command_context;
						execbrk = 0;
						restore(index);
						funcnt--;

						break;
					}
				}
				else if (t->treio == NIL)
				{
					chktrap();
					break;
				}

			}
			/* FALLTHROUGH */

		case TFORK:
			exitval = 0;
			if (execflg && (treeflgs & (FAMP | FPOU)) == 0)
				parent = 0;
			else
			{
				int forkcnt = 1;

				if (treeflgs & (FAMP | FPOU))
				{
					link_iodocs(iotemp);
					linked = 1;
				}


				/*
				 * FORKLIM is the max period between forks -
				 * power of 2 usually.  Currently shell tries after
				 * 2,4,8,16, and 32 seconds and then quits
				 */

				while ((parent = fork()) == -1)
				{
					if ((forkcnt = (forkcnt * 2)) > FORKLIM)        /* 32 */
					{
						switch (errno)
						{
						case ENOMEM:
							error(noswap);
							break;
						default:
						case EAGAIN:
							error(nofork);
							break;
						}
					}
					sigchk();
					alarm(forkcnt);
					pause();
				}
			}
			if (parent)
			{
				/*
				 * This is the parent branch of fork;
				 * it may or may not wait for the child
				 */
				if (treeflgs & FPRS && flags & ttyflg)
				{
					prn(parent);
					newline();
				}
				if (treeflgs & FPCL)
					closepipe(pf1);
				if ((treeflgs & (FAMP | FPOU)) == 0)
					await(parent, 0);
				else if ((treeflgs & FAMP) == 0)
					post(parent);
				else
					assnum(&pcsadr, parent);
				if (command_state)
				{
					command_context_end(command_state);
					command_state = NIL;
				}
				chktrap();
				break;
			}
			else    /* this is the forked branch (child) of execute */
			{
				flags |= forked;
				fiotemp  = NIL;

				if (linked == 1)
				{
					swap_iodoc_nm(iotemp);
					exec_link |= 06;
				}
				else if (linked == 0)
					iotemp = NIL;

#ifdef ACCOUNT
				suspacct();
#endif

				postclr();
				settmp();
				/*
				 * Turn off INTR and QUIT if `FINT'
				 * Reset ramaining signals to parent
				 * except for those `lost' by trap
				 */
				oldsigs();
				if (treeflgs & FINT)
				{
					signal(SIGINT, SIG_IGN);
					signal(SIGQUIT, SIG_IGN);

#ifdef NICE
					nice(NICEVAL);
#endif

				}
				/*
				 * pipe in or out
				 */
				if (treeflgs & FPIN)
				{
					rename(pf1[INPIPE], 0);
					close(pf1[OTPIPE]);
				}
				if (treeflgs & FPOU)
				{
					close(pf2[INPIPE]);
					rename(pf2[OTPIPE], 1);
				}
				/*
				 * default std input for &
				 */
				if (treeflgs & FINT && ioset == 0)
					rename(chkopen(devnull), 0);
				/*
				 * io redirection
				 */
				initio(t->treio, 0);

				if (type != TCOM)
				{
					execute(forkptr(t)->forktre, exec_link | 01, errorflg);
				}
				else if (com[0] != ENDARGS)
				{
					eflag = 0;
					if (command_state)
						command_state->recoverable = 0;
					if (!command_state)
						setlist(comptr(t)->comset, N_EXPORT);
					rmtemp(NIL);
					execa_path(com, pos,
					    command_state ? command_state->path : NIL);
				}
				done();
			}

		case TPAR:
			execute(parptr(t)->partre, exec_link, errorflg);
			done();

		case TFIL:
			{
				int pv[2];

				chkpipe(pv);
				if (execute(lstptr(t)->lstlef, 0, errorflg, pf1, pv) == 0)
					execute(lstptr(t)->lstrit, exec_link, errorflg, pv, pf2);
				else
					closepipe(pv);
			}
			break;

		case TLST:
			execute(lstptr(t)->lstlef, 0, ERREXIT_CONTEXT(errorflg));
			execute(lstptr(t)->lstrit, exec_link,
			    ERREXIT_CONTEXT(errorflg));
			break;

		case TAND:
			if (execute(lstptr(t)->lstlef, 0, ERREXIT_SUPPRESSED) == 0)
				execute(lstptr(t)->lstrit, exec_link,
				    ERREXIT_CONTEXT(errorflg));
			break;

		case TORF:
			if (execute(lstptr(t)->lstlef, 0, ERREXIT_SUPPRESSED) != 0)
				execute(lstptr(t)->lstrit, exec_link,
				    ERREXIT_CONTEXT(errorflg));
			break;

		case TFOR:
			{
				struct namnod *n = lookup(forptr(t)->fornam);
				char    **args;
				struct dolnod *argsav = NIL;

				if (forptr(t)->forlst == NIL)
				{
					args = dolv + 1;
					argsav = useargs();
				}
				else
				{
					struct argnod *schain = gchain;

					gchain = NIL;
					trim((args = scan(getarg(forptr(t)->forlst)))[0]);
					gchain = schain;
				}
				loopcnt++;
				while (*args != ENDARGS && execbrk == 0)
				{
					assign(n, *args++);
					execute(forptr(t)->fortre, 0, errorflg);
					if (breakcnt < 0)
						execbrk = (++breakcnt != 0);
				}
				if (breakcnt > 0)
						execbrk = (--breakcnt != 0);

				loopcnt--;
				argfor = (struct dolnod *)freeargs(argsav);
			}
			break;

		case TWH:
		case TUN:
			{
				int     i = 0;

				loopcnt++;
				while (execbrk == 0 &&
				    (execute(whptr(t)->whtre, 0, ERREXIT_SUPPRESSED) == 0) ==
				    (type == TWH))
				{
					i = execute(whptr(t)->dotre, 0,
					    ERREXIT_CONTEXT(errorflg));
					if (breakcnt < 0)
						execbrk = (++breakcnt != 0);
				}
				if (breakcnt > 0)
						execbrk = (--breakcnt != 0);

				loopcnt--;
				exitval = i;
			}
			break;

		case TIF:
			if (execute(ifptr(t)->iftre, 0, ERREXIT_SUPPRESSED) == 0)
				execute(ifptr(t)->thtre, exec_link,
				    ERREXIT_CONTEXT(errorflg));
			else if (ifptr(t)->eltre)
				execute(ifptr(t)->eltre, exec_link,
				    ERREXIT_CONTEXT(errorflg));
			else
				exitval = 0;    /* force zero exit for if-then-fi */
			break;

		case TSW:
			{
				register char   *r = mactrim(swptr(t)->swarg);
				register struct regnod *regp;

				regp = swptr(t)->swlst;
				while (regp)
				{
					struct argnod *rex = regp->regptr;

					while (rex)
					{
						register char   *s;

						if (gmatch(r, s = macro(rex->argval)) || (trim(s), eq(r, s)))
						{
							execute(regp->regcom, 0, errorflg);
							regp = NIL;
							break;
						}
						else
							rex = rex->argnxt;
					}
					if (regp)
						regp = regp->regnxt;
				}
			}
			break;
		}
		exitset();
	}
	sigchk();
	tdystak(sav);
	flags |= eflag;
	return(exitval);
}

static struct trenod *
recoverable_parse()
{
	struct trenod   *tree;
	jmp_buf         parse_context;
	jmp_buf         *volatile previous_command_context;
	char            *parse_stack;
	struct ionod    *saved_iopend;
	int             saved_fndef;
	int             saved_nohash;
	int             saved_peekc;
	int             saved_peekn;

	previous_command_context = command_errshell;
	parse_stack = savstak();
	saved_fndef = fndef;
	saved_nohash = nohash;
	saved_iopend = iopend;
	saved_peekc = peekc;
	saved_peekn = peekn;
	parse_alloc_begin();
	command_errshell = &parse_context;
	if (setjmp(parse_context))
	{
		command_errshell = previous_command_context;
		fndef = saved_fndef;
		nohash = saved_nohash;
		iopend = saved_iopend;
		peekc = saved_peekc;
		peekn = saved_peekn;
		tdystak(parse_stack);
		parse_alloc_rollback();
		longjmp(*previous_command_context, 1);
	}
	tree = cmd(NL, NLFLG | MTFLG);
	command_errshell = previous_command_context;
	parse_alloc_commit();
	return(tree);
}

void
execexp(s, f, errorflg)
char    *s;
int     f;
int     errorflg;
{
	struct fileblk  fb;
	struct trenod   *tree;

	push(&fb);
	if (s)
	{
		estabf(s);
		fb.feval = (char **)(f);
	}
	else if (f >= 0)
		initf(f);
	if (command_errshell && command_recovery_allowed())
		tree = recoverable_parse();
	else
		tree = cmd(NL, NLFLG | MTFLG);
	execute(tree, 0, errorflg);
	pop();
}

static void
execprint(com)
	char **com;
{
	register int    argn = 0;

	prs(execpmsg);

	while(com[argn] != ENDARGS)
	{
		prs(com[argn++]);
		blank();
	}

	newline();
}
