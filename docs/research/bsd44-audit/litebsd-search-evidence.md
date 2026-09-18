# LiteBSD source-search boundary

The requested phrase was treated as a repository/source name, not as a
generic BSD-Lite citation. The search was read-only.

## Exact filesystem surfaces

The following paths were tested with `stat` and `find`:

| Path | Observation |
|---|---|
| `~/Github/litebsd` | absent |
| `~/litebsd` | absent |
| `/home/litebsd` | absent |
| `~/Blackhole/repo-cleanup-20260522/retro-computing/litebsd` | directory containing metadata only: `diff-summary.txt`, `recent-log.txt`, `remotes.txt`, `status.txt`, and `tracked-diff.patch` |

The parent-level authority checkout that does exist is
`~/Github/4.4BSD-Lite2`. It remains the source authority for the
backport ledger.

## Phrase and permutation search

`rg -n -i --hidden --glob '!*.git/**'` searched `~/Github` and
`~/Blackhole` for these equivalent spellings:

- `LiteBSD`
- `Lite BSD`
- `Lite-BSD`
- `BSD-Lite`
- `BSD Lite`
- `BSDLite`

The hits were the 4.4BSD-Lite2 authority, existing Discobsd research, a
drive-migration report naming a Windows path, and the deletion snapshot. The
snapshot metadata identifies the remote as
`git@github.com:sergev/LiteBSD.git`; `recent-log.txt` records commit
`01c12e97` changing the getty banner from `4.4 BSD-Lite` to `LiteBSD`.
`status.txt` and `diff-summary.txt` describe a deletion state rather than a
recoverable source checkout. No LiteBSD source files were available to read
or compare.

## Claim boundary

The evidence supports the bounded statement: **no local LiteBSD source tree
was available at the searched paths, and only deletion metadata with remote
identity remained**. The evidence does not prove that the upstream remote is
unreachable or that no other unindexed copy exists. A future source intake
must preserve the remote commit identity, license files, and an immutable
checkout before any LiteBSD-specific comparison is claimed.

