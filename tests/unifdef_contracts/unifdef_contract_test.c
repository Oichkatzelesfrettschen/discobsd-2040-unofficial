#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UNIFDEF_MAX_DEPTH 64
#define UNIFDEF_MAX_LINE 4096
#define UNIFDEF_MAX_SYMBOLS 100

void unifdef_reset(void);
int unifdef_add_symbol(const char *, int, int);
int unifdef_process(FILE *, FILE *, const char *);

static int failures;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "unifdef contract: %s\n", message);
		failures++;
	}
}

static void
check_case(const unsigned char *input, size_t input_length,
	const unsigned char *expected, size_t expected_length,
	int expected_status, const char *message)
{
	unsigned char output_bytes[16384];
	FILE *input_stream = tmpfile();
	FILE *output_stream = tmpfile();
	size_t output_length;
	int status;

	check(input_stream != NULL && output_stream != NULL, "tmpfile failed");
	if (input_stream == NULL || output_stream == NULL)
		return;
	check(fwrite(input, 1, input_length, input_stream) == input_length,
	    "fixture write failed");
	rewind(input_stream);
	unifdef_reset();
	check(unifdef_add_symbol("KNOWN", 1, 0) == 0,
	    "fixture symbol rejected");
	status = unifdef_process(input_stream, output_stream, "fixture.c");
	check(status == expected_status, message);
	rewind(output_stream);
	output_length = fread(output_bytes, 1, sizeof(output_bytes), output_stream);
	check(output_length == expected_length &&
	    memcmp(output_bytes, expected, expected_length) == 0, message);
	(void)fclose(input_stream);
	(void)fclose(output_stream);
}

static void
check_filtering(void)
{
	static const unsigned char unchanged[] =
	    "#ifdef OTHER\nother\n#endif\n";
	static const unsigned char selected[] =
	    "#ifdef KNOWN\nyes\n#else\nno\n#endif\n";
	static const unsigned char selected_expected[] = "yes\n";
	static const unsigned char nested[] =
	    "#ifdef KNOWN\n#ifdef KNOWN\ninner\n#endif\n#endif\n";
	static const unsigned char nested_expected[] =
	    "#ifdef KNOWN\ninner\n#endif\n";
	static const unsigned char lexical[] =
	    "/*\n#ifdef KNOWN\n*/\n\"#ifdef KNOWN\"\n";
	static const unsigned char final_line[] =
	    "#ifdef KNOWN\nlast\n#endif";
	static const unsigned char final_expected[] = "last\n";
	static const unsigned char rejected_lexical[] =
	    "#ifndef KNOWN\n/* ignored opener\n#else\nafter\n#endif\n";
	static const unsigned char binary[] = {
	    'a', 0, 'b', '\n'
	};

	check_case(unchanged, sizeof(unchanged) - 1, unchanged,
	    sizeof(unchanged) - 1, 0, "unknown conditional changed");
	check_case(selected, sizeof(selected) - 1, selected_expected,
	    sizeof(selected_expected) - 1, 1, "known conditional selection failed");
	check_case(nested, sizeof(nested) - 1, nested_expected,
	    sizeof(nested_expected) - 1, 1, "active-symbol nesting failed");
	check_case(lexical, sizeof(lexical) - 1, lexical,
	    sizeof(lexical) - 1, 0, "comment or string became a directive");
	check_case(final_line, sizeof(final_line) - 1, final_expected,
	    sizeof(final_expected) - 1, 1, "unterminated final line changed");
	check_case(rejected_lexical, sizeof(rejected_lexical) - 1,
	    (const unsigned char *)"", 0, 2,
	    "nonignored rejected text bypassed lexical validation");
	check_case(binary, sizeof(binary), binary, sizeof(binary), 0,
	    "binary ordinary line changed");
}

