/*	awk.lx.l	4.3	84/12/08	*/

%Start A str chc sc reg comment

%{
#include	"awk.h"
#include	"awk.def.h"
#define YY_NO_INPUT	/* defeat lex */
/*
 * The rules live under the A start condition. flex starts in INITIAL,
 * where nothing matches and the default rule echoes the program text to
 * the output, so enter A before the first token.
 */
#define YY_USER_INIT	BEGIN A;
extern int	yylval;
extern int	mustfld;
extern char	*lexprog;

/*
 * flex reads through YY_INPUT, and defaults it to yyin, which it sets to
 * stdin when NULL: the program text on the command line was never read
 * and the program came from the input instead. Feed it from lexprog when
 * main sets that, the way input() below does for AT&T lex.
 */
#define YY_INPUT(buf, result, max_size) \
	{ \
		int yyc; \
		if (lexprog != NULL) \
			yyc = *lexprog ? (unsigned char)*lexprog++ : EOF; \
		else \
			yyc = getc(yyin); \
		if (yyc == '\n') \
			yylineno++; \
		result = (yyc == EOF) ? YY_NULL : ((buf)[0] = yyc, 1); \
	}

int	lineno	= 1;
#define	CADD	cbuf[clen++]=yytext[0]; if(clen>=CBUFLEN-1) {yyerror("string too long", cbuf); BEGIN A;}
#define	CBUFLEN	150
char	cbuf[CBUFLEN];
int	clen, cflag;
%}

A	[a-zA-Z_]
B	[a-zA-Z0-9_]
D	[0-9]
WS	[ \t]

%%
        /* witchcraft */
        /* switch (yybgin-yysvec-1) {
	case 0:
		BEGIN A;
		break;
	case sc:
		BEGIN A;
		return('}');
	} */

	/*
	 * The commented switch above was AT&T lex's entry hook: a "}"
	 * returned ';' and moved to state sc, and the next call returned the
	 * '}' itself. flex has no entry hook, so sc does it with rules: the
	 * next character, or the end of input, yields the '}' and returns to
	 * A without consuming anything.
	 */
<sc>.|\n	{ yyless(0); BEGIN A; return('}'); }
<sc><<EOF>>	{ BEGIN A; return('}'); }

<A>^\n		lineno++;
<A>^{WS}*#.*\n	lineno++;	/* strip comment lines */
<A>{WS}		;
<A,reg>"\\"\n	lineno++;
<A>"||"		return(BOR);
<A>BEGIN	return(XBEGIN);
<A>END		return(XEND);
<A>PROGEND	return(EOF);
<A>"&&"		return(AND);
<A>"!"		return(NOT);
<A>"!="		{ yylval = NE; return(RELOP); }
<A>"~"		{ yylval = MATCH; return(MATCHOP); }
<A>"!~"		{ yylval = NOTMATCH; return(MATCHOP); }
<A>"<"		{ yylval = LT; return(RELOP); }
<A>"<="		{ yylval = LE; return(RELOP); }
<A>"=="		{ yylval = EQ; return(RELOP); }
<A>">="		{ yylval = GE; return(RELOP); }
<A>">"		{ yylval = GT; return(RELOP); }
<A>">>"		{ yylval = APPEND; return(RELOP); }
<A>"++"		{ yylval = INCR; return(INCR); }
<A>"--"		{ yylval = DECR; return(DECR); }
<A>"+="		{ yylval = ADDEQ; return(ASGNOP); }
<A>"-="		{ yylval = SUBEQ; return(ASGNOP); }
<A>"*="		{ yylval = MULTEQ; return(ASGNOP); }
<A>"/="		{ yylval = DIVEQ; return(ASGNOP); }
<A>"%="		{ yylval = MODEQ; return(ASGNOP); }
<A>"="		{ yylval = ASSIGN; return(ASGNOP); }

<A>"$"{D}+	{	if (atoi(yytext+1)==0) {
				yylval = (hack)lookup("$record", symtab, 0);
				return(STRING);
			} else {
				yylval = fieldadr(atoi(yytext+1));
				return(FIELD);
			}
		}
<A>"$"{WS}*	{ return(INDIRECT); }
<A>NF		{ mustfld=1; yylval = (hack)setsymtab(yytext, EMPTY, 0.0, NUM, symtab); return(VAR); }
<A>({D}+("."?){D}*|"."{D}+)((e|E)("+"|-)?{D}+)?	{
		yylval = (hack)setsymtab(yytext, EMPTY, atof(yytext), CON|NUM, symtab); return(NUMBER); }
