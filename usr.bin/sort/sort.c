#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define L   1024
#define N   7
#define C   20
#ifndef MEM
#define MEM (32*2048)
#endif
#define NF  10

#define rline(mp)   (fgets((mp)->l, L, (mp)->b) == NULL)

FILE    *is, *os;
char    *dirtry[] = {_PATH_USRTMP, _PATH_TMP, NULL};
char    **dirs;
char    *file;
char    *filep;
int nfiles;
unsigned    nlines;
unsigned    ntext;
int *lspace;
char    *tspace;
int     mflg;
int cflg;
int uflg;
char    *outfil;
int unsafeout;  /*kludge to assure -m -o works*/
char    tabchar;
int     eargc;
char    **eargv;
#ifdef SORT_HOST_TEST
static char sort_host_arena[MEM];
#endif

#define CODE_IDENTITY 0
#define CODE_FOLD     1

#define IGNORE_NONE       0
#define IGNORE_NONPRINT   1
#define IGNORE_DICTIONARY 2
struct  field {
    unsigned char code;
    unsigned char ignore;
    int nflg;
    int rflg;
    int bflg[2];
    int m[2];
    int n[2];
}   fields[NF];
struct field proto = {
    CODE_IDENTITY,
    IGNORE_NONE,
    0,
    1,
    0,0,
    0,-1,
    0,0
};
int nfields;
int     error = 1;
char    *setfil();

#define blank(c)    ((c) == ' ' || (c) == '\t')
#define ascii_digit(c) ((unsigned char)(c) >= '0' && \
    (unsigned char)(c) <= '9')

static int sort_code(unsigned char, unsigned char);
static int sort_ignored(unsigned char, unsigned char);

void     sort();
void     merge(int, int);
void     disorder(char *, char *);
void     newfile();
char    *setfil(int);
void     oldfile();
void     safeoutfil();
void     cant(char *);
void     diag(char *, char *);
void     term(int);
int      cmp(char *, char *);
int      cmpa(char *, char *);
char    *skip(char *, struct field *, int);
char    *eol(char *);
void     copyproto();
void     field(char *, int);
int      number(char **);
void     qusort(char **, char **);

int (*compare)() = cmpa;

int
main(argc, argv)
int argc;
char **argv;
{
    register int a;
#ifndef SORT_HOST_TEST
    extern char end[1];
#endif
    char *ep;
    char *arg;
    struct field *p, *q;
    int i;
    size_t file_capacity;

    copyproto();
    eargv = argv;
    while (--argc > 0) {
        if(**++argv == '-') for(arg = *argv;;) {
            switch(*++arg) {
            case '\0':
                if(arg[-1] == '-')
                    eargv[eargc++] = "-";
                break;

            case 'o':
                if(--argc > 0)
                    outfil = *++argv;
                continue;

            case 'T':
                if (--argc > 0)
                    dirtry[0] = *++argv;
                continue;

            default:
                field(++*argv,nfields>0);
                break;
            }
            break;
        } else if (**argv == '+') {
            if(++nfields>=NF) {
                diag("too many keys","");
                exit(1);
            }
            copyproto();
            field(++*argv,0);
        } else
            eargv[eargc++] = *argv;
    }
    q = &fields[0];
    for(a=1; a<=nfields; a++) {
        p = &fields[a];
        if(p->code != proto.code) continue;
        if(p->ignore != proto.ignore) continue;
        if(p->nflg != proto.nflg) continue;
        if(p->rflg != proto.rflg) continue;
        if(p->bflg[0] != proto.bflg[0]) continue;
        if(p->bflg[1] != proto.bflg[1]) continue;
        p->code = q->code;
        p->ignore = q->ignore;
        p->nflg = q->nflg;
        p->rflg = q->rflg;
        p->bflg[0] = p->bflg[1] = q->bflg[0];
    }
    if(eargc == 0)
        eargv[eargc++] = "-";
    if(cflg && eargc>1) {
        diag("can check only 1 file","");
        exit(1);
    }
    safeoutfil();

#ifdef SORT_HOST_TEST
    lspace = (int *)sort_host_arena;
    ep = (char *)lspace + MEM;
#else
    ep = end + MEM;
    lspace = (int *)sbrk(0);
    while((int)brk(ep) == -1)
        ep -= 512;
    brk(ep -= 512); /* for recursion */
#endif
    a = ep - (char*)lspace;
    nlines = (a-L);
    nlines /= 5 * sizeof(char *);
    ntext = nlines * 4 * sizeof(char *);
    tspace = (char *)((char **)lspace + nlines);
    a = -1;
    for(dirs=dirtry; *dirs; dirs++) {
        file_capacity = strlen(*dirs) + 26;
        file = malloc(file_capacity);
        if(file == NULL) {
            diag("can't allocate temp name", "");
            exit(1);
        }
        sprintf(file, "%s/stm%05u.", *dirs, (unsigned)getpid());
        filep = file;
        while (*filep)
            filep++;
        strcpy(filep, "probe");
        if ( (a=creat(file, 0600)) >=0)
            break;
        free(file);
        file = NULL;
    }
    if(a < 0) {
        diag("can't locate temp","");
        exit(1);
    }
    close(a);
    unlink(file);
    if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
        signal(SIGHUP, term);
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, term);
    signal(SIGPIPE, term);
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
        signal(SIGTERM, term);
    nfiles = eargc;
    if(!mflg && !cflg) {
        sort();
        fclose(stdin);
    }
    for(a = mflg|cflg?0:eargc; a+N<nfiles || unsafeout&&a<eargc; a=i) {
        i = a+N;
        if(i>=nfiles)
            i = nfiles;
        newfile();
        merge(a, i);
    }
    if(a != nfiles) {
        oldfile();
        merge(a, nfiles);
    }
    error = 0;
    term(0);
}

