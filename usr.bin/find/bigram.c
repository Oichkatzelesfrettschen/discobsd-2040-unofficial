/*
 *  bigram < text > bigrams
 *
 * List bigrams for 'updatedb' script.
 * Use 'code' to encode a file using this output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPATH	1024		/* maximum pathname length */

int prefix_length ( char *, char * );

/*
 * The read buffer holds one byte more than the record, because fgets stores
 * the newline that ends it; a pathname of MAXPATH-1 bytes is what find's
 * decoder reconstructs into a buffer of MAXPATH.
 */
char path[MAXPATH + 1];
char oldpath[MAXPATH + 1] = " ";

int
main ( void )
{
  	register int count, j;
	char *nl;

	/*
	 * A record is one newline-terminated pathname, so a buffer filled
	 * without a newline carries a pathname the format cannot hold:
	 * reading its halves as two pathnames lists the pairs of two names no
	 * file carries and moves the prefix count code derives from the same
	 * list, so the read stops and reports instead. fgets bounds the
	 * store where gets(3) does not, which the flat process image makes
	 * decisive: text, data, bss and stack share one window and no guard
	 * page follows path, so an unbounded store reaches oldpath and the
	 * stack rather than faulting. A final line that ends at end of file
	 * without a newline is a complete record.
	 */
     	while ( fgets ( path, sizeof path, stdin ) != NULL ) {
		nl = strchr ( path, '\n' );
		if ( nl != NULL )
			*nl = '\0';
		else if ( strlen ( path ) == sizeof path - 1 ) {
			fprintf ( stderr,
			    "bigram: pathname longer than %d bytes\n",
			    MAXPATH - 1 );
			exit ( 1 );
		}
		count = prefix_length ( oldpath, path );
		/*
		   output post-residue bigrams only
		*/
		for ( j = count; path[j] != '\0'; j += 2 ) {
			if ( path[j + 1] == '\0' )
				break;
			putchar ( path[j] );
			putchar ( path[j + 1] );
			putchar ( '\n' );
		}
		strcpy ( oldpath, path );
   	}
	return 0;
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
