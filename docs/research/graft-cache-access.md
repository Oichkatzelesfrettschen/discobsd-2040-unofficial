# Shared Graft cache access

Local inference generates summaries by default. Codex and Claude read the same
retained graphs through a query-only MCP adapter, including when inference is
stopped. Structural navigation is the default; deep summaries are explicit,
bounded and advisory. Cache reads consume client context tokens but trigger
zero generation requests.

## Configure the reader

The guarded cache adapter belongs to the qwen-nvidia workflow deployment.
Its configuration pins the registered DiscoBSD source, terminal graph jobs,
read-only source and graph mounts, and bounded query limits. Keep deployment
paths, graph bindings, credentials and generated caches outside tracked files.
The repository launcher delegates to that configured adapter; it does not
install the deployment or generate summaries.

Select `PYTHON` and set `GRAFT_CACHE_READER` to the absolute path of the trusted
deployment's `cache-reader.sh`. Both variables must be supplied explicitly.
The launcher rejects relative, missing and symlink wrapper paths. Treat the
wrapper as executable configuration: configure only an owner-controlled path.

The root `.mcp.json` registers the launcher for Claude's project scope. A
Claude local-scope registration takes precedence, and user scope follows
project scope. For a local registration, from the registered source checkout:

```sh
: "${PYTHON:?Select the intended interpreter}"
: "${GRAFT_CACHE_READER:?Select the guarded cache-reader shell wrapper}"
claude mcp add --scope local graft -e "PYTHON=$PYTHON" \
    -e "GRAFT_CACHE_READER=$GRAFT_CACHE_READER" -- \
    /bin/sh "$PWD/tools/graft-cache-reader.sh"
claude mcp get graft
```

Codex uses the trusted project's local `.codex/config.toml`. Keep that file
excluded locally, and substitute the selected absolute paths into this stanza:

```toml
[mcp_servers.graft]
command = "/bin/sh"
args = ["<absolute-source-checkout>/tools/graft-cache-reader.sh"]
enabled = true
startup_timeout_sec = 15
tool_timeout_sec = 45
enabled_tools = ["graft_cache_query", "graft_cache_status"]

[mcp_servers.graft.env]
PYTHON = "<selected-interpreter>"
GRAFT_CACHE_READER = "<absolute-guarded-wrapper>"
```

Check `codex mcp get graft --json` from the checkout. Open fresh client
sessions after changing configuration. The adapter accepts its registered
source checkout, not an arbitrary worktree; bind another source only through
the workflow's explicit registration and generation procedure.

## Read the cache

`graft_cache_status` accepts `{}` and returns coverage, recorded/current source
HEAD and the local generator. Populated records measure completeness, not
factual correctness. Use structural queries first:

```json
{"operation":"graft_file_api","arguments":{"file":"sys/arch/rp2040/dev/flash_swap.c"}}
```

Select the bounded deep layer explicitly:

```json
{"layer":"deep","operation":"graft_file_api","arguments":{"file":"sys/arch/rp2040/dev/flash_swap.c"}}
```

Operations also include `graft_find_code` with `query`, `graft_trace_calls`
with `symbol`, `graft_find_all` with `pattern`, and `graft_repo_map` or
`graft_check_freshness` with empty arguments. All operate within the bound
graph and source scope. The server exposes two read tools and rejects build
requests, unknown layers, path traversal and out-of-scope paths.

## Freshness and accuracy

The retained graphs report their recorded revisions. Source excerpts read
live bytes, while summaries belong to their recorded file hashes. Compare
the relevant source hashes before reusing a summary. HEAD equality alone
does not establish freshness for working-tree edits. A freshness query that
reaches the workflow deadline reports an unmeasured result; keep the guard.

The bounded driver cache contains a factual reversal in the header-only
`flash_swap_append` summary: the declaration summary describes reading into
a buffer, while the implementation writes flash. Read the definition before
using the claim. C prototype/definition ambiguity can also hide call edges;
check lexical call sites against source. Whole-tree semantic indexing is
outside the default interactive workflow.

## Refresh and validate

Run the separate guarded local build on a bounded source scope. Retain the
structural graph for navigation and reuse unchanged cached entries. Validate
terminal status, completeness and source accuracy before updating the reader's
graph bindings. Refresh requires the local inference service; cache queries
do not. Hosted experiment graphs stay separate from the local default.

Validate initialization, exact two-tool discovery, status and both layers
with inference stopped. Calibrate rejection checks against successful reads,
then test mutation requests, invalid layers, traversal and outside-scope files.
Record graph hashes before and after queries to establish the measured read
boundary. Keep receipts with the deployment artifacts rather than committing
generated graphs. Local protocol tests establish the reader contract; each
client's connection check establishes that client's configuration surface.

For rollback, remove Claude's local entry with `claude mcp remove graft -s
local`, or set the Codex stanza's `enabled = false`. Claude then applies its
remaining project/user registrations; the project launcher still requires
the deployment variables. Cache contents and local generation settings remain
separate from client configuration.