void
sort()
{
    register char *cp;
    register char **lp;
    register int lines, text, len;
    int done = 0;
    int i = 0;
    char *f;
    char c;

    if((f = setfil(i++)) == NULL)
        is = stdin;
    else if((is = fopen(f, "r")) == NULL)
        cant(f);

    do {
        cp = tspace;
        lp = (char **)lspace;
        lines = nlines;
        text = ntext;
        while(lines > 0 && text > 0) {
            if(fgets(cp, L, is) == NULL) {
                if(i >= eargc) {
                    ++done;
                    break;
                }
                fclose(is);
                if((f = setfil(i++)) == NULL)
                    is = stdin;
                else if((is = fopen(f, "r")) == NULL)
                    cant(f);
                continue;
            }
            *lp++ = cp;
            len = strlen(cp) + 1; /* null terminate */
            if(cp[len - 2] != '\n')
                if (len == L) {
                    diag("line too long (skipped): ", cp);
                    while((c=getc(is)) != EOF && c != '\n')
                        /* throw it away */;
                    --lp;
                    continue;
                } else {
                    diag("missing newline before EOF in ",
                        f ? f : "standard input");
                    /* be friendly, append a newline */
                    ++len;
                    cp[len - 2] = '\n';
                    cp[len - 1] = '\0';
                }
            cp += len;
            --lines;
            text -= len;
        }
        qusort((char **)lspace, lp);
        if(done == 0 || nfiles != eargc)
            newfile();
        else
            oldfile();
        clearerr(os);
        while(lp > (char **)lspace) {
            cp = *--lp;
            if(*cp)
                fputs(cp, os);
            if (ferror(os)) {
                error = 1;
                term(0);
            }
        }
        fclose(os);
    } while(done == 0);
}

struct merg
{
    char    l[L];
    FILE    *b;
} *ibuf[N];

