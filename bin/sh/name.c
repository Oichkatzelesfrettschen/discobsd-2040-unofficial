/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

extern BOOL	chkid();
extern char	*simple();
extern int	mailchk;
static void	setname();

struct namnod ps2nod =
{
	(struct namnod *)NIL,
	&acctnod,
	ps2name, NIL, NIL, N_DEFAULT
};
struct namnod cdpnod =
{
	(struct namnod *)NIL,
	(struct namnod *)NIL,
	cdpname, NIL, NIL, N_DEFAULT
};
struct namnod pathnod =
{
	&mailpnod,
	(struct namnod *)NIL,
	pathname, NIL, NIL, N_DEFAULT
};
struct namnod ifsnod =
{
	&homenod,
	&mailnod,
	ifsname, NIL, NIL, N_DEFAULT
};
struct namnod ps1nod =
{
	&pathnod,
	&ps2nod,
	ps1name, NIL, NIL, N_DEFAULT
};
struct namnod homenod =
{
	&cdpnod,
	(struct namnod *)NIL,
	homename, NIL, NIL, N_DEFAULT
};
struct namnod mailnod =
{
	(struct namnod *)NIL,
	(struct namnod *)NIL,
	mailname, NIL, NIL, N_DEFAULT
};
struct namnod mchknod =
{
	&ifsnod,
	&ps1nod,
	mchkname, NIL, NIL, N_DEFAULT
};
struct namnod acctnod =
{
	(struct namnod *)NIL,
	(struct namnod *)NIL,
	acctname, NIL, NIL, N_DEFAULT
};
struct namnod mailpnod =
{
	(struct namnod *)NIL,
	(struct namnod *)NIL,
	mailpname, NIL, NIL, N_DEFAULT
};


struct namnod *namep = &mchknod;

/* ========	variable and string handling	======== */

int
syslook(w, syswds, n)
	register char *w;
	register struct sysnod syswds[];
	int n;
{
	int	low;
	int	high;
	int	mid;
	register int cond;

	if (w == NIL || *w == 0)
		return(0);

	low = 0;
	high = n - 1;

	while (low <= high)
	{
		mid = (low + high) / 2;

		if ((cond = cf(w, syswds[mid].sysnam)) < 0)
			high = mid - 1;
		else if (cond > 0)
			low = mid + 1;
		else
			return(syswds[mid].sysval);
	}
	return(0);
}

void
setlist(arg, xp)
register struct argnod *arg;
int	xp;
{
	if (flags & exportflg)
		xp |= N_EXPORT;

	while (arg)
	{
		register char *s = mactrim(arg->argval);
		setname(s, xp);
		arg = arg->argnxt;
		if (flags & execpr)
		{
			prs(s);
			if (arg)
				blank();
			else
				newline();
		}
	}
}

struct namnod_save
{
	struct namnod_save *next;
	struct namnod *node;
	char *value;
	char *environment;
	int flags;
	int persistent_flags;
};

struct command_assignment
{
	struct command_assignment *next;
	char *text;
};

struct command_env
{
	struct command_env *previous;
	struct namnod_save *saved;
	struct command_assignment *assignments;
	int applying;
	int mutated;
};

static struct command_env *command_active;
static int command_applying;

static struct namnod_save *
command_saved_node(saved, node)
	struct namnod_save *saved;
	struct namnod *node;
{
	while (saved && saved->node != node)
		saved = saved->next;
	return(saved);
}

void
command_name_changed(node)
	struct namnod *node;
{
	struct command_env *environment;
	struct namnod_save *entry;

	if (command_applying)
		return;
	for (environment = command_active; environment;
	    environment = environment->previous)
	{
		for (entry = environment->saved; entry; entry = entry->next)
		{
			if (entry->node == node && entry->flags >= 0)
				entry->flags = ~entry->flags;
		}
	}
}

void
command_attribute_changed(node, attributes)
	struct namnod *node;
	int attributes;
{
	struct command_env *environment;
	struct namnod_save *entry;

	for (environment = command_active; environment;
	    environment = environment->previous)
	{
		for (entry = environment->saved; entry; entry = entry->next)
		{
			if (entry->node == node && entry->flags >= 0)
				entry->persistent_flags |= attributes;
		}
	}
}

