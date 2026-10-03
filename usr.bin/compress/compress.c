/*
 * Compress - data compression program
 */
#define	MINIMUM(a, b)	((a) > (b) ? (b) : (a))

/*
 * Set USERMEM to the maximum amount of physical user memory available
 * in bytes.  USERMEM is used to determine the maximum BITS that can be used
 * for compression.
 *
 * SACREDMEM is the amount of physical memory saved for others; compress
 * will hog the rest.
 */
#ifndef SACREDMEM
#define SACREDMEM	0
#endif

#ifndef USERMEM
# define USERMEM 	450000	/* default user memory */
#endif

#ifdef USERMEM
# if USERMEM >= (433484+SACREDMEM)
#  define PBITS	16
# else
#  if USERMEM >= (229600+SACREDMEM)
#   define PBITS	15
#  else
#   if USERMEM >= (127536+SACREDMEM)
#    define PBITS	14
#   else
#    if USERMEM >= (73464+SACREDMEM)
#     define PBITS	13
#    else
#     define PBITS	12
#    endif
#   endif
#  endif
# endif
# undef USERMEM
#endif /* USERMEM */

#ifdef PBITS		/* Preferred BITS for this memory size */
# ifndef BITS
#  define BITS PBITS
# endif
#endif /* PBITS */

#if BITS == 16
# define HSIZE	69001		/* 95% occupancy */
#endif
#if BITS == 15
# define HSIZE	35023		/* 94% occupancy */
#endif
#if BITS == 14
# define HSIZE	18013		/* 91% occupancy */
#endif
#if BITS == 13
# define HSIZE	9001		/* 91% occupancy */
#endif
#if BITS <= 12
# define HSIZE	5003		/* 80% occupancy */
#endif


/*
 * a code_int must be able to hold 2**BITS values of type int, and also -1
 */
#if BITS > 15
typedef long int	code_int;
#else
typedef int		code_int;
#endif

typedef long int	  count_int;

#ifdef NO_UCHAR
 typedef char	char_type;
#else
 typedef	unsigned char	char_type;
#endif /* UCHAR */
static const char_type magic_header[] = { 0x1f, 0x9d };

/* Defines for third byte of header */
#define BIT_MASK	0x1f
#define BLOCK_MASK	0x80
/* Masks 0x40 and 0x20 are free.  I think 0x20 should mean that there is
   a fourth header byte (for expansion).
*/
#define INIT_BITS 9			/* initial number of bits/code */

/*
 * compress.c - File compression ala IEEE Computer, June 1984.
 *
 * Authors:	Spencer W. Thomas	(decvax!utah-cs!thomas)
 *		Jim McKie		(decvax!mcvax!jim)
 *		Steve Davies		(decvax!vax135!petsd!peora!srd)
 *		Ken Turkowski		(decvax!decwrl!turtlevax!ken)
 *		James A. Woods		(decvax!ihnp4!ames!jaw)
 *		Joe Orost		(decvax!vax135!petsd!joe)
 */
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef notdef
#include <sys/ioctl.h>
#endif

#define ARGVAL() (*++(*argv) || (--argc && *++argv))

static int n_bits;			/* number of bits/code */
static int maxbits = BITS;		/* user settable max # bits/code */
static code_int maxcode;		/* maximum code, given n_bits */
static code_int maxmaxcode = 1 << BITS;	/* first invalid code */
#ifdef COMPATIBLE		/* But wrong! */
# define MAXCODE(n_bits)	(1 << (n_bits) - 1)
#else
# define MAXCODE(n_bits)	((1 << (n_bits)) - 1)
#endif /* COMPATIBLE */


static count_int htab[HSIZE];
static unsigned short codetab[HSIZE];

#define htabof(i)	htab[i]
#define codetabof(i)	codetab[i]
static code_int hsize = HSIZE;		/* dynamic compression table size */
static count_int fsize;

/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type
 * as the codetab.  The tab_suffix table needs 2**BITS characters.  We
 * get this from the beginning of htab.  The output stack uses the rest
 * of htab, and contains characters.  There is plenty of room for any
 * possible stack (stack used to be 8000 characters).
 */

#define tab_prefixof(i)	codetabof(i)
#define tab_suffixof(i)	((char_type *)(htab))[i]
#define de_stack		((char_type *)&tab_suffixof(1<<BITS))
#define de_stack_end		((char_type *)(htab + HSIZE))

static code_int free_ent;		/* first unused entry */
static int exit_stat;			/* per-file status */
static int perm_stat;			/* permanent status */

static void usage(void);
static void on_interrupt(int);
static void version(void);
static void write_error(void);
static void clear_hash(count_int);
static void output_code(code_int);
static void print_ratio(FILE *, long int, long int);
static void clear_block(void);
static void compress_input(void);
static void decompress_input(void);
static void copy_stats(const char *, const char *);
static code_int get_code(void);
static _Noreturn void corrupt_input(void);
static void push_decompress_byte(char_type **, char_type);
static int has_z_suffix(const char *);
static int make_compressed_name(char *, size_t, const char *);
static int make_decompressed_name(char *, size_t, const char *);
static int set_decompression_parameters(int, const char *);
static int parse_maxbits(const char *, int *);

#ifdef DEBUG
static void print_codes(void);
static void dump_table(void);
static int push_debug_byte(int, int);
static int debug;
#endif /* DEBUG */

