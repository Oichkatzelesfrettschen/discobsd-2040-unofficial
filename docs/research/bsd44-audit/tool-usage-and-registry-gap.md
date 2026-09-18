# Tool usage and registry gap

The audit used the source-analysis and bounded-evidence routing instructions.
The retained scratch files show lexical search, symbol/call maps, structural
queries, static diagnostics, source metrics, cross compilation, ARM ELF/ABI
inspection, reverse-engineering cross-checks, and evidence comparison.

The canonical Markdown registry already contains these used surfaces:
`rg`, `git`, `file`, `ctags`/`readtags`, `cscope`, `cflow`, `ast-grep`,
`semgrep`, `cppcheck`, `spatch`, `cloc`, `scc`, `lizard`, `diffoscope`,
`arm-none-eabi-gcc`, `arm-none-eabi-binutils`, `r2`/`radare2`, `rizin`,
`pahole`, `jq`, and `sha256sum`.

The following used command-line programs were absent from the Markdown
registry at the pre-edit check. Their executable paths and versions were
measured with `command -v` and `--version`:

| Package | Executables | Paths and observed versions | Use in this audit |
|---|---|---|---|
| `coreutils` | `sort`, `stat`, `timeout`, `wc` | `/usr/bin/sort`, `/usr/bin/stat`, `/usr/bin/timeout`, `/usr/bin/wc`; GNU coreutils 9.11 | bounded path inventory, file metadata, command time bounds, and line/byte counts |
| `findutils` | `find` | `/usr/bin/find`; GNU findutils 4.11.0-modified | exact LiteBSD path inventory and scratch-artifact inventory |
| `gawk` | `awk` | `/usr/bin/awk`; GNU Awk 5.4.1 | tabular evidence extraction and registry cross-checks |
| `sed` | `sed` | `/usr/bin/sed`; GNU sed 4.10 | bounded source slices and version-output extraction |

The registry Markdown is already stale relative to its adjacent JSONL
generator input before this audit (`1_tools_registry.py check` reports
`generated Markdown is stale`). The final edit is deliberately surgical: it
adds these four package rows to `1_TOOLS.md`'s package index and the relevant
derived section views, then verifies only the registry diff. It does not
regenerate the stale file or rewrite unrelated registry rows.

