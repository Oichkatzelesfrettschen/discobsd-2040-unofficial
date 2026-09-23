"""Bind selected regression sources to pinned executable build recipes."""

import hashlib
import re
import shlex
from pathlib import PurePosixPath


def require(condition, message):
    if not condition:
        raise ValueError(message)


def logical_lines(contents):
    physical = contents.decode("utf-8").splitlines()
    result = []
    position = 0
    while position < len(physical):
        line = physical[position]
        position += 1
        while line.rstrip().endswith("\\"):
            require(position < len(physical), "unterminated Makefile continuation")
            line = line.rstrip()[:-1] + " " + physical[position].lstrip()
            position += 1
        result.append(line)
    return result


def rule_region(lines, target):
    marker = target + ":"
    starts = [index for index, line in enumerate(lines) if line.startswith(marker)]
    require(len(starts) == 1, f"expected one build rule for {target}")
    start = starts[0]
    end = len(lines)
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if line and not line[0].isspace() and not line.startswith(("#", ".")) and ":" in line:
            head = line.split(":", 1)[0]
            if "=" not in head:
                end = index
                break
    return lines[start:end]


def build_rule(lines, target):
    region = rule_region(lines, target)
    prerequisites = region[0].split(":", 1)[1].split()
    commands = [line.lstrip() for line in region[1:] if line.startswith("\t")]
    require(commands, f"missing build recipe for {target}")
    return prerequisites, commands


def shell_tokens(command):
    lexer = shlex.shlex(command, posix=True, punctuation_chars=";&|<>")
    lexer.whitespace_split = True
    lexer.commenters = "#"
    tokens = list(lexer)
    require(not any(token[0] in ";&|<>" for token in tokens if token),
            "compiler or submake recipe contains shell control")
    return tokens


def compiler_command(commands, source, object_compile):
    matches = []
    for command in commands:
        tokens = shell_tokens(command)
        if (source in tokens and "-o" in tokens and "$@" in tokens and
                tokens[0] in {"${HOST_CC}", "${HOSTCC}"} and
                not {"-E", "-S", "-fsyntax-only"} & set(tokens) and
                ("-c" in tokens) == object_compile):
            matches.append(tokens)
    require(len(matches) == 1, f"expected one compiler recipe for {source}")
    return matches[0]


def guarded_commands(region):
    guards = []
    commands = []
    for position, line in enumerate(region[1:]):
        directive = line.strip()
        if directive.startswith(".if "):
            guards.append((directive, "if"))
        elif directive.startswith(".elif "):
            require(guards and guards[-1][1] != "else",
                    "unmatched Makefile .elif")
            condition, _ = guards[-1]
            guards[-1] = (condition, f"elif:{position}")
        elif directive == ".else":
            require(guards, "unmatched Makefile .else")
            condition, branch = guards[-1]
            require(branch != "else", "duplicate Makefile .else")
            guards[-1] = (condition, "else")
        elif directive == ".endif":
            require(guards, "unmatched Makefile .endif")
            guards.pop()
        elif line.startswith("\t"):
            commands.append((line.lstrip(), tuple(guards)))
    require(not guards, "unterminated Makefile conditional")
    return commands


def build_precedes_run(region, program, recipe):
    commands = guarded_commands(region)
    runs = [(index, guard) for index, (command, guard) in enumerate(commands)
            if command.replace("${PROG}", program) == recipe]
    require(len(runs) == 1, "inventory execution recipe is absent")
    run_index, run_guard = runs[0]
    return any(guard == run_guard and
               (tokens := shell_tokens(command.replace("${PROG}", program))) and
               tokens[0] in {"@${MAKE}", "${MAKE}"} and
               all(re.fullmatch(r"[A-Za-z0-9_]+", token)
                   for token in tokens[1:]) and
               program in tokens[1:]
               for command, guard in commands[:run_index])


