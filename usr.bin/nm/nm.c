/*
 * nm - print name list. string table version
 */
#ifdef CROSS
#   include <stdint.h>
#   include <sys/types.h>
#   include <stdio.h>
#   include <string.h>
#   include <stdlib.h>
#   include <errno.h>
#   include <ctype.h>
#else
#   include <sys/types.h>
#   include <sys/dir.h>
#   include <sys/file.h>
#   include <stdio.h>
#   include <stdlib.h>
#   include <string.h>
#   include <strings.h>
#   include <errno.h>
#   include <ctype.h>
#endif
#include <ar.h>
#include <a.out.h>
#include <limits.h>

#ifdef CROSS
#   include "../ar/archive.h"
#else
#   include "archive.h"
#endif

CHDR	chdr;

char	gflg, nflg, oflg, pflg, uflg, archive_input;
signed char rflg = 1;
char	**xargv;

union {
        char	mag_armag[SARMAG+1];
        struct	exec mag_exp;
} mag_un;

int	narg, errs;
static long file_end;

void
error(char *s)
{
	fprintf(stderr, "nm: %s:", *xargv);
	if (archive_input) {
		fprintf(stderr, "(%s)", chdr.name);
		fprintf(stderr, ": ");
	} else
		fprintf(stderr, " ");
	fprintf(stderr, "%s\n", s);
	errs = 1;
}

/*
 * "borrowed" from 'ar' because we didn't want to drag in everything else
 * from 'ar'.  The error checking was also ripped out, basically if any
 * of the criteria for being an archive are not met then a -1 is returned
 * and the rest of 'ld' figures out what to do.
 */

/*
 * read the archive header for this member.  Use a file pointer
 * rather than a file descriptor.
 */
int
nm_get_arobj(FILE *fp)
{
	struct ar_hdr *hdr;
	register int len, nr;
	register char *p;
	char buf[20];
        static char hb[sizeof(struct ar_hdr) + 1];	/* real header */

	nr = fread(hb, 1, sizeof(struct ar_hdr), fp);
	if (nr != sizeof(struct ar_hdr))
		return(-1);

	hdr = (struct ar_hdr *)hb;
	if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1))
		return(-1);

/* Convert ar header field to an integer. */
#define	AR_ATOI(from, to, len, base, limit) { \
	char *end; \
	unsigned long number; \
	bcopy(from, buf, len); \
	buf[len] = '\0'; \
	if (buf[0] < '0' || buf[0] > '9') return -1; \
	errno = 0; \
	number = strtoul(buf, &end, base); \
	while (*end == ' ') end++; \
	if (errno == ERANGE || end != buf + (len) || number > (unsigned long)(limit)) return -1; \
	to = number; }

	/* Convert the header into the internal format. */
	AR_ATOI(hdr->ar_date, chdr.date, sizeof(hdr->ar_date), 10, AR_DATE_MAX);
	AR_ATOI(hdr->ar_uid, chdr.uid, sizeof(hdr->ar_uid), 10, INT_MAX);
	AR_ATOI(hdr->ar_gid, chdr.gid, sizeof(hdr->ar_gid), 10, INT_MAX);
	AR_ATOI(hdr->ar_mode, chdr.mode, sizeof(hdr->ar_mode), 8, USHRT_MAX);
	AR_ATOI(hdr->ar_size, chdr.size, sizeof(hdr->ar_size), 10, LONG_MAX);

	/* Leading spaces should never happen. */
	if (hdr->ar_name[0] == ' ')
		return(-1);

	/*
	 * Long name support.  Set the "real" size of the file, and the
	 * long name flag/size.
	 */
	if (!bcmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1)) {
		AR_ATOI(hdr->ar_name + sizeof(AR_EFMT1) - 1, len,
		    sizeof(hdr->ar_name) - (sizeof(AR_EFMT1) - 1), 10, MAXNAMLEN);
		chdr.lname = len;
		if (len <= 0 || len > MAXNAMLEN)
			return(-1);
		if (len > chdr.size)
			return -1;
		nr = fread(chdr.name, 1, (size_t)len, fp);
		if (nr != len)
			return(-1);
		chdr.name[len] = 0;
		chdr.size -= len;
	} else {
		chdr.lname = 0;
		bcopy(hdr->ar_name, chdr.name, sizeof(hdr->ar_name));

		/* Strip trailing spaces, null terminate. */
		for (p = chdr.name + sizeof(hdr->ar_name) - 1; *p == ' '; --p);
		*++p = '\0';
	}
	return(1);
}

