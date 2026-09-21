#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define BIG 2147483647
#define LCASE   01
#define UCASE   02
#define SWAB    04
#define NERR    010
#define SYNC    020
#define NTRUNC  040

void    stats();
void    null(int);

int cflag;
int fflag;
/*
 * skip= and seek= name a block, and the offset they reach is that block
 * count times the block size; include/sys/types.h makes off_t a long, so the
 * product needs the full off_t range while the operand itself stays within
 * the int the block sizes use.
 */
off_t   skip;
off_t   seekn;
int count;
int files   = 1;
char    *string;
char    *operand;
char    *ifile;
char    *ofile;
char    *ibuf;
char    *obuf;
int ibs = 512;
int obs = 512;
int bs;
int cbs;
int ibc;
int obc;
int cbc;
int nifr;
int nipr;
int nofr;
int nopr;
int ntrunc;
int ibf;
int obf;
char    *op;
int nspace;
/*
 * The conversion tables are read and never written. tools/elf2aout puts
 * .rodata in a_text, which the kernel may read back from the file rather
 * than write to swap, so const moves 768 bytes of this program from the
 * dirty half of its image to the clean half.
 */
const char etoa[] = {
    0000,0001,0002,0003,0234,0011,0206,0177,
    0227,0215,0216,0013,0014,0015,0016,0017,
    0020,0021,0022,0023,0235,0205,0010,0207,
    0030,0031,0222,0217,0034,0035,0036,0037,
    0200,0201,0202,0203,0204,0012,0027,0033,
    0210,0211,0212,0213,0214,0005,0006,0007,
    0220,0221,0026,0223,0224,0225,0226,0004,
    0230,0231,0232,0233,0024,0025,0236,0032,
    0040,0240,0241,0242,0243,0244,0245,0246,
    0247,0250,0133,0056,0074,0050,0053,0041,
    0046,0251,0252,0253,0254,0255,0256,0257,
    0260,0261,0135,0044,0052,0051,0073,0136,
    0055,0057,0262,0263,0264,0265,0266,0267,
    0270,0271,0174,0054,0045,0137,0076,0077,
    0272,0273,0274,0275,0276,0277,0300,0301,
    0302,0140,0072,0043,0100,0047,0075,0042,
    0303,0141,0142,0143,0144,0145,0146,0147,
    0150,0151,0304,0305,0306,0307,0310,0311,
    0312,0152,0153,0154,0155,0156,0157,0160,
    0161,0162,0313,0314,0315,0316,0317,0320,
    0321,0176,0163,0164,0165,0166,0167,0170,
    0171,0172,0322,0323,0324,0325,0326,0327,
    0330,0331,0332,0333,0334,0335,0336,0337,
    0340,0341,0342,0343,0344,0345,0346,0347,
    0173,0101,0102,0103,0104,0105,0106,0107,
    0110,0111,0350,0351,0352,0353,0354,0355,
    0175,0112,0113,0114,0115,0116,0117,0120,
    0121,0122,0356,0357,0360,0361,0362,0363,
    0134,0237,0123,0124,0125,0126,0127,0130,
    0131,0132,0364,0365,0366,0367,0370,0371,
    0060,0061,0062,0063,0064,0065,0066,0067,
    0070,0071,0372,0373,0374,0375,0376,0377,
};
const char atoe[] = {
    0000,0001,0002,0003,0067,0055,0056,0057,
    0026,0005,0045,0013,0014,0015,0016,0017,
    0020,0021,0022,0023,0074,0075,0062,0046,
    0030,0031,0077,0047,0034,0035,0036,0037,
    0100,0117,0177,0173,0133,0154,0120,0175,
    0115,0135,0134,0116,0153,0140,0113,0141,
    0360,0361,0362,0363,0364,0365,0366,0367,
    0370,0371,0172,0136,0114,0176,0156,0157,
    0174,0301,0302,0303,0304,0305,0306,0307,
    0310,0311,0321,0322,0323,0324,0325,0326,
    0327,0330,0331,0342,0343,0344,0345,0346,
    0347,0350,0351,0112,0340,0132,0137,0155,
    0171,0201,0202,0203,0204,0205,0206,0207,
    0210,0211,0221,0222,0223,0224,0225,0226,
    0227,0230,0231,0242,0243,0244,0245,0246,
    0247,0250,0251,0300,0152,0320,0241,0007,
    0040,0041,0042,0043,0044,0025,0006,0027,
    0050,0051,0052,0053,0054,0011,0012,0033,
    0060,0061,0032,0063,0064,0065,0066,0010,
    0070,0071,0072,0073,0004,0024,0076,0341,
    0101,0102,0103,0104,0105,0106,0107,0110,
    0111,0121,0122,0123,0124,0125,0126,0127,
    0130,0131,0142,0143,0144,0145,0146,0147,
    0150,0151,0160,0161,0162,0163,0164,0165,
    0166,0167,0170,0200,0212,0213,0214,0215,
    0216,0217,0220,0232,0233,0234,0235,0236,
    0237,0240,0252,0253,0254,0255,0256,0257,
    0260,0261,0262,0263,0264,0265,0266,0267,
    0270,0271,0272,0273,0274,0275,0276,0277,
    0312,0313,0314,0315,0316,0317,0332,0333,
    0334,0335,0336,0337,0352,0353,0354,0355,
    0356,0357,0372,0373,0374,0375,0376,0377,
};
/*
 * atoibm repeated atoe at every input but four, so it is those four and a
 * fall-through to atoe. It is not a bijection: 91 and 213 both reach 173,
 * and 93 and 229 both reach 189, so no inverse of it exists and conv=ascii
 * reads etoa, which is atoe's exact inverse over all 256 bytes.
 */
