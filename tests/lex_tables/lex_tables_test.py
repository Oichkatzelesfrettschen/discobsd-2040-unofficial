"""Run usr.bin/lex, built for the host, over grammars sized against its tables.

Usage:
    lex_tables_test.py run --lex LEX --source DIR --examples DIR... --work DIR
    lex_tables_test.py mutants --source DIR --examples DIR... --work DIR
        --yacc YACC -- CC FLAGS...

lex keeps its parse tree, character classes, positions, transitions,
definitions, start conditions, pushed-back input and token text in tables
whose sizes are fixed before the grammar is read. `run` gives LEX one
grammar per table that needs more than the table holds and requires exit
status 1 with the diagnostic that names the table; a grammar that fits must
exit 0 and print a scanner. With LEX built under AddressSanitizer a store
past a table aborts the process, which is neither status, so the same run
establishes that the refusal precedes the store.

`mutants` removes one bounds check at a time from a copy of the sources,
builds each copy with the given compiler command and requires `run` to fail
on it. Each mutation replaces text that occurs exactly once, so an edit that
moves a check fails here instead of producing an unmutated copy. The
compiler command must carry the sanitizer flags: without them most of the
removed checks have no observable effect on the host.
"""

import argparse
import itertools
import shutil
import subprocess
import sys
from pathlib import Path

TIMEOUT_SECONDS = 60
SOURCES = ("lmain.c", "sub1.c", "sub2.c", "header.c")
BASE_RULES = "%%\nabc\t;\n[a-z]+\t;\n[0-9]+\t;\n.\t;\n"
LETTERS = "abcdefghijklmnopqrstuvwxyz"


def overflow_grammars():
    """Return (name, grammar, diagnostic) for one over-capacity grammar per check."""
    classes = [
        "".join(group)
        for group in itertools.islice(itertools.combinations(LETTERS, 12), 120)
    ]
    conditions = [f"c{number}" for number in range(20)]
    condition_sets = list(
        itertools.islice(itertools.combinations(conditions, 3), 120)
    )
    return (
        ("positions_small", "%p 2\n" + BASE_RULES, "Too many positions"),
        ("positions", "%p 6\n" + BASE_RULES, "Too many positions"),
        ("transitions", "%a 4\n" + BASE_RULES, "Too many transitions"),
        ("packed_classes", "%k 3\n" + BASE_RULES,
         "Too many packed character classes"),
        ("states", "%n 3\n" + BASE_RULES, "Too many states"),
        ("tree",
         "%%\n" + "".join(f"abcdefgh{n}\t;\n" for n in range(120)),
         "tree.l:47  (Error) Parse tree too big"),
        ("pushback", "A {A}yyyy\n%%\n{A}\t;\n", "Too many characters pushed"),
        ("classes",
         "%e 5000\n%%\n"
         + "".join(f"[{c}]x{n}\t;\n" for n, c in enumerate(classes)),
         "Too many large character classes"),
        ("dot_class",
         "%e 5000\n%%\n"
         + "".join(f"[{c}]x{n}\t;\n" for n, c in enumerate(classes[:60]))
         + ".\t;\n",
         "Too many large character classes"),
        ("token_definition", "%%\n{" + "d" * 1500 + "}\t;\n", "Token too long"),
        ("token_start", "%%\n<" + "s" * 1500 + ">x\t;\n", "Token too long"),
        ("token_letters", "%e 5000\n%%\n" + "q" * 1500 + "\t;\n",
         "Token too long"),
        ("token_iteration", "%%\na{" + "1" * 1500 + "}\t;\n", "Token too long"),
        ("string", '%e 5000\n%%\n"' + "s" * 1500 + '"\t;\n', "String too long"),
        ("line", "D " + "x" * 700 + "\n%%\na\t;\n", "Input line too long"),
        ("definition_count",
         "".join(f"D{n} a\n" for n in range(45)) + "%%\na\t;\n",
         "Too many definitions"),
        ("definition_text",
         "".join(f"D{n} " + "a" * 200 + "\n" for n in range(8)) + "%%\na\t;\n",
         "Definitions too long"),
        ("start_text",
         "".join(f"%s cond{n}aaaaaaaa\n" for n in range(12)) + "%%\na\t;\n",
         "Start conditions too long"),
        ("start_uses",
         "%e 5000\n%s " + " ".join(conditions[:10]) + "\n%s "
         + " ".join(conditions[10:]) + "\n%%\n"
         + "".join(
             f"<{','.join(group)}>x{n}\t;\n"
             for n, group in enumerate(condition_sets)
         ),
         "Too many start conditions used"),
        ("option_refused", "%option noyywrap\n" + BASE_RULES,
         "Unsupported %option noyywrap"),
    )