off_t
nextel(FILE *af, off_t off)
{
	long payload, stored;

	chdr.name[0] = '\0';
	if (off == file_end)
		return 0;
	if (off < 0 || off > file_end ||
	    file_end - off < (long)sizeof(struct ar_hdr) ||
	    fseek(af, off, SEEK_SET) != 0 || nm_get_arobj(af) < 0) {
		error("malformed archive header");
		return 0;
	}
	payload = off + sizeof(struct ar_hdr);
	if (chdr.size < 0 || chdr.size > LONG_MAX - chdr.lname) {
		error("archive size overflow");
		return 0;
	}
	stored = chdr.size + chdr.lname;
	if (stored > file_end - payload ||
	    (stored & 1) > file_end - payload - stored) {
		error("truncated archive member");
		return 0;
	}
	return payload + stored + (stored & 1);
}

/*
 * Read a symbol table entry.
 * Return a number of bytes read, or -1 on EOF.
 * Format of symbol record:
 *  1 byte: length of name in bytes
 *  1 byte: type of symbol (N_UNDF, N_ABS, N_TEXT, etc)
 *  4 bytes: value
 *  N bytes: name
 */
int
fgetsym(FILE *fi, char *name, unsigned int *value, unsigned short *type,
    unsigned remaining)
{
        unsigned char header[6];
        unsigned length, i;
        int byte;

        if (remaining == 0 || (byte = getc(fi)) == EOF)
                return -1;
        length = (unsigned)byte;
        if (length == 0) {
                while (--remaining != 0)
                        if (getc(fi) != 0)
                                return -1;
                return 0;
        }
        if (remaining < sizeof(header) ||
            length > remaining - sizeof(header) ||
            fread(header + 1, 1, sizeof(header) - 1, fi) != sizeof(header) - 1)
                return -1;
        for (i = 0; i < length; i++) {
                byte = getc(fi);
                if (byte == EOF)
                        return -1;
                if (name)
                        name[i] = (char)byte;
        }
        if (name)
                name[length] = '\0';
        *type = header[1];
        *value = (unsigned)header[2] | ((unsigned)header[3] << 8) |
            ((unsigned)header[4] << 16) | ((unsigned)header[5] << 24);
        return (int)(length + sizeof(header));
}

int
compare(const void *a, const void *b)
{
	const struct nlist *p1 = (const struct nlist *)a;
	const struct nlist *p2 = (const struct nlist *)b;

	if (nflg) {
		if (p1->n_value > p2->n_value)
			return(rflg);
		if (p1->n_value < p2->n_value)
			return(-rflg);
	}
	return (rflg * strcmp(p1->n_name, p2->n_name));
}

void
psyms(struct nlist *symp, int nsyms)
{
	register int n, c;

	for (n=0; n<nsyms; n++) {
		c = symp[n].n_type;
		if (c == N_FN)
			c = 'f';
		else switch (c&N_TYPE) {

		case N_UNDF:
			c = 'u';
			if (symp[n].n_value)
				c = 'c';
			break;
		case N_ABS:
			c = 'a';
			break;
		case N_TEXT:
			c = 't';
			break;
		case N_DATA:
			c = 'd';
			break;
		case N_BSS:
			c = 'b';
			break;
		case N_STRNG:
			c = 's';
			break;
		case N_COMM:
			c = 'c';
			break;
		case N_FN:
			c = 'f';
			break;
		default:
			c = '?';
			break;
		}
		if (uflg && c!='u')
			continue;
		if (oflg) {
			if (archive_input)
				printf("%s:", *xargv);
			printf("%s:", archive_input ? chdr.name : *xargv);
		}
		if (symp[n].n_type & N_WEAK)
			c = 'w';
		if (symp[n].n_type & N_EXT)
			c = toupper(c);
		if (! uflg) {
			if (c=='u' || c=='U')
				printf("        ");
			else
				printf("%08x", symp[n].n_value);
			printf(" %c ", c);
		}
		printf("%s\n", symp[n].n_name);
	}
}