void
merge(a,b)
int a, b;
{
    struct  merg    *p;
    register char   *cp, *dp;
    register int     i;
    struct merg **ip, *jp;
    char    *f;
    int j;
    int k, l;
    int muflg;

    p = (struct merg *)lspace;
    j = 0;
    for(i=a; i < b; i++) {
        f = setfil(i);
        if(f == 0)
            p->b = stdin;
        else if((p->b = fopen(f, "r")) == NULL)
            cant(f);
        ibuf[j] = p;
        if(!rline(p))   j++;
        p++;
    }

    do {
        i = j;
        qusort((char **)ibuf, (char **)(ibuf+i));
        l = 0;
        while(i--) {
            cp = ibuf[i]->l;
            if(*cp == '\0') {
                l = 1;
                if(rline(ibuf[i])) {
                    k = i;
                    while(++k < j)
                        ibuf[k-1] = ibuf[k];
                    j--;
                }
            }
        }
    } while(l);

    clearerr(os);
    muflg = mflg & uflg | cflg;
    i = j;
    while(i > 0) {
        cp = ibuf[i-1]->l;
        if (!cflg && (uflg == 0 || muflg || i == 1 ||
            (*compare)(ibuf[i-1]->l,ibuf[i-2]->l))) {
            fputs(cp, os);
            if (ferror(os)) {
                error = 1;
                term(0);
            }
        }
        if(muflg){
            cp = ibuf[i-1]->l;
            dp = p->l;
            do {
            } while((*dp++ = *cp++) != '\n');
        }
        for(;;) {
            if(rline(ibuf[i-1])) {
                i--;
                if(i == 0)
                    break;
                if(i == 1)
                    muflg = uflg;
            }
            ip = &ibuf[i];
            while(--ip>ibuf&&(*compare)(ip[0]->l,ip[-1]->l)<0){
                jp = *ip;
                *ip = *(ip-1);
                *(ip-1) = jp;
            }
            if(!muflg)
                break;
            j = (*compare)(ibuf[i-1]->l,p->l);
            if(cflg) {
                if(j > 0)
                    disorder("disorder:",ibuf[i-1]->l);
                else if(uflg && j==0)
                    disorder("nonunique:",ibuf[i-1]->l);
            } else if(j == 0)
                continue;
            break;
        }
    }
    p = (struct merg *)lspace;
    for(i=a; i<b; i++) {
        fclose(p->b);
        p++;
        if(i >= eargc)
            unlink(setfil(i));
    }
    fclose(os);
}

void
disorder(s,t)
char *s, *t;
{
    register char *u;
    for(u=t; *u!='\n';u++) ;
    *u = 0;
    diag(s,t);
    term(0);
}

void
newfile()
{
    register char *f;

    f = setfil(nfiles);
    if((os=fopen(f, "w")) == NULL) {
        diag("can't create ",f);
        term(0);
    }
    nfiles++;
}

char *
setfil(i)
int i;
{
    if(i < eargc)
        if(eargv[i][0] == '-' && eargv[i][1] == '\0')
            return(0);
        else
            return(eargv[i]);
    i -= eargc;
    sprintf(filep, "%u", (unsigned)i);
    return(file);
}

void
oldfile()
{
    if(outfil) {
        if((os=fopen(outfil, "w")) == NULL) {
            diag("can't create ",outfil);
            term(0);
        }
    } else
        os = stdout;
}

void
safeoutfil()
{
    register int i;
    struct stat obuf,ibuf;

    if(!mflg||outfil==0)
        return;
    if(stat(outfil,&obuf)==-1)
        return;
    for(i=eargc-N;i<eargc;i++) {    /*-N is suff., not nec.*/
        if(stat(eargv[i],&ibuf)==-1)
            continue;
        if(obuf.st_dev==ibuf.st_dev&&
           obuf.st_ino==ibuf.st_ino)
            unsafeout++;
    }
}

void
cant(f)
char *f;
{
    perror(f);
    term(0);
}

void
diag(s,t)
char *s, *t;
{
    fputs("sort: ",stderr);
    fputs(s,stderr);
    fputs(t,stderr);
    fputs("\n",stderr);
}

void
term(sig)
int sig;
{
    register int i;

    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    if(nfiles == eargc)
        nfiles++;
    for(i=eargc; i<=nfiles; i++) {  /*<= in case of interrupt*/
        unlink(setfil(i));  /*with nfiles not updated*/
    }
    _exit(error);
}

/*
 * The V7 tables indexed signed bytes through a pointer to element 128.
 * ARM uses unsigned char, so direct indexing could read beyond each table.
 * Explicit unsigned predicates also define -d and -i for every input byte.
 */