ACCEPTED_GRAMMARS = (
    ("base", BASE_RULES),
    ("option_accepted", "%option nounput noinput\n" + BASE_RULES),
    ("output_size", "%o 2000\n" + BASE_RULES),
)

# Shipped grammars and the status this lex gives them at the SMALL table
# sizes the target builds with. scan.l needs 967 parse-tree nodes of 650.
SHIPPED_GRAMMARS = {
    "awk.lx.l": (0, None),
    "example.l": (0, None),
    "scanner.l": (0, None),
    "lex.l": (0, None),
    "scan.l": (1, "scan.l:161  (Error) Parse tree too big"),
}

# (name, file, original text, replacement)
MUTATIONS = (
    ("tree_check", "sub1.c", "\tif(tptr >= treesize)\n", "\tif(0)\n"),
    ("pushback_check", "sub1.c",
     "\tif(n >= pushc + TOKENSIZE - pushptr)\n", "\tif(0)\n"),
    ("line_check", "sub1.c",
     "\t\tif(t >= buf + sizeof(buf) - 1)\n", "\t\tif(0)\n"),
    ("packed_class_check", "sub2.c",
     "\tif(pcptr >= pchar + pchlen)\n", "\tif(0)\n"),
    ("position_check", "sub2.c",
     '\terror("Too many positions %s"', '\tif(0) error("Too many positions %s"'),
    ("transition_check", "sub2.c",
     '\terror("Too many transitions %s"',
     '\tif(0) error("Too many transitions %s"'),
    ("state_check", "sub2.c",
     "\t\t\t\t\tif(stnum + 1 >= nstates)", "\t\t\t\t\tif(0)"),
    ("class_check", "parser.y",
     "\t\t\t\t\tif(strlen((char*)token) + 1 >=\n"
     "\t\t\t\t\t    (size_t)(ccl + CCLSIZE - ccptr))\n",
     "\t\t\t\t\tif(0)\n"),
    ("dot_class_check", "parser.y",
     "\t\t\tif(ccptr + (NCH-1) > ccl+CCLSIZE)\n", "\t\t\tif(0)\n"),
    ("token_check", "parser.y", "\tif(i >= TOKENSIZE-1) \\\n", "\tif(0) \\\n"),
    ("string_check", "parser.y",
     "\t\t\t\t\tif(i >= TOKENSIZE-1)\n", "\t\t\t\t\tif(0)\n"),
    ("definition_count_check", "parser.y",
     "\t\tif(dptr + 1 >= DEFSIZE)\n", "\t\tif(0)\n"),
    ("definition_text_check", "parser.y",
     "\t\tif(strlen((char*)$2.cp) + strlen((char*)$3.cp) + 2 >=\n"
     "\t\t    (size_t)(dchar + DEFCHAR - dp))\n",
     "\t\tif(0)\n"),
    ("start_text_check", "parser.y",
     "\t\t\t\t\t\t\tif(strlen((char*)t) + 1 >=\n"
     "\t\t\t\t\t\t\t    (size_t)(stchar + STARTCHAR - sp))\n",
     "\t\t\t\t\t\t\tif(0)\n"),
    ("start_use_check", "parser.y",
     "\t\t\t\t\tif(slptr >= slist+STARTSIZE)\n"
     '\t\t\t\t\t\terror("Too many start conditions used");\n'
     "\t\t\t\t\t*slptr++ = i+1;\n",
     "\t\t\t\t\t*slptr++ = i+1;\n"),
    ("option_check", "parser.y",
     "\t\tif(accepted[n] == 0)\n", "\t\tif(0)\n"),
    ("option_dispatch", "parser.y",
     '\t\t\t\t\t\tif(strncmp((char*)p,"%option",7) == 0 &&\n',
     '\t\t\t\t\t\tif(0 && strncmp((char*)p,"%option",7) == 0 &&\n'),
)


def find_shipped(example_directories):
    found = {}
    for directory in example_directories:
        for path in sorted(Path(directory).glob("*.l")):
            if path.name in SHIPPED_GRAMMARS:
                found[path.name] = path
    missing = sorted(set(SHIPPED_GRAMMARS) - set(found))
    if missing:
        raise SystemExit(f"FAIL lex-tables: shipped grammars missing: {missing}")
    return found