static const struct {
    unsigned char from;
    unsigned char to;
} ibmexcept[] = {
    { 33,  90 },        /* '!' */
    { 91,  173 },       /* '[' */
    { 93,  189 },       /* ']' */
    { 124, 79 },        /* '|' */
};

int
atoibm(c)
    int c;
{
    unsigned i;

    for (i = 0; i < sizeof ibmexcept / sizeof ibmexcept[0]; i++)
        if (ibmexcept[i].from == (unsigned char)c)
            return (ibmexcept[i].to);
    return (atoe[c] & 0377);
}

void
term(status)
    int status;
{
    stats();
    exit(status);
}

void
flsh()
{
    register int c;

    if(obc) {
        if(obc == obs)
            nofr++; else
            nopr++;
        c = write(obf, obuf, obc);
        if(c != obc) {
            perror("write");
            term(1);
        }
        obc = 0;
    }
}

int
match(s)
char *s;
{
    register char *cs;

    cs = string;
    while(*cs++ == *s)
        if(*s++ == '\0')
            goto true;
    if(*s != '\0')
        return(0);

true:
    cs--;
    string = cs;
    return(1);
}

void
oorange()
{
    fprintf(stderr, "dd: argument %s out of range\n", operand);
    exit(1);
}

/*
 * An operand is rejected before the step that would carry it past big, not
 * after: C leaves a signed overflow undefined and wraps an unsigned one, so
 * a product read back once it has wrapped no longer names what the caller
 * wrote. On a machine whose long is 32 bits an after-the-fact test accepts
 * bs=4294967296 as zero and count=4294967297 as one.
 *
 * Every partial value stays below big, which holds because each suffix and
 * each x factor scales upward. A zero factor is the one exception and
 * carries no partial past the bound, so it needs no test.
 */
unsigned long
scale(n, factor, big)
    unsigned long n;
    unsigned long factor;
    unsigned long big;
{
    if (factor != 0 && n > (big - 1) / factor)
        oorange();
    return (n * factor);
}