struct command_env *
command_beginlist()
{
	struct command_env *environment;

	environment = (struct command_env *)alloc(sizeof *environment);
	environment->previous = NIL;
	environment->saved = NIL;
	environment->assignments = NIL;
	environment->applying = 0;
	environment->mutated = 0;
	return(environment);
}

void
command_setlist(environment, arg)
	struct command_env *environment;
	register struct argnod *arg;
{
	struct command_assignment *assignment_entry;
	struct command_assignment **assignment_tail;
	struct namnod_save *entry;
	struct namnod *node;
	char *assignment;
	char *separator;

	assignment_tail = &environment->assignments;
	while (arg)
	{
		assignment = mactrim(arg->argval);
		separator = assignment;
		while (*separator && *separator != '=')
			separator++;
		*separator = 0;
		node = lookup(assignment);
		*separator = '=';
		if (node->namflg & N_RDONLY)
			failed(node->namid, wtfailed);
#ifndef RES
		if ((flags & rshflg) &&
		    (node == &pathnod || eq(node->namid, "SHELL")))
			failed(node->namid, restricted);
#endif
		assignment_entry = (struct command_assignment *)
		    alloc(sizeof *assignment_entry);
		assignment_entry->next = NIL;
		assignment_entry->text = assignment;
		*assignment_tail = assignment_entry;
		assignment_tail = &assignment_entry->next;
		if (command_saved_node(environment->saved, node) == NIL)
		{
			entry = (struct namnod_save *)alloc(sizeof *entry);
			entry->node = node;
			entry->flags = node->namflg;
			entry->persistent_flags = 0;
			entry->value = node->namval;
			entry->environment = node->namenv;
			entry->next = environment->saved;
			environment->saved = entry;
		}
		arg = arg->argnxt;
	}

	environment->mutated = 1;
	environment->applying = 1;
	command_applying++;
	for (entry = environment->saved; entry; entry = entry->next)
	{
		if (entry->flags & N_FUNCTN)
			func_unhash(entry->node->namid);
		entry->node->namval = NIL;
		entry->node->namenv = NIL;
		entry->node->namflg = N_EXPORT;
	}
	for (assignment_entry = environment->assignments; assignment_entry;
	    assignment_entry = assignment_entry->next)
		setname(assignment_entry->text, 0);
	command_applying--;
	environment->applying = 0;
	environment->previous = command_active;
	command_active = environment;
}

void
command_restorelist(environment)
	struct command_env *environment;
{
	struct command_assignment *assignment;
	struct command_assignment *next_assignment;
	struct namnod_save *saved;
	struct namnod_save *next;
	struct namnod *node;

	if (environment->applying)
	{
		command_applying--;
		environment->applying = 0;
	}
	if (command_active == environment)
		command_active = environment->previous;

	saved = environment->saved;
	while (saved)
	{
		node = saved->node;
		if (!environment->mutated)
			;
		else if (saved->flags < 0)
		{
			if ((~saved->flags) & N_FUNCTN)
				freetree((struct trenod *)saved->environment);
			else
			{
				free(saved->value);
				free(saved->environment);
			}
		}
		else
		{
			if (node->namflg & N_FUNCTN)
			{
				func_unhash(node->namid);
				freefunc(node);
			}
			else if (node->namval == node->namenv)
				free(node->namval);
			else
			{
				free(node->namval);
				free(node->namenv);
			}
			node->namval = saved->value;
			node->namenv = saved->environment;
			node->namflg = saved->flags | saved->persistent_flags;
			if (node->namflg & N_FUNCTN)
				hash_func(node->namid);
			if (node == &mchknod)
				mailchk = stoi(node->namval);
			if (node == &pathnod)
			{
				zaphash();
				set_dotpath();
			}
		}
		next = saved->next;
		free(saved);
		saved = next;
	}
	assignment = environment->assignments;
	while (assignment)
	{
		next_assignment = assignment->next;
		free(assignment);
		assignment = next_assignment;
	}
	free(environment);
}


static void
setname(argi, xp)	/* does parameter assignments */
char	*argi;
int	xp;
{
	register char *argscan = argi;
	register struct namnod *n;

	if (letter(*argscan))
	{
		while (alphanum(*argscan))
			argscan++;

		if (*argscan == '=')
		{
			*argscan = 0;	/* make name a cohesive string */

			n = lookup(argi);
			*argscan++ = '=';
			attrib(n, xp);
			if (xp & N_ENVNAM)
				n->namenv = n->namval = argscan;
			else
				assign(n, argscan);
			return;
		}
	}
	failed(argi, notid);
}

