"""Generate calibrated scandir contract mutations from production source."""

import sys
from pathlib import Path

MUTATIONS = {
    "read_error_loss": [
        (
            "\t\t\tif (errno != 0)\n\t\t\t\tgoto fail;",
            "\t\t\tif (0)\n\t\t\t\tgoto fail;",
        )
    ],
    "cleanup_errno_loss": [
        (
            "\tclosedir(directory);\n\terrno = saved_errno;\n\treturn -1;",
            "\tclosedir(directory);\n\t(void)saved_errno;\n\treturn -1;",
        )
    ],
    "leak_record": [
        (
            "\twhile (entry_count != 0)\n"
            "\t\tfree(names[--entry_count]);",
            "\twhile (entry_count != 0)\n\t\t--entry_count;",
        )
    ],
    "metadata_growth": [
        (
            "\t\t\t\tnew_capacity = 4;",
            "\t\t\t\tnew_capacity = (size_t)status.st_size / 24;",
        )
    ],
    "source_reclen": [
        (
            "\t\tentry_copy->d_reclen = (u_short)record_size;",
            "\t\tentry_copy->d_reclen = entry->d_reclen;",
        )
    ],
    "shared_comparator": [
        (
            "static int\ncompare_entries(",
            "static int (*shared_comparison)(const struct direct **,\n"
            "    const struct direct **);\n\nstatic int\ncompare_entries(",
        ),
        (
            "\treturn (*comparison)(&left_argument, &right_argument);",
            "\t(void)comparison;\n"
            "\treturn (*shared_comparison)(&left_argument, &right_argument);",
        ),
        (
            "\tsize_t end;\n\tsize_t start;\n\n\tif (entry_count < 2)",
            "\tsize_t end;\n\tsize_t start;\n\n"
            "\tshared_comparison = comparison;\n\tif (entry_count < 2)",
        ),
    ],
}


def replace_once(source_text: str, old_text: str, new_text: str, label: str) -> str:
    anchor_count = source_text.count(old_text)
    if anchor_count != 1:
        raise RuntimeError(
            f"{label}: expected one mutation anchor, found {anchor_count}"
        )
    return source_text.replace(old_text, new_text, 1)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: scandir_mutate.py SOURCE OUTPUT_DIRECTORY")
    source_path = Path(sys.argv[1])
    output_directory = Path(sys.argv[2])
    source_text = source_path.read_text(encoding="utf-8")

    for mutation_name, replacements in MUTATIONS.items():
        mutant_text = source_text
        for old_text, new_text in replacements:
            mutant_text = replace_once(
                mutant_text, old_text, new_text, mutation_name
            )
        output_path = output_directory / f"scandir_{mutation_name}.mutant.c"
        output_path.write_text(mutant_text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