static int nomagic;	/* Use a 3-byte magic number header, unless old file. */
static int zcat_flg;	/* Write output on stdout and suppress messages. */
static int precious = 1;	/* Preserve a complete output file on interrupt. */
static int quiet = 1;	/* Suppress compression statistics. */

/*
 * block compression parameters -- after all codes are used up,
 * and compression rate changes, start over.
 */
static int block_compress = BLOCK_MASK;
static int clear_flg;
static long int ratio;
#define CHECK_GAP 10000	/* ratio check interval */
static count_int checkpoint = CHECK_GAP;
/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define FIRST	257	/* first free entry */
#define	CLEAR	256	/* table clear output code */

static int force;
static char output_name[100];
#ifdef DEBUG
static int verbose;
#endif /* DEBUG */
static void (*previous_interrupt)(int);
static int background_flag;

static int decompress_mode;

static int
parse_maxbits(const char *text, int *value)
{
	int parsed_value = 0;
	const unsigned char *character = (const unsigned char *)text;

	if (*character == '\0')
		return -1;
	for (; *character != '\0'; character++) {
		int digit;

		if (*character < '0' || *character > '9')
			return -1;
		digit = *character - '0';
		if (parsed_value > (INT_MAX - digit) / 10)
			return -1;
		parsed_value = parsed_value * 10 + digit;
	}
	*value = parsed_value;
	return 0;
}

static void
usage(void)
{
#ifdef DEBUG
	fprintf(stderr, "Usage: compress [-dDVfc] [-b maxbits] [file ...]\n");
#else
	fprintf(stderr, "Usage: compress [-fvc] [-b maxbits] [file ...]\n");
#endif
}

static void
on_interrupt(int signal_number)
{
	(void)signal_number;
	if (!precious)
		unlink(output_name);
	_exit(1);
}

static void
version(void)
{
	fprintf(stderr, "Compress utility, Berkeley 5.9 5/11/86\n");
	fprintf(stderr, "Options: ");
#ifdef NO_UCHAR
	fprintf(stderr, "NO_UCHAR, ");
#endif
#ifdef COMPATIBLE
	fprintf(stderr, "COMPATIBLE, ");
#endif
#ifdef DEBUG
	fprintf(stderr, "DEBUG, ");
#endif
#ifdef BSD4_2
	fprintf(stderr, "BSD4_2, ");
#endif
	fprintf(stderr, "BITS = %d\n", BITS);
}

static void
write_error(void)
{
	perror(output_name);
	if (!precious)
		unlink(output_name);
	exit(1);
}

static _Noreturn void
corrupt_input(void)
{
	fprintf(stderr, "uncompress: corrupt input\n");
	if (!precious)
		unlink(output_name);
	exit(1);
}

static void
push_decompress_byte(char_type **stack_pointer, char_type value)
{
	if (*stack_pointer >= de_stack_end)
		corrupt_input();
	*(*stack_pointer)++ = value;
}

static void
clear_hash(count_int table_size)		/* reset code table */
{
	count_int table_index;

	for (table_index = 0; table_index < table_size; table_index++)
		htab[table_index] = -1;
}

static int offset;
static long int in_count = 1;		/* length of input */
static long int bytes_out;		/* length of compressed output */
static long int out_count;		/* number of codes output */

/*****************************************************************
 * TAG(output)
 *
 * Output the given code.
 * Inputs:
 * 	code:	A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *		that n_bits =< (long)wordsize - 1.
 * Outputs:
 * 	Outputs code to the file.
 * Assumptions:
 *	Chars are 8 bits long.
 * Algorithm:
 * 	Maintain a BITS character long buffer (so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */
static char_type buf[BITS];
static const char_type lmask[9] =
    { 0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00 };
