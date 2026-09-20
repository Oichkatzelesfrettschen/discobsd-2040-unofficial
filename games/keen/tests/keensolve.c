#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 6
#define MAX_CELLS (MAX_SIZE * MAX_SIZE)
#define MAX_CAGES 52
#define MAX_PERMUTATIONS 720
#define MAX_EMITTED_SOLUTIONS 100

struct clue {
	int target;
	char operation;
	unsigned char used;
	unsigned char defined;
};

struct puzzle {
	int size;
	unsigned char cage[MAX_CELLS];
	unsigned char fixed[MAX_CELLS];
	struct clue clues[MAX_CAGES];
};

static int
read_line(FILE *input, char *line, size_t capacity)
{
	size_t length;

	if (fgets(line, (int)capacity, input) == NULL)
		return ferror(input) ? -1 : 1;
	length = strlen(line);
	if (length > 0 && line[length - 1] == '\n') {
		line[--length] = '\0';
		if (length > 0 && line[length - 1] == '\r')
			line[length - 1] = '\0';
	} else if (length == capacity - 1) {
		return -1;
	}
	return 0;
}

static int
parse_bounded_integer(const char *text, int minimum, int maximum, int *value)
{
	char *end;
	long parsed;

	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed < minimum ||
	    parsed > maximum)
		return -1;
	*value = (int)parsed;
	return 0;
}

struct solver {
	const struct puzzle *puzzle;
	unsigned char permutations[MAX_PERMUTATIONS][MAX_SIZE];
	unsigned char grid[MAX_CELLS];
	unsigned char emitted[MAX_EMITTED_SOLUTIONS][MAX_CELLS];
	unsigned int column_masks[MAX_SIZE];
	int permutation_count;
	int solution_count;
	int solution_limit;
	int emit_solutions;
};

struct partial_cage {
	int values[MAX_CELLS];
	int value_count;
	int remaining;
};

enum parse_state {
	EXPECT_SIZE,
	EXPECT_CAGES,
	EXPECT_CLUES,
	READ_CLUES,
	PARSE_COMPLETE
};

static int
label_index(int label)
{
	if (label >= 'a' && label <= 'z')
		return label - 'a';
	if (label >= 'A' && label <= 'Z')
		return 26 + label - 'A';
	return -1;
}

static int
read_grid(FILE *input, struct puzzle *puzzle, int entries)
{
	char line[128];
	int row;

	for (row = 0; row < puzzle->size; row++) {
		int column;

		if (read_line(input, line, sizeof(line)) != 0)
			return -1;
		if ((int)strlen(line) != puzzle->size)
			return -1;
		for (column = 0; column < puzzle->size; column++) {
			int cell = row * puzzle->size + column;

			if (entries) {
				if (line[column] == '.')
					puzzle->fixed[cell] = 0;
				else if (line[column] >= '1' &&
				    line[column] < '1' + puzzle->size)
					puzzle->fixed[cell] =
					    (unsigned char)(line[column] - '0');
				else
					return -1;
			} else {
				int cage = label_index((unsigned char)line[column]);

				if (cage < 0)
					return -1;
				puzzle->cage[cell] = (unsigned char)cage;
				puzzle->clues[cage].used = 1;
			}
		}
	}
	return 0;
}

static int
parse_clue(const char *line, struct puzzle *puzzle)
{
	const char *specification;
	struct clue *clue;
	char *end;
	long target;
	int cage;

	if (line[0] == '\0' || line[1] != ' ' || line[2] == '\0')
		return -1;
	cage = label_index((unsigned char)line[0]);
	if (cage < 0)
		return -1;
	clue = &puzzle->clues[cage];
	if (clue->defined)
		return -1;
	specification = line + 2;
	errno = 0;
	target = strtol(specification, &end, 10);
	if (errno != 0 || end == specification || target < 1 || target > INT_MAX)
		return -1;
	if (*end == '\0')
		clue->operation = '\0';
	else if (end[1] == '\0' && strchr("+-*/", *end) != NULL)
		clue->operation = *end;
	else
		return -1;
	clue->target = (int)target;
	clue->defined = 1;
	return 0;
}

static int
validate_clues(const struct puzzle *puzzle)
{
	for (int cage = 0; cage < MAX_CAGES; cage++) {
		const struct clue *clue = &puzzle->clues[cage];
		int cell_count = 0;

		for (int cell = 0; cell < puzzle->size * puzzle->size; cell++) {
			if (puzzle->cage[cell] == cage)
				cell_count++;
		}
		if (clue->used != clue->defined)
			return -1;
		if (!clue->defined)
			continue;
		if (clue->operation == '\0' && cell_count != 1)
			return -1;
		if ((clue->operation == '-' || clue->operation == '/') &&
		    cell_count != 2)
			return -1;
		if ((clue->operation == '+' || clue->operation == '*') &&
		    cell_count < 2)
			return -1;
	}
	return 0;
}

