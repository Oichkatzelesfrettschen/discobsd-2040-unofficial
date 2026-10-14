/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"
#include "sym.h"

/* ========     character handling for command lines    ========*/

static int nextc_quoted;
#define PEEKQUOTE 0x200

word()
{
	register char   c, d;
	struct argnod   *arg = (struct argnod *)locstak();
	register char   *argp = arg->argval;
	int             alpha = 1;
	int             quote_seen = FALSE;

	wdnum = 0;
	wdset = 0;

	while (1)
	{
		do {
			c = nextc(0);
			if (nextc_quoted)
				quote_seen = TRUE;
		} while (space(c));

		if (c == COMCHAR)
		{
			while ((c = readc()) != NL && c != EOF);
			peekc = c;
		}
		else
		{
			break;  /* out of comment - white space loop */
		}
	}
	if (!eofmeta(c))
	{
		do
		{
			if (c == LITERAL)
			{
				quote_seen = TRUE;
				*argp++ = (DQUOTE);
				while ((c = readc()) && c != LITERAL)
				{
					/* @@@ *argp++ = (c | QUOTE); */
					*argp++ = qmask(c);

					if (c == NL)
						chkpr();
				}
				*argp++ = (DQUOTE);
			}
			else
			{
				*argp++ = (c);
				if (c == '=')
					wdset |= alpha;
				if (!alphanum(c))
					alpha = 0;
				if (qotchar(c))
				{
					quote_seen = TRUE;
					d = c;
					while ((*argp++ = (c = nextc(d))) && c != d)
					{
						if (c == NL)
							chkpr();
					}
				}
			}
			c = nextc(0);
			if (nextc_quoted)
				quote_seen = TRUE;
		} while (!eofmeta(c));
		argp = endstak(argp);
		if (!letter(arg->argval[0]))
			wdset = 0;

		peekn = c | MARK;
		if (arg->argval[1] == 0 &&
		    (d = arg->argval[0], digit(d)) &&
		    (c == '>' || c == '<'))
		{
			word();
			wdnum = d - '0';
		}
		else            /*check for reserved words*/
		{
			if (reserv == FALSE || (wdval = syslook(arg->argval,reserved, no_reserved)) == 0)
			{
				wdarg = arg;
				wdval = 0;
			}
		}
	}
	else if (dipchar(c))
	{
		if ((d = nextc(0)) == c)
		{
			wdval = c | SYMREP;
			if (c == '<')
			{
				if ((d = nextc(0)) == '-')
					wdnum |= IOSTRIP;
				else
					peekn = d | MARK |
					    (nextc_quoted ? PEEKQUOTE : 0);
			}
		}
		else
		{
			peekn = d | MARK;
			wdval = c;
		}
	}
	else
	{
		if ((wdval = c) == EOF)
			wdval = EOFSYM;
		if (iopend && eolchar(c))
		{
			copy(iopend);
			iopend = 0;
		}
	}
	reserv = FALSE;
	wdquote = quote_seen;
	return(wdval);
}

skipc()
{
	register char c;

	while (c = nextc(0), space(c))
		;
	return(c);
}

nextc(quote)
char    quote;
{
	register char   c, d;

retry:
	nextc_quoted = (peekn & PEEKQUOTE) != 0;
	if ((d = readc()) == ESCAPE)
	{
		if ((c = readc()) == NL)
		{
			chkpr();
			goto retry;
		}
		else if (quote && c != quote && !escchar(c))
			peekc = c | MARK;
		else
		{
			nextc_quoted = TRUE;
			/* @@@ d = c | QUOTE; */
			d = qmask(c);
		}
	}
	return(d);
}

readc()
{
	register char   c;
	register int    len;
	register struct fileblk *f;

	if (peekn)
	{
		peekc = peekn;
		peekn = 0;
	}
	if (peekc)
	{
		c = peekc;
		peekc = 0;
		return(c);
	}
	f = standin;
retry:
	if (f->fnxt != f->fend)
	{
		if ((c = *f->fnxt++) == 0)
		{
			if (f->feval)
			{
				if (estabf(*f->feval++))
					c = EOF;
				else
					c = SP;
			}
			else
				goto retry;     /* = c = readc(); */
		}
		if (flags & readpr && standin->fstak == NIL)
			prc(c);
		if (c == NL)
			f->flin++;
	}
	else if (f->feof || f->fdes < 0)
	{
		c = EOF;
		f->feof++;
	}
	else if ((len = readb()) <= 0)
	{
		close(f->fdes);
		f->fdes = -1;
		c = EOF;
		f->feof++;
	}
	else
	{
		f->fend = (f->fnxt = f->fbuf) + len;
		goto retry;
	}
	return(c);
}

readb()
{
	register struct fileblk *f = standin;
	register int    len;

	do
	{
		if (trapnote & SIGSET)
		{
			newline();
			sigchk();
		}
		else if ((trapnote & TRAPSET) && (rwait > 0))
		{
			newline();
			chktrap();
			clearup();
		}

	       len = read(f->fdes, f->fbuf, f->fsiz);

	} while (  len < 0 && trapnote );

	return(len);
}