static int
sort_code(unsigned char code_kind, unsigned char input_byte)
{
    if(code_kind == CODE_FOLD && input_byte >= 'a' && input_byte <= 'z')
        return(input_byte - 'a' + 'A');
    return(input_byte);
}

static int
sort_ignored(unsigned char ignore_kind, unsigned char input_byte)
{
    if(ignore_kind == IGNORE_NONE)
        return(0);
    if(ignore_kind == IGNORE_NONPRINT)
        return(input_byte < ' ' || input_byte > '~');
    return(!(input_byte == '\t' || input_byte == ' ' ||
        (input_byte >= '0' && input_byte <= '9') ||
        (input_byte >= 'A' && input_byte <= 'Z') ||
        (input_byte >= 'a' && input_byte <= 'z')));
}

int
cmp(i, j)
char *i, *j;
{
    register char *pa, *pb;
    char *skip();
    unsigned char code, ignore;
    int a, b;
    int k;
    char *la, *lb;
    register int sa;
    int sb;
    char *ipa, *ipb, *jpa, *jpb;
    struct field *fp;

    for(k = nfields>0; k<=nfields; k++) {
        fp = &fields[k];
        pa = i;
        pb = j;
        if(k) {
            la = skip(pa, fp, 1);
            pa = skip(pa, fp, 0);
            lb = skip(pb, fp, 1);
            pb = skip(pb, fp, 0);
        } else {
            la = eol(pa);
            lb = eol(pb);
        }
        if(fp->nflg) {
            if(tabchar) {
                if(pa<la&&*pa==tabchar)
                    pa++;
                if(pb<lb&&*pb==tabchar)
                    pb++;
            }
            while(blank(*pa))
                pa++;
            while(blank(*pb))
                pb++;
            sa = sb = fp->rflg;
            if(*pa == '-') {
                pa++;
                sa = -sa;
            }
            if(*pb == '-') {
                pb++;
                sb = -sb;
            }
            for(ipa = pa; ipa<la&&ascii_digit(*ipa); ipa++) ;
            for(ipb = pb; ipb<lb&&ascii_digit(*ipb); ipb++) ;
            jpa = ipa;
            jpb = ipb;
            a = 0;
            if(sa==sb)
                while(ipa > pa && ipb > pb)
                    if(b = *--ipb - *--ipa)
                        a = b;
            while(ipa > pa)
                if(*--ipa != '0')
                    return(-sa);
            while(ipb > pb)
                if(*--ipb != '0')
                    return(sb);
            if(a) return(a*sa);
            if(*(pa=jpa) == '.')
                pa++;
            if(*(pb=jpb) == '.')
                pb++;
            if(sa==sb)
                while(pa<la && ascii_digit(*pa)
                   && pb<lb && ascii_digit(*pb))
                    if(a = *pb++ - *pa++)
                        return(a*sa);
            while(pa<la && ascii_digit(*pa))
                if(*pa++ != '0')
                    return(-sa);
            while(pb<lb && ascii_digit(*pb))
                if(*pb++ != '0')
                    return(sb);
            continue;
        }
        code = fp->code;
        ignore = fp->ignore;
loop:
        while(pa < la && sort_ignored(ignore, (unsigned char)*pa))
            pa++;
        while(pb < lb && sort_ignored(ignore, (unsigned char)*pb))
            pb++;
        if(pa>=la || *pa=='\n')
            if(pb<lb && *pb!='\n')
                return(fp->rflg);
            else continue;
        if(pb>=lb || *pb=='\n')
            return(-fp->rflg);
        if((sa = sort_code(code, (unsigned char)*pb++) -
            sort_code(code, (unsigned char)*pa++)) == 0)
            goto loop;
        return(sa*fp->rflg);
    }
    if(uflg)
        return(0);
    return(cmpa(i, j));
}

int
cmpa(pa, pb)
register char *pa, *pb;
{
    while(*pa == *pb) {
        if(*pa++ == '\n')
            return(0);
        pb++;
    }
    return(
        *pa == '\n' ? fields[0].rflg:
        *pb == '\n' ?-fields[0].rflg:
        (unsigned char)*pb > (unsigned char)*pa ? fields[0].rflg:
        -fields[0].rflg
    );
}

