#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int colrm_parse_column(const char *, unsigned long *);
int colrm_next_column(unsigned long *, int);
int colrm_filter(FILE *, FILE *, unsigned long, unsigned long);
int colrm_program_main(int, char **);

static int failures;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "colrm contract: %s\n", message);
		failures++;
	}
}

static void
check_filter(const unsigned char *input, size_t input_length,
	unsigned long start, unsigned long stop,
	const unsigned char *expected, size_t expected_length,
	const char *message)
{
	unsigned char output_bytes[128];
	FILE *input_stream = tmpfile();
	FILE *output_stream = tmpfile();
	size_t output_length;

	check(input_stream != NULL && output_stream != NULL, "tmpfile failed");
	if (input_stream == NULL || output_stream == NULL)
		return;
	check(fwrite(input, 1, input_length, input_stream) == input_length,
	    "fixture write failed");
	rewind(input_stream);
	check(colrm_filter(input_stream, output_stream, start, stop) == 0,
	    message);
	rewind(output_stream);
	output_length = fread(output_bytes, 1, sizeof(output_bytes), output_stream);
	check(output_length == expected_length &&
	    memcmp(output_bytes, expected, expected_length) == 0, message);
	(void)fclose(input_stream);
	(void)fclose(output_stream);
}

static void
check_filter_contract(void)
{
	static const unsigned char text[] = "abcdef\n";
	static const unsigned char delete_tail[] = "ab\n";
	static const unsigned char delete_range[] = "abf\n";
	static const unsigned char delete_one[] = "abdef\n";
	static const unsigned char unterminated[] = "xyz";
	static const unsigned char unterminated_expected[] = "xz";
	static const unsigned char lines[] = "abcd\nwxyz\n";
	static const unsigned char lines_expected[] = "acd\nwyz\n";
	static const unsigned char tab_input[] = "a\tb\n";
	static const unsigned char tab_expected[] = "ab\n";
	static const unsigned char backspace_input[] = "abcd\bX\n";
	static const unsigned char backspace_expected[] = "abc\b\n";
	static const unsigned char floor_input[] = "\bA\n";
	static const unsigned char floor_expected[] = "\b\n";
	static const unsigned char bytes_input[] = { 0x00, 0x80, 0xff, '\n' };
	static const unsigned char bytes_expected[] = { 0x00, '\n' };

	check_filter(text, sizeof(text) - 1, 0, 0, text, sizeof(text) - 1,
	    "copy mode changed bytes");
	check_filter(text, sizeof(text) - 1, 3, 0, delete_tail,
	    sizeof(delete_tail) - 1, "open-ended removal failed");
	check_filter(text, sizeof(text) - 1, 3, 5, delete_range,
	    sizeof(delete_range) - 1, "inclusive range removal failed");
	check_filter(text, sizeof(text) - 1, 3, 3, delete_one,
	    sizeof(delete_one) - 1, "single-column removal failed");
	check_filter(unterminated, sizeof(unterminated) - 1, 2, 2,
	    unterminated_expected, sizeof(unterminated_expected) - 1,
	    "unterminated input failed");
	check_filter(lines, sizeof(lines) - 1, 2, 2,
	    lines_expected, sizeof(lines_expected) - 1,
	    "newline reset failed");
	check_filter(tab_input, sizeof(tab_input) - 1, 4, 8, tab_expected,
	    sizeof(tab_expected) - 1, "tab boundary failed");
	check_filter(backspace_input, sizeof(backspace_input) - 1, 4, 4,
	    backspace_expected, sizeof(backspace_expected) - 1,
	    "backspace range transition failed");
	check_filter(floor_input, sizeof(floor_input) - 1, 1, 1,
	    floor_expected, sizeof(floor_expected) - 1,
	    "backspace floor failed");
	check_filter(bytes_input, sizeof(bytes_input), 2, 3, bytes_expected,
	    sizeof(bytes_expected), "binary bytes changed");
}

static void
check_parsing_and_overflow(void)
{
	unsigned long column;
	unsigned long value = 99;
	char overflow[64];
	char *bad_range[] = { (char *)"colrm", (char *)"5", (char *)"4" };
	char *too_many[] = { (char *)"colrm", (char *)"1", (char *)"2",
	    (char *)"3" };

	check(colrm_parse_column("1", &value) == 0 && value == 1,
	    "one did not parse");
	check(colrm_parse_column("0007", &value) == 0 && value == 7,
	    "leading zeros did not parse");
	check(colrm_parse_column("", &value) != 0, "empty value parsed");
	check(colrm_parse_column("0", &value) != 0, "zero parsed");
	check(colrm_parse_column("+1", &value) != 0, "plus sign parsed");
	check(colrm_parse_column("-1", &value) != 0, "minus sign parsed");
	check(colrm_parse_column("1x", &value) != 0, "suffix parsed");
	(void)snprintf(overflow, sizeof(overflow), "%lu0", ULONG_MAX);
	check(colrm_parse_column(overflow, &value) != 0, "overflow parsed");

	column = ULONG_MAX;
	check(colrm_next_column(&column, 'x') != 0 && column == ULONG_MAX,
	    "ordinary-column overflow passed");
	column = ULONG_MAX - 3UL;
	check(colrm_next_column(&column, '\t') != 0,
	    "tab-column overflow passed");
	column = 0;
	check(colrm_next_column(&column, '\b') == 0 && column == 0,
	    "backspace crossed zero");
	check(colrm_program_main(3, bad_range) == 1,
	    "reversed range passed");
	check(colrm_program_main(4, too_many) == 1,
	    "extra argument passed");
}

static void
check_io_errors(void)
{
	FILE *input_stream = tmpfile();
	FILE *output_stream = tmpfile();
	FILE *write_input = tmpfile();
	FILE *write_output = tmpfile();
	FILE *flush_input = tmpfile();
	FILE *flush_output = tmpfile();

	check(input_stream != NULL && output_stream != NULL &&
	    write_input != NULL && write_output != NULL &&
	    flush_input != NULL && flush_output != NULL,
	    "I/O fixture creation failed");
	if (input_stream == NULL || output_stream == NULL ||
	    write_input == NULL || write_output == NULL ||
	    flush_input == NULL || flush_output == NULL)
		return;
	check(close(fileno(input_stream)) == 0, "input close failed");
	check(colrm_filter(input_stream, output_stream, 0, 0) == 1,
	    "read error passed");

	check(fputc('x', write_input) == 'x', "write fixture write failed");
	rewind(write_input);
	check(setvbuf(write_output, NULL, _IONBF, 0) == 0,
	    "unbuffered output setup failed");
	check(close(fileno(write_output)) == 0, "write output close failed");
	check(colrm_filter(write_input, write_output, 0, 0) == 1,
	    "write error passed");

	check(fputc('x', flush_input) == 'x', "flush fixture write failed");
	rewind(flush_input);
	check(setvbuf(flush_output, NULL, _IOFBF, BUFSIZ) == 0,
	    "output buffering failed");
	check(close(fileno(flush_output)) == 0, "output close failed");
	check(colrm_filter(flush_input, flush_output, 0, 0) == 1,
	    "final flush error passed");
}

int
main(void)
{
	check_filter_contract();
	check_parsing_and_overflow();
	check_io_errors();
	if (failures != 0)
		return 1;
	puts("colrm contract tests passed");
	return 0;
}