static int
parse_puzzle_line(FILE *input, struct puzzle *puzzle, const char *line,
    enum parse_state *state)
{
	switch (*state) {
	case EXPECT_SIZE:
		if (strncmp(line, "size ", 5) != 0 ||
		    parse_bounded_integer(line + 5, 3, MAX_SIZE,
		    &puzzle->size) < 0)
			return -1;
		*state = EXPECT_CAGES;
		break;
	case EXPECT_CAGES:
		if (strcmp(line, "cages") != 0 ||
		    read_grid(input, puzzle, 0) != 0)
			return -1;
		*state = EXPECT_CLUES;
		break;
	case EXPECT_CLUES:
		if (strcmp(line, "clues") != 0)
			return -1;
		*state = READ_CLUES;
		break;
	case READ_CLUES:
		if (strcmp(line, "entries") == 0) {
			if (read_grid(input, puzzle, 1) != 0)
				return -1;
			*state = PARSE_COMPLETE;
		} else if (parse_clue(line, puzzle) < 0) {
			return -1;
		}
		break;
	case PARSE_COMPLETE:
		return -1;
	}
	return 0;
}

static int
parse_puzzle(FILE *input, struct puzzle *puzzle)
{
	char line[128];
	int line_status;
	enum parse_state state = EXPECT_SIZE;

	*puzzle = (struct puzzle){0};
	while ((line_status = read_line(input, line, sizeof(line))) == 0) {
		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (parse_puzzle_line(input, puzzle, line, &state) < 0)
			return -1;
	}
	if (line_status < 0 || (state != READ_CLUES && state != PARSE_COMPLETE))
		return -1;
	return validate_clues(puzzle);
}

static int
cage_holds(const struct clue *clue, const int *values, int value_count)
{
	int result;
	int index;

	if (clue->operation == '\0')
		return value_count == 1 && values[0] == clue->target;
	if (clue->operation == '+') {
		result = 0;
		for (index = 0; index < value_count; index++)
			result += values[index];
		return result == clue->target;
	}
	if (clue->operation == '*') {
		result = 1;
		for (index = 0; index < value_count; index++) {
			if (result > clue->target / values[index])
				return 0;
			result *= values[index];
		}
		return result == clue->target;
	}
	if (value_count != 2)
		return 0;
	result = values[0] < values[1] ? values[0] : values[1];
	index = values[0] < values[1] ? values[1] : values[0];
	if (clue->operation == '-')
		return index - result == clue->target;
	return index % result == 0 && index / result == clue->target;
}

static int
addition_possible(const struct puzzle *puzzle, const struct clue *clue,
    const struct partial_cage *partial)
{
	int minimum = 0;

	for (int index = 0; index < partial->value_count; index++)
		minimum += partial->values[index];
	return minimum + partial->remaining <= clue->target &&
	    clue->target <= minimum + partial->remaining * puzzle->size;
}

static int
multiplication_possible(const struct puzzle *puzzle, const struct clue *clue,
    const struct partial_cage *partial)
{
	int maximum;
	int minimum = 1;

	for (int index = 0; index < partial->value_count; index++) {
		if (minimum > clue->target / partial->values[index])
			return 0;
		minimum *= partial->values[index];
	}
	maximum = minimum;
	for (int index = 0; index < partial->remaining; index++) {
		if (maximum > clue->target / puzzle->size) {
			maximum = clue->target;
			break;
		}
		maximum *= puzzle->size;
	}
	return clue->target % minimum == 0 && minimum <= clue->target &&
	    clue->target <= maximum;
}

static int
binary_cage_possible(const struct puzzle *puzzle, const struct clue *clue,
    const struct partial_cage *partial)
{
	if (partial->value_count == 0 || partial->remaining > 1)
		return 1;
	for (int value = 1; value <= puzzle->size; value++) {
		const int pair[2] = {partial->values[0], value};

		if (cage_holds(clue, pair, 2))
			return 1;
	}
	return 0;
}

static int
cage_possible(const struct solver *solver, int cage, int assigned_row)
{
	const struct puzzle *puzzle = solver->puzzle;
	const struct clue *clue = &puzzle->clues[cage];
	struct partial_cage partial = {{0}, 0, 0};

	for (int cell = 0; cell < puzzle->size * puzzle->size; cell++) {
		if (puzzle->cage[cell] != cage)
			continue;
		if (cell / puzzle->size <= assigned_row)
			partial.values[partial.value_count++] = solver->grid[cell];
		else
			partial.remaining++;
	}
	if (partial.remaining == 0)
		return cage_holds(clue, partial.values, partial.value_count);
	if (clue->operation == '\0')
		return 1;
	if (clue->operation == '+')
		return addition_possible(puzzle, clue, &partial);
	if (clue->operation == '*')
		return multiplication_possible(puzzle, clue, &partial);
	return binary_cage_possible(puzzle, clue, &partial);
}