char *
skip(pp, fp, j)
struct field *fp;
char *pp;
int j;
{
    register int i;
    register char *p;

    p = pp;
    if( (i=fp->m[j]) < 0)
        return(eol(p));
    while(i-- > 0) {
        if(tabchar != 0) {
            while(*p != tabchar)
                if(*p != '\n')
                    p++;
                else goto ret;
            if(i>0||j==0)
                p++;
        } else {
            while(blank(*p))
                p++;
            while(!blank(*p))
                if(*p != '\n')
                    p++;
                else goto ret;
        }
    }
    if(tabchar==0||fp->bflg[j])
        while(blank(*p))
            p++;
    i = fp->n[j];
    while(i-- > 0) {
        if(*p != '\n')
            p++;
        else goto ret;
    }
ret:
    return(p);
}

char *
eol(p)
register char *p;
{
    while(*p != '\n') p++;
    return(p);
}

void
copyproto()
{
    fields[nfields] = proto;
}

void
field(s,k)
char *s;
int k;
{
    register struct field *p;
    register int d;
    p = &fields[nfields];
    d = 0;
    for(; *s!=0; s++) {
        switch(*s) {
        case '\0':
            return;

        case 'b':
            p->bflg[k]++;
            break;

        case 'd':
            p->ignore = IGNORE_DICTIONARY;
            break;

        case 'f':
            p->code = CODE_FOLD;
            break;
        case 'i':
            p->ignore = IGNORE_NONPRINT;
            break;

        case 'c':
            cflg = 1;
            continue;

        case 'm':
            mflg = 1;
            continue;

        case 'n':
            p->nflg++;
            break;
        case 't':
            tabchar = *++s;
            if(tabchar == 0) s--;
            continue;

        case 'r':
            p->rflg = -1;
            continue;
        case 'u':
            uflg = 1;
            break;

        case '.':
            if(p->m[k] == -1)   /* -m.n with m missing */
                p->m[k] = 0;
            d = &fields[0].n[0]-&fields[0].m[0];

        default:
            p->m[k+d] = number(&s);
        }
        compare = cmp;
    }
}

int
number(ppa)
char **ppa;
{
    int n;
    register char *pa;
    pa = *ppa;
    n = 0;
    while(ascii_digit(*pa)) {
        n = n*10 + *pa - '0';
        *ppa = pa++;
    }
    return(n);
}

#define qsexc(p,q) t= *p;*p= *q;*q=t
#define qstexc(p,q,r) t= *p;*p= *r;*r= *q;*q=t

void
qusort(a,l)
char **a, **l;
{
    register char **i, **j;
    char **k;
    char **lp, **hp;
    int c;
    char *t;
    unsigned n;

start:
    if((n=l-a) <= 1)
        return;


    n /= 2;
    hp = lp = a+n;
    i = a;
    j = l-1;


    for(;;) {
        if(i < lp) {
            if((c = (*compare)(*i, *lp)) == 0) {
                --lp;
                qsexc(i, lp);
                continue;
            }
            if(c < 0) {
                ++i;
                continue;
            }
        }

loop:
        if(j > hp) {
            if((c = (*compare)(*hp, *j)) == 0) {
                ++hp;
                qsexc(hp, j);
                goto loop;
            }
            if(c > 0) {
                if(i == lp) {
                    ++hp;
                    qstexc(i, hp, j);
                    i = ++lp;
                    goto loop;
                }
                qsexc(i, j);
                --j;
                ++i;
                continue;
            }
            --j;
            goto loop;
        }


        if(i == lp) {
            if(uflg)
                for(k=lp+1; k<=hp;) **k++ = '\0';
            if(lp-a >= l-hp) {
                qusort(hp+1, l);
                l = lp;
            } else {
                qusort(a, lp);
                a = hp+1;
            }
            goto start;
        }


        --lp;
        qstexc(j, lp, i);
        j = --hp;
    }
}