def run_lex(lex, grammar, work):
    """Run lex -t on grammar inside work; return (status, scanner, diagnostics)."""
    try:
        completed = subprocess.run(
            [str(lex), "-t", str(grammar)],
            cwd=work,
            capture_output=True,
            timeout=TIMEOUT_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return None, "", f"no exit within {TIMEOUT_SECONDS} seconds"
    return (
        completed.returncode,
        completed.stdout.decode("latin-1"),
        completed.stderr.decode("latin-1"),
    )


def check_case(lex, grammar, work, want_status, want_text):
    status, scanner, diagnostics = run_lex(lex, grammar, work)
    problems = []
    if status != want_status:
        problems.append(f"exit {status}, expected {want_status}")
    if "Sanitizer" in diagnostics or "runtime error" in diagnostics:
        problems.append("sanitizer report")
    if want_text is not None and want_text not in diagnostics:
        problems.append(f"diagnostic lacks {want_text!r}")
    if want_status == 0 and "yylex(void)" not in scanner:
        problems.append("no scanner on standard output")
    if problems:
        first_lines = " | ".join(diagnostics.splitlines()[:3])
        problems.append(f"stderr: {first_lines}")
    return problems


def run_cases(lex, source, example_directories, work):
    """Return a list of 'case: problem' strings; empty when every case holds."""
    work = Path(work).resolve()
    skeleton = work / "share" / "misc"
    skeleton.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(Path(source) / "ncform", skeleton / "ncform")
    lex = Path(lex).resolve()
    failures = []
    count = 0
    for name, grammar, diagnostic in overflow_grammars():
        path = work / f"{name}.l"
        path.write_text(grammar)
        count += 1
        for problem in check_case(lex, path, work, 1, diagnostic):
            failures.append(f"{name}: {problem}")
    for name, grammar in ACCEPTED_GRAMMARS:
        path = work / f"{name}.l"
        path.write_text(grammar)
        count += 1
        for problem in check_case(lex, path, work, 0, None):
            failures.append(f"{name}: {problem}")
    for name, path in sorted(find_shipped(example_directories).items()):
        status, diagnostic = SHIPPED_GRAMMARS[name]
        count += 1
        for problem in check_case(lex, path.resolve(), work, status, diagnostic):
            failures.append(f"{name}: {problem}")
    return count, failures


def build(compiler, yacc, source, output):
    """Build lex from source into output; return the failed command's output or None."""
    generated = output.with_suffix(".y.c")
    for command in (
        [yacc, "-o", str(generated), str(source / "parser.y")],
        [*compiler, f"-I{source}", "-o", str(output), str(generated)]
        + [str(source / name) for name in SOURCES],
    ):
        completed = subprocess.run(
            command, capture_output=True, text=True, check=False
        )
        if completed.returncode != 0:
            return completed.stdout + completed.stderr
    return None


def command_run(arguments):
    count, failures = run_cases(
        arguments.lex, arguments.source, arguments.examples, arguments.work
    )
    for failure in failures:
        print(f"FAIL lex-tables: {failure}")
    if failures:
        return 1
    print(f"PASS lex-tables: {count} grammars")
    return 0


def command_mutants(arguments):
    source = Path(arguments.source)
    work = Path(arguments.work).resolve()
    survivors = []
    for name, file_name, original, replacement in MUTATIONS:
        mutant_source = work / f"mutant-{name}"
        if mutant_source.exists():
            shutil.rmtree(mutant_source)
        mutant_source.mkdir(parents=True)
        for path in source.iterdir():
            if path.suffix in (".c", ".h", ".y") or path.name == "ncform":
                shutil.copyfile(path, mutant_source / path.name)
        text = (mutant_source / file_name).read_text()
        if text.count(original) != 1:
            print(
                f"FAIL lex-tables: mutation {name} matches "
                f"{text.count(original)} sites in {file_name}"
            )
            return 1
        (mutant_source / file_name).write_text(text.replace(original, replacement))
        binary = mutant_source / "lex"
        errors = build(arguments.compiler, arguments.yacc, mutant_source, binary)
        if errors is not None:
            print(f"FAIL lex-tables: mutant {name} does not build\n{errors}")
            return 1
        _, failures = run_cases(
            binary, mutant_source, arguments.examples, mutant_source / "work"
        )
        if not failures:
            survivors.append(name)
    if survivors:
        print(f"FAIL lex-tables: mutants accepted: {' '.join(survivors)}")
        return 1
    print(f"PASS lex-tables: {len(MUTATIONS)} mutants rejected")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run")
    run.add_argument("--lex", required=True)
    mutants = commands.add_parser("mutants")
    mutants.add_argument("--yacc", required=True)
    mutants.add_argument("compiler", nargs="+")
    for command in (run, mutants):
        command.add_argument("--source", required=True)
        command.add_argument("--examples", required=True, action="append")
        command.add_argument("--work", required=True)
    arguments = parser.parse_args()
    if arguments.command == "run":
        return command_run(arguments)
    return command_mutants(arguments)


if __name__ == "__main__":
    sys.exit(main())