static void
build_permutations(struct solver *solver, int column, unsigned int used,
    unsigned char *permutation)
{
	int value;

	if (column == solver->puzzle->size) {
		memcpy(solver->permutations[solver->permutation_count++],
		    permutation, (size_t)solver->puzzle->size);
		return;
	}
	for (value = 1; value <= solver->puzzle->size; value++) {
		unsigned int bit = 1U << value;

		if ((used & bit) != 0)
			continue;
		permutation[column] = (unsigned char)value;
		build_permutations(solver, column + 1, used | bit, permutation);
	}
}

static int
row_fits(const struct solver *solver, int row,
    const unsigned char *permutation)
{
	int column;

	for (column = 0; column < solver->puzzle->size; column++) {
		int cell = row * solver->puzzle->size + column;
		unsigned int bit = 1U << permutation[column];

		if ((solver->puzzle->fixed[cell] != 0 &&
		    solver->puzzle->fixed[cell] != permutation[column]) ||
		    (solver->column_masks[column] & bit) != 0)
			return 0;
	}
	return 1;
}

static void
search(struct solver *solver, int row)
{
	int permutation_index;

	if (solver->solution_count >= solver->solution_limit)
		return;
	if (row == solver->puzzle->size) {
		if (solver->emit_solutions &&
		    solver->solution_count < MAX_EMITTED_SOLUTIONS)
			memcpy(solver->emitted[solver->solution_count], solver->grid,
			    (size_t)(solver->puzzle->size * solver->puzzle->size));
		solver->solution_count++;
		return;
	}
	for (permutation_index = 0;
	    permutation_index < solver->permutation_count;
	    permutation_index++) {
		const unsigned char *permutation =
		    solver->permutations[permutation_index];
		int cage;
		int column;
		int possible = 1;

		if (!row_fits(solver, row, permutation))
			continue;
		memcpy(&solver->grid[row * solver->puzzle->size], permutation,
		    (size_t)solver->puzzle->size);
		for (column = 0; column < solver->puzzle->size; column++)
			solver->column_masks[column] |= 1U << permutation[column];
		for (cage = 0; cage < MAX_CAGES; cage++) {
			if (solver->puzzle->clues[cage].used &&
			    !cage_possible(solver, cage, row)) {
				possible = 0;
				break;
			}
		}
		if (possible)
			search(solver, row + 1);
		for (column = 0; column < solver->puzzle->size; column++)
			solver->column_masks[column] ^= 1U << permutation[column];
	}
}

int
main(int argc, char **argv)
{
	struct puzzle puzzle;
	struct solver solver = {0};
	unsigned char permutation[MAX_SIZE];
	FILE *input;
	int emit_solutions = 0;
	int solution_limit;

	if (argc == 4 && strcmp(argv[1], "--emit") == 0) {
		emit_solutions = 1;
		argv++;
		argc--;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: keensolve [--emit] puzzle limit\n");
		return 2;
	}
	input = strcmp(argv[1], "-") == 0 ? stdin : fopen(argv[1], "r");
	if (input == NULL) {
		perror(argv[1]);
		return 2;
	}
	if (parse_puzzle(input, &puzzle) < 0) {
		fprintf(stderr, "%s: invalid puzzle\n", argv[1]);
		if (input != stdin)
			fclose(input);
		return 2;
	}
	if (input != stdin)
		fclose(input);
	if (parse_bounded_integer(argv[2], 1, 10000, &solution_limit) < 0) {
		fprintf(stderr, "keensolve: limit must be from 1 through 10000\n");
		return 2;
	}
	solver.puzzle = &puzzle;
	solver.solution_limit = solution_limit;
	solver.emit_solutions = emit_solutions;
	build_permutations(&solver, 0, 0, permutation);
	search(&solver, 0);
	printf("solutions %d\n", solver.solution_count);
	if (emit_solutions) {
		for (int solution = 0; solution < solver.solution_count &&
		    solution < MAX_EMITTED_SOLUTIONS; solution++) {
			int cell;

			printf("grid ");
			for (cell = 0; cell < puzzle.size * puzzle.size; cell++)
				putchar('0' + solver.emitted[solution][cell]);
			putchar('\n');
		}
	}
	return 0;
}