long
number(big)
    long big;
{
    register char *cs;
    unsigned long n, d, lim;

    cs = string;
    n = 0;
    lim = (unsigned long)big;
    while(*cs >= '0' && *cs <= '9') {
        d = (unsigned long)(*cs++ - '0');
        if (d >= lim || n > (lim - 1 - d) / 10)
            oorange();
        n = n*10 + d;
    }
    for(;;)
    switch(*cs++) {

    case 'k':
        n = scale(n, 1024UL, lim);
        continue;

    case 'w':
        n = scale(n, (unsigned long)sizeof(int), lim);
        continue;

    case 'b':
        n = scale(n, 512UL, lim);
        continue;

    case '*':
    case 'x':
        string = cs;
        n = scale(n, (unsigned long)number(BIG), lim);
        /* FALLTHROUGH */

    case '\0':
        if (n >= lim)
            oorange();
        return((long)n);
    }
    /* never gets here */
}

void
cnull(cc)
    int cc;
{
    register int c;

    c = cc;
    if(cflag&UCASE && c>='a' && c<='z')
        c += 'A'-'a';
    if(cflag&LCASE && c>='A' && c<='Z')
        c += 'a'-'A';
    null(c);
}

void
null(c)
    int c;
{
    *op = c;
    op++;
    if(++obc >= obs) {
        flsh();
        op = obuf;
    }
}

void
ascii(cc)
    int cc;
{
    register int c;

    c = etoa[cc] & 0377;
    if(cbs == 0) {
        cnull(c);
        return;
    }
    if(c == ' ') {
        nspace++;
        goto out;
    }
    while(nspace > 0) {
        null(' ');
        nspace--;
    }
    cnull(c);

out:
    if(++cbc >= cbs) {
        null('\n');
        cbc = 0;
        nspace = 0;
    }
}

void
unblock(cc)
    int cc;
{
    register int c;

    c = cc & 0377;
    if(cbs == 0) {
        cnull(c);
        return;
    }
    if(c == ' ') {
        nspace++;
        goto out;
    }
    while(nspace > 0) {
        null(' ');
        nspace--;
    }
    cnull(c);

out:
    if(++cbc >= cbs) {
        null('\n');
        cbc = 0;
        nspace = 0;
    }
}

void
ebcdic(cc)
    int cc;
{
    register int c;

    c = cc;
    if(cflag&UCASE && c>='a' && c<='z')
        c += 'A'-'a';
    if(cflag&LCASE && c>='A' && c<='Z')
        c += 'a'-'A';
    c = atoe[c] & 0377;
    if(cbs == 0) {
        null(c);
        return;
    }
    if(cc == '\n') {
        while(cbc < cbs) {
            null(atoe[' ']);
            cbc++;
        }
        cbc = 0;
        return;
    }
    if(cbc == cbs)
        ntrunc++;
    cbc++;
    if(cbc <= cbs)
        null(c);
}

void
ibm(cc)
    int cc;
{
    register int c;

    c = cc;
    if(cflag&UCASE && c>='a' && c<='z')
        c += 'A'-'a';
    if(cflag&LCASE && c>='A' && c<='Z')
        c += 'a'-'A';
    c = atoibm(c);
    if(cbs == 0) {
        null(c);
        return;
    }
    if(cc == '\n') {
        while(cbc < cbs) {
            null(atoibm(' '));
            cbc++;
        }
        cbc = 0;
        return;
    }
    if(cbc == cbs)
        ntrunc++;
    cbc++;
    if(cbc <= cbs)
        null(c);
}

void
block(cc)
    int cc;
{
    register int c;

    c = cc;
    if(cflag&UCASE && c>='a' && c<='z')
        c += 'A'-'a';
    if(cflag&LCASE && c>='A' && c<='Z')
        c += 'a'-'A';
    c &= 0377;
    if(cbs == 0) {
        null(c);
        return;
    }
    if(cc == '\n') {
        while(cbc < cbs) {
            null(' ');
            cbc++;
        }
        cbc = 0;
        return;
    }
    if(cbc == cbs)
        ntrunc++;
    cbc++;
    if(cbc <= cbs)
        null(c);
}