void
replace(a, v)
register char	**a;
char	*v;
{
	free(*a);
	*a = make(v);
}

void
dfault(n, v)
struct namnod *n;
char	*v;
{
	if (n->namval == NIL)
		assign(n, v);
}

void
assign(n, v)
struct namnod *n;
char	*v;
{
	if (n->namflg & N_RDONLY)
		failed(n->namid, wtfailed);

#ifndef RES

	else if (flags & rshflg)
	{
		if (n == &pathnod || eq(n->namid, "SHELL"))
			failed(n->namid, restricted);
	}

#endif

	else if (n->namflg & N_FUNCTN)
	{
		func_unhash(n->namid);
		freefunc(n);

		n->namenv = NIL;
		n->namflg = N_DEFAULT;
	}
	command_name_changed(n);

	if (n == &mchknod)
	{
		mailchk = stoi(v);
	}

	replace(&n->namval, v);
	attrib(n, N_ENVCHG);
	if (eq(n->namid, "OPTIND"))
		getoptsreset(v);

	if (n == &pathnod)
	{
		zaphash();
		set_dotpath();
		return;
	}

	if (flags & prompt)
	{
		if ((n == &mailpnod) || (n == &mailnod && mailpnod.namflg == N_DEFAULT))
			setmail(n->namval);
	}
}

int
readvar(names)
char	**names;
{
	struct fileblk	fb;
	register struct fileblk *f = &fb;
	register char	c;
	register int	rc = 0;
	struct namnod *n = lookup(*names++);	/* done now to avoid storage mess */
	char	*rel = (char *)relstak();

	push(f);
	initf(dup(0));

	if (lseek(0, 0L, 1) == -1)
		f->fsiz = 1;

	/*
	 * strip leading IFS characters
	 */
	while ((any((c = nextc(0)), ifsnod.namval)) && !(eolchar(c)))
			;
	for (;;)
	{
		if ((*names && any(c, ifsnod.namval)) || eolchar(c))
		{
			zerostak();
			assign(n, absstak(rel));
			setstak(rel);
			if (*names)
				n = lookup(*names++);
			else
				n = NIL;
			if (eolchar(c))
			{
				break;
			}
			else		/* strip imbedded IFS characters */
			{
				while ((any((c = nextc(0)), ifsnod.namval)) &&
					!(eolchar(c)))
					;
			}
		}
		else
		{
			pushstak(c);
			c = nextc(0);

			if (eolchar(c))
			{
				char *top = staktop;

				while (any(*(--top), ifsnod.namval))
					;
				staktop = top + 1;
			}


		}
	}
	while (n)
	{
		assign(n, nullstr);
		if (*names)
			n = lookup(*names++);
		else
			n = NIL;
	}

	if (eof)
		rc = 1;
	lseek(0, (long)(f->fnxt - f->fend), 1);
	pop();
	return(rc);
}

void
assnum(p, i)
char	**p;
int	i;
{
	itos(i);
	replace(p, numbuf);
}

char *
make(v)
char	*v;
{
	register char	*p;

	if (v)
	{
		movstr(v, p = alloc(length(v)));
		return(p);
	}
	else
		return(NIL);
}


struct namnod *
lookup(nam)
	register char	*nam;
{
	register struct namnod *nscan = namep;
	register struct namnod **prev = &namep;
	int		LR;

	if (!chkid(nam))
		failed(nam, notid);
	while (nscan)
	{
		if ((LR = cf(nam, nscan->namid)) == 0)
			return(nscan);

		else if (LR < 0)
			prev = &(nscan->namlft);
		else
			prev = &(nscan->namrgt);
		nscan = *prev;
	}
	/*
	 * add name node
	 */
	nscan = (struct namnod *)alloc(sizeof *nscan);
	nscan->namlft = nscan->namrgt = (struct namnod *)NIL;
	nscan->namid = make(nam);
	nscan->namval = NIL;
	nscan->namflg = N_DEFAULT;
	nscan->namenv = NIL;

	return(*prev = nscan);
}