void
namelist(void)
{
	off_t   off;
	char	ibuf[BUFSIZ];
	register FILE	*fi;

	archive_input = 0;
	fi = fopen(*xargv, "r");
	if (fi == NULL) {
		error("cannot open");
		return;
	}
	setbuf(fi, ibuf);
	if (fseek(fi, 0, SEEK_END) != 0 || (file_end = ftell(fi)) < 0 ||
	    fseek(fi, 0, SEEK_SET) != 0) {
		error("cannot determine file extent");
		goto out;
	}

	off = 0;
	memset(&mag_un, 0, sizeof(mag_un));
	if (fread((char *)&mag_un, 1, SARMAG, fi) != SARMAG) {
		error("read error");
		goto out;
	}

	if (strncmp(mag_un.mag_armag, ARMAG, SARMAG)==0) {
		archive_input = 1;
		off = SARMAG;
	}
	else if (N_BADMAG(mag_un.mag_exp)) {
		error("bad format");
		goto out;
	}
	if (fseek(fi, 0, SEEK_SET) != 0) {
		error("cannot rewind input");
		goto out;
	}

	if (archive_input) {
		off = nextel(fi, off);
		if (off == 0)
			goto out;
		if (narg > 1)
			printf("\n%s:\n", *xargv);
	}

	do {
		off_t	o, curpos;
		unsigned long extent, sections[4];
		unsigned section, capacity;
		register int i;
		unsigned n;
		struct nlist *symp = NULL;

		curpos = ftell(fi);
		if (curpos < 0) {
			error("cannot locate object");
			goto out;
		}
		extent = archive_input ? (unsigned long)chdr.size :
		    (unsigned long)file_end;
		if (archive_input &&
		    (strcmp(chdr.name, "__.SYMDEF") == 0 ||
		    strcmp(chdr.name, "__.SYMDEF SORTED") == 0))
			continue;
		if (extent < sizeof(mag_un.mag_exp.a_midmag)) {
			if (!archive_input)
				error("truncated object header");
			continue;
		}
		if (fread(&mag_un.mag_exp.a_midmag, 1,
		    sizeof(mag_un.mag_exp.a_midmag), fi) !=
		    sizeof(mag_un.mag_exp.a_midmag)) {
			error("cannot read object magic");
			goto out;
		}
		if (N_BADMAG(mag_un.mag_exp))
			continue;
		if (extent < sizeof(struct exec) ||
		    fread((char *)&mag_un.mag_exp + sizeof(mag_un.mag_exp.a_midmag),
		    1, sizeof(struct exec) - sizeof(mag_un.mag_exp.a_midmag), fi) !=
		    sizeof(struct exec) - sizeof(mag_un.mag_exp.a_midmag)) {
			error("truncated object header");
			goto out;
		}

		o = sizeof(struct exec);
		sections[0] = mag_un.mag_exp.a_text;
		sections[1] = mag_un.mag_exp.a_data;
		sections[2] = N_GETMAGIC(mag_un.mag_exp) == RMAGIC ?
		    mag_un.mag_exp.a_reltext : 0;
		sections[3] = N_GETMAGIC(mag_un.mag_exp) == RMAGIC ?
		    mag_un.mag_exp.a_reldata : 0;
		for (section = 0; section < 4; section++) {
			if (sections[section] > extent - (unsigned long)o) {
				error("object section exceeds member");
				goto out;
			}
			o += sections[section];
		}
		if (mag_un.mag_exp.a_syms > extent - (unsigned long)o ||
		    fseek(fi, curpos + o, SEEK_SET) != 0) {
			error("invalid symbol table extent");
			goto out;
		}
		n = mag_un.mag_exp.a_syms;
		if (n == 0) {
			error("no name list");
			continue;
		}

		i = 0;
		while (n > 0) {
			unsigned value;
                        unsigned short type;

                        int c = fgetsym (fi, 0, &value, &type, n);
                        if (c < 0) {
                                error("malformed symbol table");
                                goto out;
                        }
                        if (c == 0)
                                break;
                        n -= c;
			if (gflg && (type & N_EXT) == 0)
				continue;
			if (uflg && (type & N_TYPE) == N_UNDF && value != 0)
				continue;
			if (i == INT_MAX ||
			    (size_t)i >= (size_t)-1 / sizeof(struct nlist)) {
				error("too many symbols");
				goto out;
			}
			i++;
		}

		if (fseek(fi, curpos + o, SEEK_SET) != 0) {
			error("cannot rewind symbol table");
			goto out;
		}
		capacity = (unsigned)i;
		if (capacity != 0) {
			symp = malloc((size_t)capacity * sizeof(struct nlist));
			if (symp == NULL) {
				error("out of memory");
				goto out;
			}
		}
		i = 0;
		n = mag_un.mag_exp.a_syms;
		while (n > 0) {
		        char name [256];
                        unsigned value;
                        unsigned short type;

                        int c = fgetsym(fi, name, &value, &type, n);
                        if (c < 0) {
                                error("malformed symbol table");
                                goto symbol_failure;
                        }
                        if (c == 0)
                                break;
                        n -= c;
			if (gflg && (type & N_EXT) == 0)
				continue;
			if (uflg && (type & N_TYPE) == N_UNDF && value != 0)
				continue;
			if ((unsigned)i == capacity) {
				error("symbol table changes between reads");
				goto symbol_failure;
			}

                        symp[i].n_name = malloc(c - 5);
                        if (! symp[i].n_name) {
				error("out of memory");
				goto symbol_failure;
			}
			symp[i].n_value = value;
			symp[i].n_type = type;
			memcpy(symp[i].n_name, name, c - 6);
			symp[i].n_name[c - 6] = '\0';
			i++;
		}

		if (pflg==0 && i > 1)
			qsort(symp, i, sizeof(struct nlist), compare);
		if ((archive_input || narg>1) && oflg==0)
			printf("\n%s:\n", archive_input ? chdr.name : *xargv);

		psyms(symp, i);
		if (symp) {
		        for (n=0; n<(unsigned)i; n++)
                                free (symp[n].n_name);
			free((char *)symp);
                        symp = NULL;
                }
		continue;
symbol_failure:
		while (i > 0)
			free(symp[--i].n_name);
		free(symp);
		goto out;
	} while(archive_input && (off = nextel(fi, off)) != 0);
out:
	if (fclose(fi) != 0)
		error("cannot close input");
}