void
stats()
{
    fprintf(stderr,"%u+%u records in\n", nifr, nipr);
    fprintf(stderr,"%u+%u records out\n", nofr, nopr);
    if(ntrunc)
        fprintf(stderr,"%u truncated records\n", ntrunc);
}

int
main(argc, argv)
int argc;
char    **argv;
{
    void (*conv)(); // XXX Return type was 'int'.
    /* ibc starts at zero, so the refill branch sets ip before the
       conversion stage reads it; the initializer states that to the
       compiler, which reaches the stage through a goto. */
    register char *ip = 0;
    register int c;
    int a;

    conv = null;
    for(c=1; c<argc; c++) {
        string = argv[c];
        /* match() advances string, so the whole operand is kept for the
           range diagnostic number() prints. */
        operand = string;
        if(match("ibs=")) {
            ibs = number(BIG);
            continue;
        }
        if(match("obs=")) {
            obs = number(BIG);
            continue;
        }
        if(match("cbs=")) {
            cbs = number(BIG);
            continue;
        }
        if (match("bs=")) {
            bs = number(BIG);
            continue;
        }
        if(match("if=")) {
            ifile = string;
            continue;
        }
        if(match("of=")) {
            ofile = string;
            continue;
        }
        if(match("skip=")) {
            skip = number(BIG);
            continue;
        }
        if(match("seek=")) {
            seekn = number(BIG);
            continue;
        }
        if(match("count=")) {
            count = number(BIG);
            continue;
        }
        if(match("files=")) {
            files = number(BIG);
            continue;
        }
        if(match("conv=")) {
        cloop:
            if(match(","))
                goto cloop;
            if(*string == '\0')
                continue;
            if(match("ebcdic")) {
                conv = ebcdic;
                goto cloop;
            }
            if(match("ibm")) {
                conv = ibm;
                goto cloop;
            }
            if(match("ascii")) {
                conv = ascii;
                goto cloop;
            }
            if(match("block")) {
                conv = block;
                goto cloop;
            }
            if(match("unblock")) {
                conv = unblock;
                goto cloop;
            }
            if(match("lcase")) {
                cflag |= LCASE;
                goto cloop;
            }
            if(match("ucase")) {
                cflag |= UCASE;
                goto cloop;
            }
            if(match("swab")) {
                cflag |= SWAB;
                goto cloop;
            }
            if(match("noerror")) {
                cflag |= NERR;
                goto cloop;
            }
            if(match("sync")) {
                cflag |= SYNC;
                goto cloop;
            }
            if(match("notrunc")) {
                cflag |= NTRUNC;
                goto cloop;
            }
        }
        fprintf(stderr,"bad arg: %s\n", string);
        exit(1);
    }
    if(conv == null && cflag&(LCASE|UCASE))
        conv = cnull;
    if (ifile)
        ibf = open(ifile, 0);
    else
        ibf = dup(0);
    if(ibf < 0) {
        perror(ifile);
        exit(1);
    }
    /*
     * creat(2) carries O_TRUNC, which discards the output before seek= has
     * named the offset the copy starts at. The output opens without it and
     * the truncation happens after the seek instead, so the blocks seek=
     * steps over survive and conv=notrunc can suppress the truncation
     * altogether.
     */
    if (ofile)
        obf = open(ofile, O_WRONLY|O_CREAT, 0666);
    else
        obf = dup(1);
    if(obf < 0) {
        fprintf(stderr,"cannot create: %s\n", ofile);
        exit(1);
    }
    if (bs) {
        ibs = obs = bs;
        if (conv == null)
            fflag++;
    }
    if(ibs == 0 || obs == 0) {
        fprintf(stderr,"counts: cannot be zero\n");
        exit(1);
    }
    ibuf = sbrk(ibs);
    if (fflag)
        obuf = ibuf;
    else
        obuf = sbrk(obs);
    sbrk(64);   /* For good measure */
    if(ibuf == (char *)-1 || obuf == (char *)-1) {
        fprintf(stderr, "not enough memory\n");
        exit(1);
    }
    ibc = 0;
    obc = 0;
    cbc = 0;
    op = obuf;

    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, term);
    /*
     * skip= and seek= name a block, and the offset they reach is that count
     * times the block size. off_t is long (include/sys/types.h), so the
     * operand is rejected while the product still fits rather than after it
     * wraps. One lseek(2) reaches the offset where the descriptor is
     * seekable; a pipe refuses the probe, so the input blocks are consumed
     * by reading instead.
     */
    if (skip) {
        if (skip > (off_t)(BIG / ibs)) {
            fprintf(stderr, "dd: skip=%ld out of range for ibs=%d\n",
                (long)skip, ibs);
            exit(1);
        }
        if (lseek(ibf, (off_t)0, SEEK_CUR) < 0) {
            while(skip) {
                read(ibf, ibuf, ibs);
                skip--;
            }
        } else if (lseek(ibf, skip * ibs, SEEK_CUR) < 0) {
            perror("skip");
            exit(1);
        }
    }
    if (seekn) {
        if (seekn > (off_t)(BIG / obs)) {
            fprintf(stderr, "dd: seek=%ld out of range for obs=%d\n",
                (long)seekn, obs);
            exit(1);
        }
        if (lseek(obf, seekn * obs, SEEK_CUR) < 0) {
            perror("seek");
            exit(1);
        }
    }
    /*
     * The output an of= operand names is truncated at the position the copy
     * starts from, which is zero without seek=. ftruncate(2) reaches
     * ufs_setattr() and itrunc() (sys/kern/ufs_fio.c, sys/kern/ufs_inode.c),
     * and itrunc() allocates through bmap() when the length passes i_size,
     * so the call is confined to a regular file: a device inode carries no
     * block list to extend. A descriptor inherited from the caller keeps
     * whatever length the caller gave it.
     */
    if (ofile && (cflag&NTRUNC) == 0) {
        struct stat osb;
        off_t opos = lseek(obf, (off_t)0, SEEK_CUR);

        if (opos >= 0 && fstat(obf, &osb) == 0 && S_ISREG(osb.st_mode) &&
            ftruncate(obf, opos) < 0) {
            perror("truncate");
            exit(1);
        }
    }