<A>"}"{WS}*\n	{ BEGIN sc; lineno++; return(';'); }
<A>"}"		{ BEGIN sc; return(';'); }
<A>;\n		{ lineno++; return(';'); }
<A>\n		{ lineno++; return(NL); }
<A>while	return(WHILE);
<A>for		return(FOR);
<A>if		return(IF);
<A>else		return(ELSE);
<A>next		return(NEXT);
<A>exit		return(EXIT);
<A>break	return(BREAK);
<A>continue	return(CONTINUE);
<A>print	{ yylval = PRINT; return(PRINT); }
<A>printf	{ yylval = PRINTF; return(PRINTF); }
<A>sprintf	{ yylval = SPRINTF; return(SPRINTF); }
<A>split	{ yylval = SPLIT; return(SPLIT); }
<A>substr	return(SUBSTR);
<A>index	return(INDEX);
<A>in		return(IN);
<A>getline	return(GETLINE);
<A>length	{ yylval = FLENGTH; return(FNCN); }
<A>log		{ yylval = FLOG; return(FNCN); }
<A>int		{ yylval = FINT; return(FNCN); }
<A>exp		{ yylval = FEXP; return(FNCN); }
<A>sqrt		{ yylval = FSQRT; return(FNCN); }
<A>{A}{B}*	{ yylval = (hack)setsymtab(yytext, tostring(""), 0.0, STR|NUM, symtab); return(VAR); }
<A>\"		{ BEGIN str; clen=0; }

<A>#		{ BEGIN comment; }
<comment>\n	{ BEGIN A; lineno++; return(NL); }
<comment>.	;

<A>.		{ yylval = yytext[0]; return(yytext[0]); }

<reg>"["	{ BEGIN chc; clen=0; cflag=0; }
<reg>"[^"	{ BEGIN chc; clen=0; cflag=1; }

<reg>"?"	return(QUEST);
<reg>"+"	return(PLUS);
<reg>"*"	return(STAR);
<reg>"|"	return(OR);
<reg>"."	return(DOT);
<reg>"("	return('(');
<reg>")"	return(')');
<reg>"^"	return('^');
<reg>"$"	return('$');
<reg>\\{D}{D}{D}	{ sscanf(yytext+1, "%o", &yylval); return(CHAR); }
<reg>\\.	{	if (yytext[1]=='n') yylval = '\n';
			else if (yytext[1] == 't') yylval = '\t';
			else yylval = yytext[1];
			return(CHAR);
		}
<reg>"/"	{ BEGIN A; unput('/'); }
<reg>\n		{ yyerror("newline in regular expression"); lineno++; BEGIN A; }
<reg>.		{ yylval = yytext[0]; return(CHAR); }

<str>\"		{ char *s; BEGIN A; cbuf[clen]=0; s = tostring(cbuf);
		cbuf[clen] = ' '; cbuf[++clen] = 0;
		yylval = (hack)setsymtab(cbuf, s, 0.0, CON|STR, symtab); return(STRING); }
<str>\n		{ yyerror("newline in string"); lineno++; BEGIN A; }
<str>"\\\""	{ cbuf[clen++]='"'; }
<str,chc>"\\"n	{ cbuf[clen++]='\n'; }
<str,chc>"\\"t	{ cbuf[clen++]='\t'; }
<str,chc>"\\\\"	{ cbuf[clen++]='\\'; }
<str>.		{ CADD; }

<chc>"\\""]"	{ cbuf[clen++]=']'; }
<chc>"]"	{ BEGIN reg; cbuf[clen]=0; yylval = (hack)tostring(cbuf);
		if (cflag==0) { return(CCL); }
		else { return(NCCL); } }
<chc>\n		{ yyerror("newline in character class"); lineno++; BEGIN A; }
<chc>.		{ CADD; }

%%

input()
{
	register c;
	extern char *lexprog;

	if (yyin == NULL)
		c = *lexprog++;
	else
		c = getc(yyin);
	if (c == '\n')
		yylineno++;
	else if (c == EOF)
		c = 0;
	return(c);
}

startreg()
{
	BEGIN reg;
}