static void
check_error_boundaries(void)
{
	static const unsigned char elif_input[] =
	    "#ifdef KNOWN\nyes\n#elif OTHER\nno\n#endif\n";
	static const unsigned char duplicate_else[] =
	    "#ifdef KNOWN\nyes\n#else\nno\n#else\nmore\n#endif\n";
	static const unsigned char missing_endif[] = "#ifdef KNOWN\nyes\n";
	static const unsigned char open_comment[] = "/* open\n";
	unsigned char long_line[UNIFDEF_MAX_LINE + 1];
	unsigned char exact_splice[UNIFDEF_MAX_LINE];
	unsigned char exact_depth[UNIFDEF_MAX_DEPTH * 13];
	unsigned char deep_input[(UNIFDEF_MAX_DEPTH + 1) * 6];
	size_t input_length = 0;
	size_t exact_length = 0;
	int depth;

	check_case(elif_input, sizeof(elif_input) - 1,
	    (const unsigned char *)"yes\n", 4, 2, "#elif was accepted");
	check_case(duplicate_else, sizeof(duplicate_else) - 1,
	    (const unsigned char *)"yes\n", 4, 2, "duplicate #else was accepted");
	check_case(missing_endif, sizeof(missing_endif) - 1,
	    (const unsigned char *)"yes\n", 4, 2, "missing #endif was accepted");
	check_case(open_comment, sizeof(open_comment) - 1, open_comment,
	    sizeof(open_comment) - 1, 2, "open comment was accepted");
	memset(long_line, 'x', sizeof(long_line));
	check_case(long_line, UNIFDEF_MAX_LINE, long_line, UNIFDEF_MAX_LINE,
	    0, "4096-byte line was rejected");
	check_case(long_line, sizeof(long_line), (const unsigned char *)"", 0,
	    2, "overlong line was accepted");
	memset(exact_splice, 'x', sizeof(exact_splice));
	exact_splice[sizeof(exact_splice) - 2] = '\\';
	exact_splice[sizeof(exact_splice) - 1] = '\n';
	check_case(exact_splice, sizeof(exact_splice), exact_splice,
	    sizeof(exact_splice), 0, "4096-byte spliced line did not terminate");
	for (depth = 0; depth < UNIFDEF_MAX_DEPTH; depth++) {
		memcpy(exact_depth + exact_length, "#if 1\n", 6);
		exact_length += 6;
	}
	for (depth = 0; depth < UNIFDEF_MAX_DEPTH; depth++) {
		memcpy(exact_depth + exact_length, "#endif\n", 7);
		exact_length += 7;
	}
	check_case(exact_depth, exact_length, exact_depth, exact_length, 0,
	    "64-level nesting was rejected");
	for (depth = 0; depth < UNIFDEF_MAX_DEPTH + 1; depth++) {
		memcpy(deep_input + input_length, "#if 1\n", 6);
		input_length += 6;
	}
	check_case(deep_input, input_length, deep_input,
	    UNIFDEF_MAX_DEPTH * 6, 2, "excess nesting was accepted");
}

static void
check_symbol_limit(void)
{
	char names[UNIFDEF_MAX_SYMBOLS + 1][8];
	int symbol_index;

	unifdef_reset();
	for (symbol_index = 0; symbol_index < UNIFDEF_MAX_SYMBOLS;
	    symbol_index++) {
		(void)snprintf(names[symbol_index], sizeof(names[symbol_index]),
		    "S%03d", symbol_index);
		check(unifdef_add_symbol(names[symbol_index], 1, 0) == 0,
		    "bounded symbol was rejected");
	}
	(void)snprintf(names[UNIFDEF_MAX_SYMBOLS],
	    sizeof(names[UNIFDEF_MAX_SYMBOLS]), "S100");
	check(unifdef_add_symbol(names[UNIFDEF_MAX_SYMBOLS], 1, 0) != 0,
	    "101st symbol was accepted");
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
	unifdef_reset();
	check(unifdef_add_symbol("KNOWN", 1, 0) == 0,
	    "I/O fixture symbol rejected");
	check(close(fileno(input_stream)) == 0, "input close failed");
	check(unifdef_process(input_stream, output_stream, "read.c") == 2,
	    "read error passed");

	check(fputc('x', write_input) == 'x', "write fixture failed");
	rewind(write_input);
	check(setvbuf(write_output, NULL, _IONBF, 0) == 0,
	    "unbuffered output setup failed");
	check(close(fileno(write_output)) == 0, "write output close failed");
	unifdef_reset();
	check(unifdef_add_symbol("KNOWN", 1, 0) == 0,
	    "write fixture symbol rejected");
	check(unifdef_process(write_input, write_output, "write.c") == 2,
	    "write error passed");

	check(fputc('x', flush_input) == 'x', "flush fixture failed");
	rewind(flush_input);
	check(setvbuf(flush_output, NULL, _IOFBF, BUFSIZ) == 0,
	    "buffered output setup failed");
	check(close(fileno(flush_output)) == 0, "flush output close failed");
	unifdef_reset();
	check(unifdef_add_symbol("KNOWN", 1, 0) == 0,
	    "flush fixture symbol rejected");
	check(unifdef_process(flush_input, flush_output, "flush.c") == 2,
	    "flush error passed");
}

int
main(void)
{
	check_filtering();
	check_error_boundaries();
	check_symbol_limit();
	check_io_errors();
	if (failures != 0)
		return 1;
	puts("unifdef contract tests passed");
	return 0;
}
