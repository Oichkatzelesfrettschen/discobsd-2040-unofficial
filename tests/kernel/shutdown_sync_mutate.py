"""Generate calibrated mutations of the production shutdown helper."""

import sys
from pathlib import Path

MUTATIONS = {
    "unconditional_success": (
        "\tif (busy_count == 0)\n"
        "\t\tprintf(\"buffers idle\\n\");",
        "\tif (1)\n"
        "\t\tprintf(\"buffers idle\\n\");",
    ),
    "missing_recount": (
        "\t\tmdelay(40U * (unsigned)pass);\n"
        "\t\tbusy_count = shutdown_busy_buffers();",
        "\t\tmdelay(40U * (unsigned)pass);",
    ),
    "ignored_errors": (
        "\t\tif (mount_entry->m_inodp == NULL ||\n"
        "\t\t    mount_entry->m_write_error == 0)\n"
        "\t\t\tcontinue;",
        "\t\tif (1)\n"
        "\t\t\tcontinue;",
    ),
    "cleared_latch": (
        "\t\tprintf(\"recorded write error on dev %o: error %d\\n\",\n"
        "\t\t    mount_entry->m_dev, mount_entry->m_write_error);",
        "\t\tprintf(\"recorded write error on dev %o: error %d\\n\",\n"
        "\t\t    mount_entry->m_dev, mount_entry->m_write_error);\n"
        "\t\tmount_entry->m_write_error = 0;",
    ),
    "bypassed_guards": (
        "\tif ((howto & RB_NOSYNC) != 0 || waittime >= 0 ||\n"
        "\t    bfreelist[0].av_forw == NULL)\n"
        "\t\treturn;",
        "\tif ((howto & RB_NOSYNC) != 0 && waittime >= 0 &&\n"
        "\t    bfreelist[0].av_forw == NULL)\n"
        "\t\treturn;",
    ),
}


def mutate(source_text, mutation_name, old_text, new_text):
    if source_text.count(old_text) != 1:
        raise ValueError(
            f"{mutation_name}: expected one mutation anchor, found "
            f"{source_text.count(old_text)}"
        )
    return source_text.replace(old_text, new_text, 1)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: shutdown_sync_mutate.py SOURCE OUTPUT_DIRECTORY"
        )
    source_path = Path(sys.argv[1])
    output_directory = Path(sys.argv[2])
    source_text = source_path.read_text(encoding="utf-8")
    for mutation_name, (old_text, new_text) in MUTATIONS.items():
        mutated_text = mutate(
            source_text, mutation_name, old_text, new_text
        )
        output_path = (
            output_directory / f"shutdown_sync_{mutation_name}.mutant.c"
        )
        output_path.write_text(mutated_text, encoding="utf-8")


if __name__ == "__main__":
    main()