BOOL
chkid(nam)
char	*nam;
{
	register char *cp = nam;

	if (!letter(*cp))
		return(FALSE);
	else
	{
		while (*++cp)
		{
			if (!alphanum(*cp))
				return(FALSE);
		}
	}
	return(TRUE);
}

static void (*namfn)();

static void
namwalk(np)
register struct namnod *np;
{
	if (np)
	{
		namwalk(np->namlft);
		(*namfn)(np);
		namwalk(np->namrgt);
	}
}

void
namscan(fn)
	void	(*fn)();
{
	namfn = fn;
	namwalk(namep);
}

void
printnam(n)
struct namnod *n;
{
	register char	*s;

	sigchk();

	if (n->namflg & N_FUNCTN)
	{
		prs_buff(n->namid);
		prs_buff("(){\n");
		prf(n->namenv);
		prs_buff("\n}\n");
	}
	else if ((s = n->namval))
	{
		prs_buff(n->namid);
		prc_buff('=');
		prs_buff(s);
		prc_buff(NL);
	}
}

static char *
staknam(n)
register struct namnod *n;
{
	register char	*p;

	p = movstr(n->namid, staktop);
	p = movstr("=", p);
	p = movstr(n->namval, p);
	return(getstak(p + 1 - (char *)(stakbot)));
}

static int namec;

void
exname(n)
	register struct namnod *n;
{
	register int 	flg = n->namflg;

	if (flg & N_ENVCHG)
	{

		if (flg & N_EXPORT)
		{
			free(n->namenv);
			n->namenv = make(n->namval);
		}
		else
		{
			free(n->namval);
			n->namval = make(n->namenv);
		}
	}


	if (!(flg & N_FUNCTN))
		n->namflg = N_DEFAULT;

	if (n->namval)
		namec++;

}

void
printro(n)
register struct namnod *n;
{
	if (n->namflg & N_RDONLY)
	{
		prs_buff(readonly);
		prc_buff(SP);
		prs_buff(n->namid);
		prc_buff(NL);
	}
}

void
printexp(n)
register struct namnod *n;
{
	if (n->namflg & N_EXPORT)
	{
		prs_buff(export);
		prc_buff(SP);
		prs_buff(n->namid);
		prc_buff(NL);
	}
}

void
setup_env()
{
	register char **e = environ;

	while (*e)
		setname(*e++, N_ENVNAM);
}


static char **argnam;

void
pushnam(n)
struct namnod *n;
{
	if (n->namval)
		*argnam++ = staknam(n);
}

char **
setenvv()
{
	register char	**er;

	namec = 0;
	namscan(exname);

	argnam = er = (char **)getstak(namec * BYTESPERWORD + BYTESPERWORD);
	namscan(pushnam);
	*argnam++ = NIL;
	return(er);
}

struct namnod *
findnam(nam)
	register char	*nam;
{
	register struct namnod *nscan = namep;
	int		LR;

	if (!chkid(nam))
		return(NIL);
	while (nscan)
	{
		if ((LR = cf(nam, nscan->namid)) == 0)
			return(nscan);
		else if (LR < 0)
			nscan = nscan->namlft;
		else
			nscan = nscan->namrgt;
	}
	return(NIL);
}


void
unset_name(name)
	register char 	*name;
{
	register struct namnod	*n;

	if ((n = findnam(name)))
	{
		if (n->namflg & N_RDONLY)
			failed(name, wtfailed);

		if (n == &pathnod ||
		    n == &ifsnod ||
		    n == &ps1nod ||
		    n == &ps2nod ||
		    n == &mchknod)
		{
			failed(name, badunset);
		}

#ifndef RES

		if ((flags & rshflg) && eq(name, "SHELL"))
			failed(name, restricted);

#endif
		command_name_changed(n);

		if (n->namflg & N_FUNCTN)
		{
			func_unhash(name);
			freefunc(n);
		}
		else
		{
			free(n->namval);
			free(n->namenv);
		}

		n->namval = n->namenv = NIL;
		n->namflg = N_DEFAULT;

		if (flags & prompt)
		{
			if (n == &mailpnod)
				setmail(mailnod.namval);
			else if (n == &mailnod && mailpnod.namflg == N_DEFAULT)
				setmail(NIL);
		}
	}
}