static const char_type rmask[9] =
    { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

static void
output_code(code_int code)
{
#ifdef DEBUG
    static int col = 0;
#endif /* DEBUG */

    int bit_offset = offset;
    int bits = n_bits;
    char_type *buffer_pointer = buf;

#ifdef DEBUG
	if (verbose)
	    fprintf(stderr, "%5ld%c", (long int)code,
		    (col+=6) >= 74 ? (col = 0, '\n') : ' ');
#endif /* DEBUG */
    if (code >= 0) {
/*
 * byte/bit numbering on the VAX is simulated by the following code
 */
	/*
	 * Get to the first byte.
	 */
	buffer_pointer += bit_offset >> 3;
	bit_offset &= 7;
	/*
	 * Since code is always >= 8 bits, only need to mask the first
	 * hunk on the left.
	 */
	*buffer_pointer = (*buffer_pointer & rmask[bit_offset]) |
	    ((code << bit_offset) & lmask[bit_offset]);
	buffer_pointer++;
	bits -= 8 - bit_offset;
	code >>= 8 - bit_offset;
	/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
	if (bits >= 8) {
	    *buffer_pointer++ = code;
	    code >>= 8;
	    bits -= 8;
	}
	/* Last bits. */
	if (bits)
	    *buffer_pointer = code;
	offset += n_bits;
	if (offset == (n_bits << 3)) {
	    buffer_pointer = buf;
	    bits = n_bits;
	    bytes_out += bits;
	    do
		putchar(*buffer_pointer++);
	    while(--bits);
	    offset = 0;
	}

	/*
	 * If the next entry is going to be too big for the code size,
	 * then increase it, if possible.
	 */
	if (free_ent > maxcode || (clear_flg > 0))
	{
	    /*
	     * Write the whole buffer, because the input side won't
	     * discover the size increase until after it has read it.
	     */
	    if (offset > 0) {
		if (fwrite(buf, 1, (size_t)n_bits, stdout) !=
		    (size_t)n_bits)
			write_error();
		bytes_out += n_bits;
	    }
	    offset = 0;

	    if (clear_flg) {
    	        maxcode = MAXCODE (n_bits = INIT_BITS);
	        clear_flg = 0;
	    }
	    else {
	    	n_bits++;
	    	if (n_bits == maxbits)
		    maxcode = maxmaxcode;
	    	else
		    maxcode = MAXCODE(n_bits);
	    }
#ifdef DEBUG
	    if (debug) {
		fprintf(stderr, "\nChange to %d bits\n", n_bits);
		col = 0;
	    }
#endif /* DEBUG */
	}
    } else {
	/*
	 * At EOF, write the rest of the buffer.
	 */
	if (offset > 0 && fwrite(buf, 1, (size_t)((offset + 7) / 8),
	    stdout) != (size_t)((offset + 7) / 8))
		write_error();
	bytes_out += (offset + 7) / 8;
	offset = 0;
	fflush(stdout);
#ifdef DEBUG
	if (verbose)
	    fprintf(stderr, "\n");
#endif /* DEBUG */
	if (ferror(stdout))
		write_error();
    }
}

static void
print_ratio(FILE *stream, long int numerator, long int denominator)
{
    int quotient;			/* Does not need to be long. */

    if (numerator > 214748L) {	/* 2147483647/10000 */
        quotient = numerator / (denominator / 10000L);
    } else {
        quotient = 10000L * numerator / denominator;
    }
    if (quotient < 0) {
        putc('-', stream);
        quotient = -quotient;
    }
    fprintf(stream, "%d.%02d%%", quotient / 100, quotient % 100);
}

static void
clear_block(void)		/* table clear for block compress */
{
    long int current_ratio;

    checkpoint = in_count + CHECK_GAP;
#ifdef DEBUG
	if (debug) {
    		fprintf (stderr, "count: %ld, ratio: ", in_count);
		print_ratio(stderr, in_count, bytes_out);
		fprintf (stderr, "\n");
	}
#endif /* DEBUG */

    if (in_count > 0x007fffff) {	/* shift will overflow */
	current_ratio = bytes_out >> 8;
	if (current_ratio == 0) {	/* Do not divide by zero. */
	    current_ratio = 0x7fffffff;
	} else {
	    current_ratio = in_count / current_ratio;
	}
    } else {
	current_ratio = (in_count << 8) / bytes_out; /* 8 fractional bits */
    }
    if (current_ratio > ratio) {
	ratio = current_ratio;
    } else {
	ratio = 0;
#ifdef DEBUG
	if (verbose)
		dump_table();	/* dump string table */
#endif
	clear_hash((count_int)hsize);
	free_ent = FIRST;
	clear_flg = 1;
	output_code((code_int)CLEAR);
#ifdef DEBUG
	if (debug)
    		fprintf (stderr, "clear\n");
#endif /* DEBUG */
    }
}

/*
 * compress stdin to stdout
 *
 * Algorithm:  use open addressing double hashing (no chaining) on the
 * prefix code / next character combination.  We do a variant of Knuth's
 * algorithm D (vol. 3, sec. 6.4) along with G. Knott's relatively-prime
 * secondary probe.  Here, the modular division first probe is gives way
 * to a faster exclusive-or manipulation.  Also do block compression with
 * an adaptive reset, whereby the code table is cleared when the compression
 * ratio decreases, but after the table fills.  The variable-length output
 * codes are re-sized at this point, and a special CLEAR code is generated
 * for the decompressor.  Late addition:  construct the table according to
 * file size for noticeable speed improvement on small files.  Please direct
 * questions about this implementation to ames!jaw.
 */
static void
compress_input(void)
{
    long hash_key;
    code_int hash_index = 0;
    int input_byte;
    code_int current_code;
    int hash_step;
    code_int table_size;
    int hash_shift;

#ifndef COMPATIBLE
    if (nomagic == 0) {
	putchar(magic_header[0]); putchar(magic_header[1]);
	putchar((char)(maxbits | block_compress));
	if (ferror(stdout))
		write_error();
    }
#endif /* COMPATIBLE */

    offset = 0;
    bytes_out = 3;		/* includes 3-byte header mojo */
    out_count = 0;
    clear_flg = 0;
    ratio = 0;
    in_count = 1;
    checkpoint = CHECK_GAP;
    maxcode = MAXCODE(n_bits = INIT_BITS);
    free_ent = ((block_compress) ? FIRST : 256);

    current_code = getchar();

    hash_shift = 0;
    for (hash_key = (long int)hsize; hash_key < 65536L; hash_key *= 2L)
	hash_shift++;
    hash_shift = 8 - hash_shift;	/* set hash code range bound */

    table_size = hsize;
    clear_hash((count_int)table_size);

    while ((input_byte = getchar()) != EOF) {
	in_count++;
	hash_key = ((long int)input_byte << maxbits) + current_code;
	hash_index = (input_byte << hash_shift) ^ current_code;

	if (htabof(hash_index) == hash_key) {
	    current_code = codetabof(hash_index);
	    continue;
	} else if ((long int)htabof(hash_index) < 0) /* empty slot */
	    goto nomatch;
	hash_step = table_size - hash_index;
	if (hash_index == 0)
	    hash_step = 1;
probe:
	if ((hash_index -= hash_step) < 0)
	    hash_index += table_size;

	if (htabof(hash_index) == hash_key) {
	    current_code = codetabof(hash_index);
	    continue;
	}
	if ((long int)htabof(hash_index) > 0)
	    goto probe;
nomatch:
	output_code(current_code);
	out_count++;
	current_code = input_byte;
	if (free_ent < maxmaxcode) {
	    codetabof(hash_index) = free_ent++;
	    htabof(hash_index) = hash_key;
	}
	else if ((count_int)in_count >= checkpoint && block_compress)
	    clear_block();
    }
    /*
     * Put out the final code.
     */
    output_code(current_code);
    out_count++;
    output_code((code_int)-1);

    /*
     * Print out stats on stderr
     */
    if (zcat_flg == 0 && !quiet) {
#ifdef DEBUG
	fprintf(stderr,
		"%ld chars in, %ld codes (%ld bytes) out, compression factor: ",
		in_count, out_count, bytes_out);
	print_ratio(stderr, in_count, bytes_out);
	fprintf(stderr, "\n");
	fprintf(stderr, "\tCompression as in compact: ");
	print_ratio(stderr, in_count - bytes_out, in_count);
	fprintf(stderr, "\n");
	fprintf(stderr, "\tLargest code (of last block) was %ld (%d bits)\n",
		(long int)(free_ent - 1), n_bits);
#else /* !DEBUG */
	fprintf(stderr, "Compression: ");
	print_ratio(stderr, in_count - bytes_out, in_count);
#endif /* DEBUG */
    }
    if (bytes_out > in_count)	/* exit(2) if no savings */
	exit_stat = 2;
}

/*
 * Decompress stdin to stdout.  This routine adapts to the codes in the
 * file building the "string" table on-the-fly; requiring no table to
 * be stored in the compressed file.  The tables used herein are shared
 * with those of the compress() routine.  See the definitions above.
 */
static void
decompress_input(void)
{
    char_type *stack_pointer;
    int final_character;
    code_int code;
    code_int previous_code;
    code_int input_code;
    code_int prefix_code;

    /*
     * As above, initialize the first 256 entries in the table.
     */
    maxcode = MAXCODE(n_bits = INIT_BITS);
    for (code = 255; code >= 0; code--) {
	tab_prefixof(code) = 0;
	tab_suffixof(code) = (char_type)code;
    }
    free_ent = ((block_compress) ? FIRST : 256);

    final_character = previous_code = get_code();
    if (previous_code == -1)	/* EOF already? */
	return;			/* Get out of here */
    if (previous_code > 255)
	corrupt_input();
    putchar((char)final_character);
    if (ferror(stdout))		/* Crash if can't write */
	write_error();
    stack_pointer = de_stack;

    while ((code = get_code()) > -1) {

	if ((code == CLEAR) && block_compress) {
	    for (code = 255; code >= 0; code--)
		tab_prefixof(code) = 0;
	    clear_flg = 1;
	    free_ent = FIRST - 1;
	    if ((code = get_code()) == -1)
		break;
	}
	input_code = code;
	/*
	 * Special case for KwKwK string.
	 */
	if (code > free_ent)
	    corrupt_input();
	if (code == free_ent) {
	    push_decompress_byte(&stack_pointer,
		(char_type)final_character);
	    code = previous_code;
	}

	/*
	 * Generate output characters in reverse order
	 */
	while (code >= 256) {
	    if (code >= free_ent)
		corrupt_input();
	    push_decompress_byte(&stack_pointer, tab_suffixof(code));
	    prefix_code = tab_prefixof(code);
	    if (prefix_code >= code)
		corrupt_input();
	    code = prefix_code;
	}
	final_character = tab_suffixof(code);
	push_decompress_byte(&stack_pointer, (char_type)final_character);

	/*
	 * And put them out in forward order
	 */
	do
	    putchar(*--stack_pointer);
	while (stack_pointer > de_stack);

	/*
	 * Generate the new entry.
	 */
	if ((code=free_ent) < maxmaxcode) {
	    tab_prefixof(code) = (unsigned short)previous_code;
	    tab_suffixof(code) = final_character;
	    free_ent = code + 1;
	}
	/*
	 * Remember previous code.
	 */
	previous_code = input_code;
    }
    fflush(stdout);
    if (ferror(stdout))
	write_error();
}

static void
copy_stats(const char *input_name, const char *destination_name)
{
    struct stat statbuf;
    mode_t mode;
    struct timeval timep[2];

    if (fclose(stdout) == EOF) {
	perror(destination_name);
	perm_stat = 1;
	goto remove_destination;
    }
    if (stat(input_name, &statbuf)) {	/* Get stat on input file */
	perror(input_name);
	perm_stat = 1;
	goto remove_destination;
    }
    if (!S_ISREG(statbuf.st_mode)) {
	if (quiet)
		fprintf(stderr, "%s: ", input_name);
	fprintf(stderr, " -- not a regular file: unchanged");
	exit_stat = 1;
	perm_stat = 1;
    } else if (statbuf.st_nlink > 1) {
	if (quiet)
		fprintf(stderr, "%s: ", input_name);
	fprintf(stderr, " -- has %lu other links: unchanged",
		(unsigned long)statbuf.st_nlink - 1UL);
	exit_stat = 1;
	perm_stat = 1;
    } else if (exit_stat == 2 && (!force)) { /* No compression: remove file.Z */
	if (!quiet)
		fprintf(stderr, " -- file unchanged");
    } else {			/* ***** Successful Compression ***** */
	exit_stat = 0;
	mode = statbuf.st_mode & 07777;
	if (chmod(destination_name, mode)) {	/* Copy modes */
	    perror(destination_name);
	    goto metadata_failure;
	}
	if (chown(destination_name, statbuf.st_uid, statbuf.st_gid)) {
	    perror(destination_name);
	    goto metadata_failure;
	}
	timep[0].tv_sec = statbuf.st_atime;
	timep[0].tv_usec = 0;
	timep[1].tv_sec = statbuf.st_mtime;
	timep[1].tv_usec = 0;
	if (utimes(destination_name, timep)) {
	    perror(destination_name);
	    goto metadata_failure;
	}
	if (unlink(input_name)) {
	    perror(input_name);
	    perm_stat = 1;
	    return;
	}
	if (!quiet)
		fprintf(stderr, " -- replaced with %s", destination_name);
	return;		/* Successful return */
    }

    /* Unsuccessful return -- one of the tests failed */
remove_destination:
    if (unlink(destination_name)) {
	perror(destination_name);
    }
    return;

metadata_failure:
    perm_stat = 1;
    goto remove_destination;
}

static int
has_z_suffix(const char *name)
{
	size_t name_length = strlen(name);

	return name_length >= 2 && name[name_length - 2] == '.' &&
	    name[name_length - 1] == 'Z';
}

static int
make_compressed_name(char *destination, size_t capacity, const char *source)
{
	size_t source_length = strlen(source);

	if (capacity < 3 || source_length > capacity - 3)
		return -1;
	memcpy(destination, source, source_length);
	destination[source_length] = '.';
	destination[source_length + 1] = 'Z';
	destination[source_length + 2] = '\0';
	return 0;
}

static int
make_decompressed_name(char *destination, size_t capacity, const char *source)
{
	size_t source_length = strlen(source);
	size_t output_length;

	if (!has_z_suffix(source))
		return -1;
	output_length = source_length - 2;
	if (output_length >= capacity)
		return -1;
	memcpy(destination, source, output_length);
	destination[output_length] = '\0';
	return 0;
}

static int
set_decompression_parameters(int header_byte, const char *input_name)
{
	int header_maxbits;

	if (header_byte == EOF) {
		fprintf(stderr, "%s: truncated compressed header\n", input_name);
		return -1;
	}
	header_maxbits = header_byte & BIT_MASK;
	if (header_maxbits < INIT_BITS || header_maxbits > BITS) {
		fprintf(stderr,
		    "%s: compressed with %d bits, can only handle %d through %d bits\n",
		    input_name, header_maxbits, INIT_BITS, BITS);
		return -1;
	}
	block_compress = header_byte & BLOCK_MASK;
	maxbits = header_maxbits;
	maxmaxcode = 1 << maxbits;
	return 0;
}

/*****************************************************************
 * TAG(main)
 *
 * Algorithm from "A Technique for High Performance Data Compression",
 * Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.
 *
 * Usage: compress [-dfvc] [-b bits] [file ...]
 * Inputs:
 *	-d:	    If given, decompression is done instead.
 *
 *      -c:         Write output on stdout, don't remove original.
 *
 *      -b:         Parameter limits the max number of bits/code.
 *
 *	-f:	    Forces output file to be generated, even if one already
 *		    exists, and even if no space is saved by compressing.
 *		    If -f is not used, the user will be prompted if stdin is
 *		    a tty, otherwise, the output file will not be overwritten.
 *
 *      -v:	    Write compression statistics
 *
 * 	file ...:   Files to be compressed.  If none specified, stdin
 *		    is used.
 * Outputs:
 *	file.Z:	    Compressed form of file with same mode, owner, and utimes
 * 	or stdout   (if stdin used as input)
 *
 * Assumptions:
 *	When filenames are given, replaces with the compressed version
 *	(.Z suffix) only if the file decreases in size.
 * Algorithm:
 * 	Modified Lempel-Ziv method (LZW).  Basically finds common
 * substrings and replaces them with a variable size code.  This is
 * deterministic, and can be done on the fly.  Thus, the decompression
 * procedure needs no input table, but tracks the way the table was built.
 */
int
main(int argc, char **argv)
{
    int overwrite = 0;	/* Do not overwrite unless given -f flag */
    char input_name[100];
    char **file_list;
    char **file_pointer;
    char *base_name;
    struct stat statbuf;

    /* This bg check only works for sh. */
    previous_interrupt = signal(SIGINT, SIG_IGN);
    if (previous_interrupt != SIG_IGN) {
	signal(SIGINT, on_interrupt);
    }
    background_flag = previous_interrupt != SIG_DFL;
#ifdef notdef     /* This works for csh but we don't want it. */
    { int tgrp;
    if (background_flag == 0 && ioctl(2, TIOCGPGRP, (char *)&tgrp) == 0 &&
      getpgrp(0) != tgrp)
	background_flag = 1;
    }
#endif

#ifdef COMPATIBLE
    nomagic = 1;	/* Original didn't have a magic number */
#endif /* COMPATIBLE */

    file_list = file_pointer = malloc((size_t)argc * sizeof(*argv));
    if (file_list == NULL) {
	fprintf(stderr, "compress: cannot allocate the file list\n");
	return 1;
    }
    *file_list = NULL;

    if ((base_name = strrchr(argv[0], '/')) != NULL) {
	base_name++;
    } else {
	base_name = argv[0];
    }
    if (strcmp(base_name, "uncompress") == 0) {
	decompress_mode = 1;
    } else if (strcmp(base_name, "zcat") == 0) {
	decompress_mode = 1;
	zcat_flg = 1;
    }

#ifdef BSD4_2
    /* 4.2BSD dependent - take it out if not */
    setlinebuf(stderr);
#endif /* BSD4_2 */

    /* Argument Processing
     * All flags are optional.
     * -D => debug
     * -V => print Version; debug verbose
     * -d => do_decomp
     * -v => unquiet
     * -f => force overwrite of output file
     * -n => no header: useful to uncompress old files
     * -b maxbits => maxbits.  If -b is specified, then maxbits MUST be
     *	    given also.
     * -c => cat all output to stdout
     * -C => generate output compatible with compress 2.0.
     * if a string is left, must be an input filename.
     */
    for (argc--, argv++; argc > 0; argc--, argv++) {
	if (**argv == '-') {	/* A flag argument */
	    while (*++(*argv)) {	/* Process all flags in this arg */
		switch (**argv) {
#ifdef DEBUG
		    case 'D':
			debug = 1;
			break;
		    case 'V':
			verbose = 1;
			version();
			break;
#else
		    case 'V':
			version();
			break;
#endif /* DEBUG */
		    case 'v':
			quiet = 0;
			break;
		    case 'd':
			decompress_mode = 1;
			break;
		    case 'f':
		    case 'F':
			overwrite = 1;
			force = 1;
			break;
		    case 'n':
			nomagic = 1;
			break;
		    case 'C':
			block_compress = 0;
			break;
		    case 'b':
			if (!ARGVAL()) {
			    fprintf(stderr, "Missing maxbits\n");
			    usage();
			    exit(1);
			}
			if (parse_maxbits(*argv, &maxbits) != 0) {
				fprintf(stderr,
				    "Maxbits must be an unsigned decimal integer\n");
				usage();
				exit(1);
			}
			goto nextarg;
		    case 'c':
			zcat_flg = 1;
			break;
		    case 'q':
			quiet = 1;
			break;
		    default:
			fprintf(stderr, "Unknown flag: '%c'; ", **argv);
			usage();
			exit(1);
		}
	    }
	}
	else {		/* Input file name */
	    *file_pointer++ = *argv;	/* Build input file list */
	    *file_pointer = NULL;
	    /* process nextarg; */
	}
	nextarg: continue;
    }

    if (maxbits < INIT_BITS) maxbits = INIT_BITS;
    if (maxbits > BITS) maxbits = BITS;
    maxmaxcode = 1 << maxbits;

    if (*file_list != NULL) {
	for (file_pointer = file_list; *file_pointer; file_pointer++) {
	    exit_stat = 0;
	    if (decompress_mode) {		/* DECOMPRESSION */
		/* Check for .Z suffix */
		if (!has_z_suffix(*file_pointer)) {
		    /* No .Z: tack one on */
		    if (make_compressed_name(input_name, sizeof(input_name),
			*file_pointer) != 0) {
			fprintf(stderr, "%s: filename is too long\n",
			    *file_pointer);
			perm_stat = 1;
			continue;
		    }
		    *file_pointer = input_name;
		}
		/* Open input file */
		if (freopen(*file_pointer, "r", stdin) == NULL) {
		    perror(*file_pointer);
		    perm_stat = 1;
		    continue;
		}
		/* Check the magic number */
		if (nomagic == 0) {
		    if ((getchar() != (magic_header[0] & 0xFF))
		     || (getchar() != (magic_header[1] & 0xFF))) {
			fprintf(stderr, "%s: not in compressed format\n",
			    *file_pointer);
			perm_stat = 1;
		    continue;
		    }
		    if (set_decompression_parameters(getchar(),
			*file_pointer) != 0) {
			perm_stat = 1;
			continue;
		    }
		}
		if (zcat_flg == 0) {
		    /* Generate output filename */
		    if (make_decompressed_name(output_name, sizeof(output_name),
			*file_pointer) != 0) {
			fprintf(stderr, "%s: output filename is too long\n",
			    *file_pointer);
			perm_stat = 1;
			continue;
		    }
		}
	    } else {					/* COMPRESSION */
		if (has_z_suffix(*file_pointer)) {
		    	fprintf(stderr, "%s: already has .Z suffix -- no change\n",
			    *file_pointer);
		    continue;
		}
		/* Open input file */
		if (freopen(*file_pointer, "r", stdin) == NULL) {
		    perror(*file_pointer);
		    perm_stat = 1;
		    continue;
		}
		if (stat(*file_pointer, &statbuf) != 0) {
		    perror(*file_pointer);
		    perm_stat = 1;
		    continue;
		}
		fsize = (long) statbuf.st_size;
		/*
		 * tune hash table size for small files -- ad hoc,
		 * but the sizes match earlier #defines, which
		 * serve as upper bounds on the number of output codes.
		 */
		hsize = HSIZE;
		if (fsize < (1 << 12))
		    hsize = MINIMUM(5003, HSIZE);
		else if (fsize < (1 << 13))
		    hsize = MINIMUM(9001, HSIZE);
		else if (fsize < (1 << 14))
		    hsize = MINIMUM(18013, HSIZE);
		else if (fsize < (1 << 15))
		    hsize = MINIMUM(35023, HSIZE);
		else if (fsize < 47000)
		    hsize = MINIMUM(50021, HSIZE);

		if (zcat_flg == 0) {
		    /* Generate output filename */
		    if (make_compressed_name(output_name, sizeof(output_name),
			*file_pointer) != 0) {
			fprintf(stderr, "%s: output filename is too long\n",
			    *file_pointer);
			perm_stat = 1;
			continue;
		    }
#ifndef BSD4_2		/* Short filenames */
		    if ((base_name = strrchr(output_name, '/')) != NULL)
			base_name++;
		    else
			base_name = output_name;
		    if (strlen(base_name) > 14) {
			fprintf(stderr, "%s: filename too long to tack on .Z\n",
			    base_name);
			continue;
		    }
#endif  /* BSD4_2		Long filenames allowed */
		}
	    }
	    /* Check for overwrite of existing file */
	    if (overwrite == 0 && zcat_flg == 0) {
		if (stat(output_name, &statbuf) == 0) {
		    char response[2];
		    ssize_t response_length;
		    char response_tail;

		    response[0] = 'n';
		    response[1] = '\n';
		    fprintf(stderr, "%s already exists;", output_name);
		    if (background_flag == 0 && isatty(2)) {
			fprintf(stderr, " do you wish to overwrite %s (y or n)? ",
			output_name);
			fflush(stderr);
			response_length = read(2, response, sizeof(response));
			if (response_length < 0) {
			    perror("stderr");
			    response[0] = 'n';
			}
			while (response_length == 2 && response[1] != '\n') {
			    response_length = read(2, &response_tail, 1);
			    if (response_length <= 0 || response_tail == '\n')
				break;
			}
		    }
		    if (response[0] != 'y') {
			fprintf(stderr, "\tnot overwritten\n");
			continue;
		    }
		}
	    }
	    if (zcat_flg == 0) {		/* Open output file */
		if (freopen(output_name, "w", stdout) == NULL) {
		    perror(output_name);
		    perm_stat = 1;
		    continue;
		}
		precious = 0;
		if (!quiet)
			fprintf(stderr, "%s: ", *file_pointer);
	    }

	    /* Actually do the compression/decompression */
	    if (decompress_mode == 0)	compress_input();
#ifndef DEBUG
	    else			decompress_input();
#else
	    else if (debug == 0)	decompress_input();
	    else			print_codes();
	    if (verbose)		dump_table();
#endif /* DEBUG */
	    if (zcat_flg == 0) {
		copy_stats(*file_pointer, output_name);
		precious = 1;
		if ((exit_stat == 1) || (!quiet))
			putc('\n', stderr);
	    }
	}
    } else {		/* Standard input */
	if (decompress_mode == 0) {
		compress_input();
#ifdef DEBUG
		if (verbose)		dump_table();
#endif /* DEBUG */
		if (!quiet)
			putc('\n', stderr);
	} else {
	    /* Check the magic number */
	    if (nomagic == 0) {
		if ((getchar()!=(magic_header[0] & 0xFF))
		 || (getchar()!=(magic_header[1] & 0xFF))) {
		    fprintf(stderr, "stdin: not in compressed format\n");
		    exit(1);
		}
		fsize = 100000;		/* assume stdin large for USERMEM */
		if (set_decompression_parameters(getchar(), "stdin") != 0)
			exit(1);
	    }
#ifndef DEBUG
	    decompress_input();
#else
	    if (debug == 0)	decompress_input();
	    else		print_codes();
	    if (verbose)	dump_table();
#endif /* DEBUG */
	}
    }
    free(file_list);
    return perm_stat ? perm_stat : exit_stat;
}

/*****************************************************************
 * TAG(getcode)
 *
 * Read one code from the standard input.  If EOF, return -1.
 * Inputs:
 * 	stdin
 * Outputs:
 * 	code or -1 is returned.
 */

static code_int
get_code(void)
{
    code_int code;
    static int offset = 0, size = 0;
    static char_type buf[BITS];
    int bit_offset;
    int bits;
    char_type *buffer_pointer = buf;

    if (clear_flg > 0 || offset >= size || free_ent > maxcode) {
	/*
	 * If the next entry will be too big for the current code
	 * size, then we must increase the size.  This implies reading
	 * a new buffer full, too.
	 */
	if (free_ent > maxcode) {
	    n_bits++;
	    if (n_bits == maxbits)
		maxcode = maxmaxcode;	/* won't get any bigger now */
	    else
		maxcode = MAXCODE(n_bits);
	}
	if (clear_flg > 0) {
    	    maxcode = MAXCODE (n_bits = INIT_BITS);
	    clear_flg = 0;
	}
	size = fread(buf, 1, n_bits, stdin);
	if (size <= 0)
	    return -1;			/* end of file */
	offset = 0;
	/* Round size down to integral number of codes */
	size = (size << 3) - (n_bits - 1);
    }
    bit_offset = offset;
    bits = n_bits;
	/*
	 * Get to the first byte.
	 */
	buffer_pointer += bit_offset >> 3;
	bit_offset &= 7;
	/* Get first part (low order bits) */
#ifdef NO_UCHAR
	code = ((*buffer_pointer++ >> bit_offset) &
	    rmask[8 - bit_offset]) & 0xff;
#else
	code = *buffer_pointer++ >> bit_offset;
#endif /* NO_UCHAR */
	bits -= 8 - bit_offset;
	bit_offset = 8 - bit_offset;
	/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
	if (bits >= 8) {
#ifdef NO_UCHAR
	    code |= (*buffer_pointer++ & 0xff) << bit_offset;
#else
	    code |= *buffer_pointer++ << bit_offset;
#endif /* NO_UCHAR */
	    bit_offset += 8;
	    bits -= 8;
	}
	/* high order bits. */
	code |= (*buffer_pointer & rmask[bits]) << bit_offset;
    offset += n_bits;

    return code;
}
#ifdef DEBUG
static void
print_codes(void)
{
    /*
     * Just print out codes from input file.  For debugging.
     */
    code_int code;
    int col = 0, bits;

    bits = n_bits = INIT_BITS;
    maxcode = MAXCODE(n_bits);
    free_ent = ((block_compress) ? FIRST : 256);
    while ((code = get_code()) >= 0) {
	if ((code == CLEAR) && block_compress) {
   	    free_ent = FIRST - 1;
   	    clear_flg = 1;
	}
	else if (free_ent < maxmaxcode)
	    free_ent++;
	if (bits != n_bits) {
	    fprintf(stderr, "\nChange to %d bits\n", n_bits);
	    bits = n_bits;
	    col = 0;
	}
	fprintf(stderr, "%5ld%c", (long int)code,
	    (col+=6) >= 74 ? (col = 0, '\n') : ' ');
    }
    putc('\n', stderr);
    exit(0);
}

static code_int sorttab[1 << BITS];	/* sorted pointers into htab */

static void
dump_table(void)	/* dump string table */
{
    int index;
    int first;
    int entry;
#define STACK_SIZE	15000
    int stack_top = STACK_SIZE;
    int character;

    if (decompress_mode == 0) {	/* compressing */
	for (index = 0; index < hsize; index++) {
		if ((long int)htabof(index) >= 0) {
			sorttab[codetabof(index)] = index;
		}
	}
	first = block_compress ? FIRST : 256;
	for (index = first; index < free_ent; index++) {
		fprintf(stderr, "%5d: \"", index);
		de_stack[--stack_top] = '\n';
		de_stack[--stack_top] = '"';
		stack_top = push_debug_byte(
		    (htabof(sorttab[index]) >> maxbits) & 0xff,
                                     stack_top);
		for (entry = htabof(sorttab[index]) & ((1 << maxbits) - 1);
		    entry > 256;
		    entry = htabof(sorttab[entry]) & ((1 << maxbits) - 1)) {
			stack_top = push_debug_byte(
			    htabof(sorttab[entry]) >> maxbits,
						stack_top);
		}
		stack_top = push_debug_byte(entry, stack_top);
		fwrite(&de_stack[stack_top], 1,
		    (size_t)(STACK_SIZE - stack_top), stderr);
	   	stack_top = STACK_SIZE;
	}
   } else if (!debug) {	/* decompressing */

	for (index = 0; index < free_ent; index++) {
	   entry = index;
	   character = tab_suffixof(entry);
	   if ((unsigned int)character <= 0x7fU && isprint(character))
	       fprintf(stderr, "%5d: %5d/'%c'  \"",
			   entry, tab_prefixof(entry), character);
	   else
	       fprintf(stderr, "%5d: %5d/\\%03o \"",
			   entry, tab_prefixof(entry), character);
	   de_stack[--stack_top] = '\n';
	   de_stack[--stack_top] = '"';
	   for (; entry != 0;
		   entry = entry >= FIRST ? tab_prefixof(entry) : 0) {
	       stack_top = push_debug_byte(tab_suffixof(entry), stack_top);
	   }
	   fwrite(&de_stack[stack_top], 1,
	       (size_t)(STACK_SIZE - stack_top), stderr);
	   stack_top = STACK_SIZE;
	}
    }
}

static int
push_debug_byte(int character, int stack_top)
{
	if (((unsigned int)character <= 0x7fU && isprint(character) &&
	    character != '\\') || character == ' ') {
	    de_stack[--stack_top] = character;
	} else {
	    switch (character) {
	    case '\n': de_stack[--stack_top] = 'n'; break;
	    case '\t': de_stack[--stack_top] = 't'; break;
	    case '\b': de_stack[--stack_top] = 'b'; break;
	    case '\f': de_stack[--stack_top] = 'f'; break;
	    case '\r': de_stack[--stack_top] = 'r'; break;
	    case '\\': de_stack[--stack_top] = '\\'; break;
	    default:
		de_stack[--stack_top] = '0' + character % 8;
		de_stack[--stack_top] = '0' + (character / 8) % 8;
		de_stack[--stack_top] = '0' + character / 64;
	 	break;
	    }
	    de_stack[--stack_top] = '\\';
	}
	return stack_top;
}
#endif /* DEBUG */
