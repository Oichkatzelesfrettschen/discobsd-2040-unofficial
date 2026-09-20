#!/usr/bin/env python3
"""Run one bounded shard of Keen's independent C17 uniqueness proof."""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
CASE_TIMEOUT_SECONDS = 30


def run_process(arguments, label, environment=None, timeout=CASE_TIMEOUT_SECONDS):
    try:
        return subprocess.run(
            arguments,
            capture_output=True,
            text=True,
            check=True,
            env=environment,
            timeout=timeout,
        ).stdout
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            f"{label}: exceeded {timeout} seconds"
        ) from error
    except subprocess.CalledProcessError as error:
        raise AssertionError(
            f"{label}: exited {error.returncode}: {error.stderr.strip()}"
        ) from error


def verify_timeout_control():
    try:
        subprocess.run(
            [sys.executable, "-c", "import time; time.sleep(2)"],
            check=True,
            timeout=0.01,
        )
    except subprocess.TimeoutExpired:
        print("keen: subprocess deadline negative control passed")
        return
    raise AssertionError("keen: subprocess deadline accepted a hanging control")


def fixture(name):
    with open(os.path.join(FIXTURES, name), encoding="ascii") as input_file:
        return input_file.read()


def verify_solver_rejects(solver, text, label, limit="10"):
    descriptor, path = tempfile.mkstemp(prefix="keen-invalid-", suffix=".keen")
    try:
        with os.fdopen(descriptor, "w", encoding="ascii") as output_file:
            output_file.write(text)
        try:
            result = subprocess.run(
                [solver, path, limit],
                capture_output=True,
                text=True,
                timeout=CASE_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired as error:
            raise AssertionError(f"{label}: rejection timed out") from error
    finally:
        os.unlink(path)
    assert result.returncode == 2, (
        f"{label}: malformed input returned {result.returncode}: {result.stdout}"
    )


def dump(program, size, seed, nounique=False):
    environment = dict(os.environ)
    environment.pop("KEEN_NOUNIQUE", None)
    if nounique:
        environment["KEEN_NOUNIQUE"] = "1"
    return run_process(
        [program, "--dump", str(size), str(seed)],
        f"size {size} seed {seed}: generator",
        environment,
    )


def parse_count(output, label):
    first_line = output.splitlines()[0] if output else ""
    words = first_line.split()
    assert len(words) == 2 and words[0] == "solutions", (
        f"{label}: malformed count output {first_line!r}"
    )
    return int(words[1])


def both(program, solver, text, label, limit=1000, emit=False):
    descriptor, path = tempfile.mkstemp(prefix="keen-count-", suffix=".keen")
    try:
        with os.fdopen(descriptor, "w", encoding="ascii") as output_file:
            output_file.write(text)
        game_output = run_process(
            [program, "--count", path, str(limit)], f"{label}: game counter"
        )
        solver_arguments = [solver]
        if emit:
            solver_arguments.append("--emit")
        solver_arguments.extend([path, str(limit)])
        solver_output = run_process(
            solver_arguments, f"{label}: independent C17 counter"
        )
    finally:
        os.unlink(path)
    game_count = parse_count(game_output, f"{label}: game counter")
    solver_count = parse_count(solver_output, f"{label}: independent counter")
    assert game_count == solver_count, (
        f"{label}: counters disagree: game {game_count}, "
        f"independent C17 {solver_count}\n{text}"
    )
    grids = [
        line[5:]
        for line in solver_output.splitlines()[1:]
        if line.startswith("grid ")
    ]
    return game_count, grids


def imposed_entries(text):
    lines = text.splitlines()
    start = lines.index("entries") + 1
    entries = {}
    for row, line in enumerate(lines[start:start + 5]):
        for column, value in enumerate(line):
            if value != ".":
                entries[(row, column)] = int(value)
    return entries


def check_fixtures(program, solver):
    verify_timeout_control()
    reported = fixture("reported-5x5.keen")
    verify_solver_rejects(
        solver, reported.replace("size 5", "size 5junk", 1), "invalid size"
    )
    first_clue = next(
        line for line in reported.splitlines() if line.startswith("a ")
    )
    verify_solver_rejects(
        solver, f"{reported}\n{first_clue}\n", "duplicate clue"
    )
    verify_solver_rejects(solver, f"{reported}\nZ 1\n", "unused clue")
    verify_solver_rejects(solver, f"{reported}\n{'x' * 128}\n", "long line")
    verify_solver_rejects(solver, reported, "invalid solution limit", "10junk")
    print("keen: C17 parser negative controls passed")
    reported_count, grids = both(
        program, solver, reported, "reported fixture", emit=True
    )
    assert reported_count == 5, (
        f"reported fixture: expected exactly five grids, got {reported_count}"
    )
    assert len(grids) == 5 and all(len(grid) == 25 for grid in grids), (
        "reported fixture: independent solver did not emit all five grids"
    )
    forced = {}
    for cell in range(25):
        values = {grid[cell] for grid in grids}
        if len(values) == 1:
            forced[(cell // 5, cell % 5)] = int(values.pop())
    imposed = imposed_entries(fixture("reported-5x5-entries.keen"))
    assert forced == imposed, (
        f"reported fixture: forced {sorted(forced)}, imposed {sorted(imposed)}"
    )
    assert len(forced) == 13, (
        f"reported fixture: expected thirteen forced cells, got {len(forced)}"
    )
    entries_count, _ = both(
        program,
        solver,
        fixture("reported-5x5-entries.keen"),
        "reported fixture with entries",
    )
    assert entries_count == 5, (
        f"reported fixture with entries: expected five, got {entries_count}"
    )
    unique_count, _ = both(
        program, solver, fixture("unique-5x5.keen"), "unique fixture"
    )
    inconsistent_count, _ = both(
        program,
        solver,
        fixture("inconsistent-5x5.keen"),
        "inconsistent fixture",
    )
    assert unique_count == 1, f"unique fixture: got {unique_count}"
    assert inconsistent_count == 0, (
        f"inconsistent fixture: got {inconsistent_count}"
    )
    print(
        "keen: fixtures OK (reported 5 grids, 13 forced cells match entries, "
        "unique 1, inconsistent 0)"
    )


def check_unchecked(program, solver, size):
    ambiguous = 0
    for seed in range(1, 41):
        count, _ = both(
            program,
            solver,
            dump(program, size, seed, nounique=True),
            f"unchecked size {size} seed {seed}",
            limit=50,
        )
        if count >= 2:
            ambiguous += 1
    assert ambiguous > 0, (
        f"unchecked size {size}: no seed produced an ambiguous puzzle"
    )
    print(
        f"keen: unchecked size {size}: {ambiguous}/40 puzzles ambiguous"
    )


def check_unique_range(program, solver, size, first_seed, last_seed):
    for seed in range(first_seed, last_seed + 1):
        count, _ = both(
            program,
            solver,
            dump(program, size, seed),
            f"checked size {size} seed {seed}",
            limit=50,
        )
        assert count == 1, (
            f"checked size {size} seed {seed}: expected one grid, got {count}"
        )
    case_count = last_seed - first_seed + 1
    print(
        f"keen: checked size {size} seeds {first_seed}-{last_seed}: "
        f"{case_count}/{case_count} puzzles unique"
    )


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: uniqueness.py fixtures|unchecked-N|checked-N-FIRST-LAST "
            "game solver"
        )
    group, program, solver = sys.argv[1:]
    group_parts = group.split("-")
    if group == "fixtures":
        check_fixtures(program, solver)
    elif len(group_parts) == 2 and group_parts[0] == "unchecked" and (
        group_parts[1] in {"4", "5"}
    ):
        check_unchecked(program, solver, int(group_parts[1]))
    elif len(group_parts) == 4 and group_parts[0] == "checked" and (
        group_parts[1] in {"3", "4", "5", "6"}
    ):
        size, first_seed, last_seed = map(int, group_parts[1:])
        if not 1 <= first_seed <= last_seed <= 40:
            raise SystemExit(f"invalid checked seed range: {group}")
        check_unique_range(program, solver, size, first_seed, last_seed)
    else:
        raise SystemExit(f"unknown uniqueness group: {group}")


if __name__ == "__main__":
    main()