int
main(int argc, char *argv[])
{
	if (--argc>0 && argv[1][0]=='-' && argv[1][1]!=0) {
		argv++;
		while (*++*argv) switch (**argv) {

		case 'n':
			nflg++;
			continue;
		case 'g':
			gflg++;
			continue;
		case 'u':
			uflg++;
			continue;
		case 'r':
			rflg = -1;
			continue;
		case 'p':
			pflg++;
			continue;
		case 'o':
			oflg++;
			continue;
		case 'h':
usage:                  fprintf (stderr, "Usage:\n");
                        fprintf (stderr, "  nm [-gunrpo] file...\n");
                        fprintf (stderr, "Options:\n");
                        fprintf (stderr, "  -g      Display only external symbols\n");
                        fprintf (stderr, "  -u      Display only undefined symbols\n");
                        fprintf (stderr, "  -n      Sort symbols numerically by address\n");
                        fprintf (stderr, "  -r      Reverse the order of the sort\n");
                        fprintf (stderr, "  -p      Do not sort the symbols\n");
                        fprintf (stderr, "  -o      Precede each symbol by the file name\n");
                        return(1);
		default:
			fprintf(stderr, "nm: invalid argument -%c\n",
			    *argv[0]);
                        goto usage;
		}
		argc--;
	}
	if (argc == 0) {
		argc = 1;
		argv[1] = "a.out";
	}
	narg = argc;
	xargv = argv;
	while (argc--) {
		++xargv;
		namelist();
	}
	if (fflush(stdout) == EOF || ferror(stdout))
		error("cannot write symbol output");
	return(errs);
}
