# graft as the context layer for this tree

graft 0.18.0 (`@nanonets/graft`, /usr/bin/graft) builds a regenerable graph
of the tree under `graft/`: one markdown card per file, a wiring graph of
symbols and call edges, and, through a model, one concept node per
mechanism plus a summary and crux span per symbol. Every query refreshes
the structural graph against the working tree first, so an answer names
the code as it stands, uncommitted edits included. This note records what
each surface does on this tree, what was measured, and where the tool
misreads C.

## What runs without a model

`graft build` parses the 2472 C, header, assembly and Python files in
8 seconds into 24826 nodes and 14088 edges, a 61 MB cache. On that graph:

| Command | What it answers | Measured here |
| --- | --- | --- |
| `graft ask "<task>" --source` | ranked symbols with the crux lines inline | swap-to-flash question: three right hits in 515 tokens against 10458 for the files |
| `graft grep "<regex>"` | every hit grouped by enclosing symbol | found the one kernel caller of flash_swap_append at flash.c:592 |
| `graft skeleton FILE` | every signature in a file, no bodies | usb.c API surface at a tenth of the file |
| `graft callers SYM -d N` | who calls, reaches, imports a symbol | flash_erase back to dhara journal prepare_head at depth 2 |
| `graft map` | directory clusters, hubs, hotspots | usr.bin 9107 symbols, sys 4229, sys/arch/rp2040 566 |
| `graft blast --base REF` | what depends on the lines a diff touched | names non-code files it cannot place |
| `graft check --json` | drift between graph and tree | the CI form |
| `graft viz --export DIR` | a self-contained interactive page | 71 MB for the whole tree |
| `graft mcp` | six tools over stdio for an agent | listed below |

The MCP tools are graft_find_code, graft_file_api, graft_trace_calls,
graft_find_all, graft_repo_map and graft_check_freshness, the same six
surfaces. `.mcp.json` at the root registers the server for Claude Code.

## What the model adds

`graft build --deep` writes the concept nodes and the per-symbol summary
and crux. Through the qwen-nvidia appliance, launched with
`QWEN_CHAT_TOOLS=on` so llama-server honors the forced tool call graft
records through, and `eval "$(scripts/graft-consumer-env.sh)"` in that
checkout for the environment, the eight-file sys/arch/rp2040/dev reached
134 of 134 symbols in 2m11s on the served 24576-token slot with the 4B
distill. The same directory at 16384 tokens over two slots failed usb.c
and flash.c with HTTP 400, because graft sends a whole file per request;
a slot needs 16k tokens or more.

With the deep layer, a prose question routes to a concept node that a
lexical hit cannot reach: "how does the usb console hand bytes to the
tty" returns the usb_cdc_acm concept naming the three endpoints, the ring
buffer, and the Microsoft OS 2.0 descriptor set, where the structural
graph returns usb_service and usb.h by name alone. A summary written from
a header prototype is filler ("appends flash swap data read from flash to
a provided buffer"), since the model sees only the declaration; the
summary from the definition lands on the sector-erase loop.

The whole tree runs at about 16 files a minute on the single served slot
through the concept pass, so the concept pass is about 2.6 hours and the
symbol pass follows; the pass is resumable and cached per file hash, and
a later `graft build --deep` resummarizes changed files alone. The
served policy pins `--parallel 1`, so `-j` above 1 buys nothing; the 2B
distill halves the time at lower summary quality.

## Where it misreads C

The C tier is tree-sitter with name resolution and no type information.
A prototype in a header and its definition in a .c file count as two
definitions of one name, and a cross-file caller of an ambiguous name is
dropped rather than guessed: `graft callers flash_swap_append` reports no
callers while flash.c:592 calls it. Nearly every kernel function has a
header prototype, so `callers` and `blast` undercount across the kernel;
`graft grep` finds the same call sites correctly.

`graft build --lsp` with clangd and no compile_commands.json makes it
worse: 1m50s, edges from 14088 to 473728, about 1500 per program `main`
as clangd resolves host libc, and the callers answer unchanged. Upstream
issue 420 records a readiness race in the same path. A compile database
from `bear` around the kernel build is the untested route to real
compiler edges.

## Claude Code wiring

`graft init --agents claude --no-global --no-statusline` writes
`.claude/settings.json` with four hooks, two helper shims, a skill file,
and `.mcp.json`. SessionStart injects the six-command orientation, about
3 KB. UserPromptSubmit runs `graft ask` on the prompt with a 15-second
budget and injects the top three hits; on the structural graph a natural
bug report ("fix the usb cdc console dropping characters under load")
matched nothing and injected a 163-byte pointer instead. PostToolUse on
an edit prints the blast radius and re-syncs the graph; Stop tallies
tokens for `graft stats`. `.claude/` is excluded by the global gitignore
on this host, so the hooks and skill stay per developer and `.mcp.json`
is the part the tree carries.

## Standing under the evidence rules

Everything graft writes is derived: the structural graph from the source
it parsed, the summaries from a model reading one file at a time. A hit
is a lead that names a `file:line`; the source at that line settles the
claim, and a summary ranks below a comment that agrees with the code.
`graft/` stays ignored and is never evidence.