loop:
    if(ibc-- == 0) {
        ibc = 0;
        if(count==0 || nifr+nipr!=count) {
            if(cflag&(NERR|SYNC))
            for(ip=ibuf+ibs; ip>ibuf;)
                *--ip = 0;
            ibc = read(ibf, ibuf, ibs);
        }
        /*
         * End of input is ibc == 0 from read(2), and the test precedes the
         * error handler because that handler rewrites ibc from the buffer
         * contents: a buffer conv=noerror zeroed before a failed read
         * reaches the end-of-input test as a zero count and ends the copy
         * with a success status.
         */
        if(ibc == 0 && --files<=0) {
            flsh();
            term(0);
        }
        if(ibc == -1) {
            perror("read");
            if((cflag&NERR) == 0) {
                flsh();
                term(1);
            }
            ibc = 0;
            for(c=0; c<ibs; c++)
                if(ibuf[c] != 0)
                    ibc = c;
            /*
             * read(2) leaves the file offset where it stood when it fails,
             * so conv=noerror steps the input past the failed block itself
             * and the copy advances; the same read repeats otherwise.
             */
            lseek(ibf, (off_t)ibs, SEEK_CUR);
            stats();
        }
        if(ibc != ibs) {
            nipr++;
            if(cflag&SYNC)
                ibc = ibs;
        } else
            nifr++;
        ip = ibuf;
        c = ibc >> 1;
        if(cflag&SWAB && c)
        do {
            a = *ip++;
            ip[-1] = *ip;
            *ip++ = a;
        } while(--c);
        ip = ibuf;
        if (fflag) {
            obc = ibc;
            flsh();
            ibc = 0;
        }
        goto loop;
    }
    c = 0;
    c |= *ip++;
    c &= 0377;
    (*conv)(c);
    goto loop;
}
