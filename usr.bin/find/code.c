/*
 * PURPOSE:	sorted list compressor (works with a modified 'find'
 *		to encode/decode a filename database)
 *
 * USAGE:	bigram < list > bigrams
 *		process bigrams (see updatedb) > common_bigrams
 *		code common_bigrams < list > squozen_list
 *
 * METHOD:	Uses 'front compression' (see ";login:", March 1983, p. 8 ).
 *		Output format is, per line, an offset differential count byte
 *		followed by a partially bigram-encoded ascii residue.
 *
 *  	The codes are:
 *
 *	0-28	likeliest differential counts + offset to make nonnegative
 *	30	escape code for out-of-range count to follow in next word
 *	128-255 bigram codes, (128 most common, as determined by 'updatedb')
 *	32-127  single character (printable) ascii residue
 *
 * SEE ALSO:	updatedb.csh, bigram.c, find.c
 *
 * AUTHOR:	James A. Woods, Informatics General Corp.,
 *		NASA Ames Research Center, 10/82
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPATH 1024		/* maximum pathname length */
#define	RESET	30		/* switch code */
#define	NBIGRAM	256		/* table size: 128 bigrams of two bytes */

int strindex ( char *, char * );
int prefix_length ( char *, char * );

/*
 * The read buffer holds one byte more than the record, because fgets stores
 * the newline that ends it; a pathname of MAXPATH-1 bytes is what find's
 * decoder reconstructs into a buffer of MAXPATH.
 */
char path[MAXPATH + 1];
char oldpath[MAXPATH + 1] = " ";
char bigrams[NBIGRAM + 1] = { 0 };

int
main ( int argc, char *argv[] )
{
  	int count, oldcount, diffcount;
	int j, code;
	char bigram[3], *nl;
	size_t nread;
	FILE *fp;

	oldcount = 0;
	bigram[2] = '\0';

	/*
	 * Standard output carries the database, so the usage message and every
	 * other diagnostic go to standard error; a byte of prose on standard
	 * output is a byte the decoder reads as a count.
	 */
	if ( argc != 2 ) {
		fprintf ( stderr,
		    "usage: code common_bigrams < list > coded_list\n" );
		exit ( 1 );
	}
	if ((fp = fopen(argv[1], "r")) == NULL) {
		fprintf ( stderr, "code: can't open %s\n", argv[1] );
		exit(1);
	}
	/*
	 * The header is NBIGRAM bytes whatever the table file holds: a tree
	 * with fewer than 128 distinct bigrams leaves it short, and the zero
	 * fill that follows is a code no residue byte selects, because a
	 * bigram code is parity-marked and strindex matches only a pair the
	 * table carries.
	 */
	nread = fread ( bigrams, 1, NBIGRAM, fp );
	if ( nread < (size_t) NBIGRAM && ferror ( fp ) ) {
		fprintf ( stderr, "code: can't read %s\n", argv[1] );
		exit ( 1 );
	}
	fclose ( fp );
	fwrite ( bigrams, 1, NBIGRAM, stdout );

	/*
	 * A record is one newline-terminated pathname, so a buffer filled
	 * without a newline carries a pathname the format cannot hold:
	 * encoding its halves as two records enters two files that do not
	 * exist, so the read stops and reports instead. fgets bounds the
	 * store where gets(3) does not, which the flat process image makes
	 * decisive: text, data, bss and stack share one window and no guard
	 * page follows path, so an unbounded store reaches oldpath, the
	 * bigram table and the stack rather than faulting. A final line that
	 * ends at end of file without a newline is a complete record.
	 */
     	while ( fgets ( path, sizeof path, stdin ) != NULL ) {
		nl = strchr ( path, '\n' );
		if ( nl != NULL )
			*nl = '\0';
		else if ( strlen ( path ) == sizeof path - 1 ) {
			fprintf ( stderr,
			    "code: pathname longer than %d bytes\n",
			    MAXPATH - 1 );
			exit ( 1 );
		}
		/*
		   squelch unprintable chars so as not to botch decoding
		*/
		for ( j = 0; path[j] != '\0'; j++ ) {
			path[j] &= 0177;
			if ( path[j] < 040 || path[j] == 0177 )
				path[j] = '?';
		}
		count = prefix_length ( oldpath, path );
		diffcount = count - oldcount;
		if ( (diffcount < -14) || (diffcount > 14) ) {
			putc ( RESET, stdout );
			putw ( diffcount + 14, stdout );
		}
		else
			putc ( diffcount + 14, stdout );

		for ( j = count; path[j] != '\0'; j += 2 ) {
			if ( path[j + 1] == '\0' ) {
				putchar ( path[j] );
				break;
			}
			bigram[0] = path[j];
			bigram[1] = path[j + 1];
			/*
			    linear search for specific bigram in string table
			*/
			if ( (code = strindex ( bigrams, bigram )) % 2 == 0 )
				putchar ( (code / 2) | 0200 );
			else
				fputs ( bigram, stdout );
		}
		strcpy ( oldpath, path );
		oldcount = count;
	}
	return 0;
}

int
strindex ( char *string, char *pattern )	/* return location of pattern */
{						/* in string or -1 */
	register char *s, *p, *q;

	for ( s = string; *s != '\0'; s++ )
		if ( *s == *pattern ) {		/* fast first char check */
			for ( p = pattern + 1, q = s + 1; *p != '\0'; p++, q++ )
				if ( *q != *p )
					break;
			if ( *p == '\0' )
				return ( q - strlen ( pattern ) - string );
		}
	return ( -1 );
}

int
prefix_length ( char *s1, char *s2 )	/* return length of longest common prefix */
{					/* ... of strings s1 and s2 */
	register char *start;

    	for ( start = s1; *s1 == *s2; s1++, s2++ )
		if ( *s1 == '\0' )
	    		break;
    	return ( s1 - start );
}