def validate(data, evidence, bindings, inventory, report, read_blob):
    require(bindings["schema_version"] == 1, "unsupported recipe binding schema")
    comparison = evidence["source_comparison"]
    require(bindings["recipient_commit"] == data["recipient_commit"] and
            bindings["executed_tree"] == comparison["executed_tree"],
            "recipe binding revision mismatch")
    rows = [row for row in data["fixes"] if row["validation"] == "executed"]
    required_variants = {identifier for row in rows for identifier in row["variants"]}
    variant_bindings = bindings["variants"]
    require(set(variant_bindings) == required_variants,
            "recipe binding variant set mismatch")
    for row in rows:
        sources = {variant_bindings[identifier]["source"] for identifier in row["variants"]}
        require(sources == set(row["regression_sources"]),
                f"{row['id']}: regression source binding mismatch")
    inventory_variants = {variant["id"]: variant for variant in inventory["variants"]}
    report_receipts = {receipt["variant"]: receipt for receipt in report["variants"]}
    required_makefiles = {inventory_variants[identifier]["makefile"]
                          for identifier in required_variants}
    require(set(bindings["makefiles"]) == required_makefiles,
            "recipe binding Makefile set mismatch")
    require(set(bindings["supporting_inputs"]) == {
        "Makefile", "tools/test-execution.mk", "tools/test_execution.py",
        "tests/umount_contracts/umount_shim.h",
    }, "recipe supporting-input set mismatch")
    makefiles = {}
    sources = {}
    for path, expected_hash in (bindings["makefiles"] |
                                bindings["supporting_inputs"]).items():
        require(re.fullmatch(r"[0-9a-f]{64}", expected_hash),
                f"{path}: invalid recipe input hash")
        recipient = read_blob(data["recipient_commit"], path)
        executed = read_blob(comparison["executed_tree"], path)
        require(hashlib.sha256(recipient).hexdigest() == expected_hash and
                hashlib.sha256(executed).hexdigest() == expected_hash,
                f"{path}: pinned recipe input hash mismatch")
        sources[path] = recipient
        if path in bindings["makefiles"]:
            lines = logical_lines(recipient)
            assignments = [(index, match[1], match[2])
                           for index, line in enumerate(lines)
                           if (match := re.fullmatch(r"ILP32\s*([:?+!]?)=\s*(.*)", line))]
            first_conditional = next((index for index, line in enumerate(lines)
                                      if line.startswith(".if ")), len(lines))
            require(len(assignments) == 1 and
                    assignments[0][0] < first_conditional and
                    assignments[0][1:] == ("", "-m32"),
                    f"{path}: ILP32 width assignment mismatch")
            makefiles[path] = lines
    require(b"check-ilp32-execution-recipes:" in sources["Makefile"] and
            b"tools/test_execution.py" in sources["tools/test-execution.mk"] and
            all(b"include ${TOPSRC}/tools/test-execution.mk" in sources[path]
                for path in required_makefiles),
            "recipe supporting inputs do not bind the execution route")
    for identifier, binding in variant_bindings.items():
        require(set(binding) == {"source", "compile_target"},
                f"{identifier}: unexpected binding fields")
        expected = inventory_variants[identifier]
        reported = report_receipts[identifier]
        makefile_path = expected["makefile"]
        directory = expected["directory"]
        source_path = binding["source"]
        require(source_path.startswith(directory + "/"),
                f"{identifier}: source is outside test directory")
        source = source_path[len(directory) + 1:]
        require("/" not in source and source == PurePosixPath(source_path).name,
                f"{identifier}: regression source is not local")
        require(source_path in comparison["source_sha256"],
                f"{identifier}: regression source lacks pinned hash")
        program = expected["program"]
        compile_target = binding["compile_target"]
        require(compile_target == ("gate.o" if identifier == "umount.ilp32" else program),
                f"{identifier}: compiler target drift")
        lines = makefiles[makefile_path]
        prerequisites, commands = build_rule(lines, compile_target)
        require(source in prerequisites,
                f"{identifier}: regression source is not a prerequisite")
        compile_tokens = compiler_command(commands, source,
                                          identifier == "umount.ilp32")
        if identifier == "umount.ilp32":
            require("umount_shim.h" in prerequisites,
                    "umount gate object lacks the pinned shim header")
            require(any(re.fullmatch(r"PROG\s*=\s*" + re.escape(program), line)
                        for line in lines), "umount program identity mismatch")
            require(any(re.fullmatch(r"CFLAGS\s*=.*\$\{ILP32\}.*", line)
                        for line in lines) and "${CFLAGS}" in compile_tokens,
                    "umount compiler width is unbound")
            link_prerequisites, link_commands = build_rule(lines, "${PROG}")
            require("gate.o" in link_prerequisites and any(
                "gate.o" in (tokens := shlex.split(command)) and
                tokens[0] == "${HOSTCC}" and "-c" not in tokens and
                "-o" in tokens and "$@" in tokens
                for command in link_commands),
                "umount gate.o is absent from executable link")
        else:
            width_selected = "${ILP32}" in compile_tokens or "-m32" in compile_tokens
            require(width_selected == (expected["width"] == "ilp32"),
                    f"{identifier}: compiler width mismatch")
        execution_region = rule_region(lines, expected["target"])
        recipe = expected["recipe"].replace("${PROG}", program)
        target_prerequisites = execution_region[0].split(":", 1)[1].split()
        require(program in expected["prerequisites"],
                f"{identifier}: inventory lacks executable prerequisite")
        recursively_built = build_precedes_run(execution_region, program, recipe)
        require(program in target_prerequisites or recursively_built,
                f"{identifier}: executable is not built before execution")
        require(reported["invocation"] == ["./" + program] and
                reported["executable"]["path"] == directory + "/" + program and
                re.fullmatch(r"[0-9a-f]{64}", reported["executable"]["sha256"]),
                f"{identifier}: retained executable receipt mismatch")
