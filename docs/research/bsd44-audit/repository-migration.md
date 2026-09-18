# Discobsd audit path migration

The host renamed the two repositories while this audit was active:

| Role | Previous path | Current path | Current observed state |
|---|---|---|---|
| Notes | `~/Github/rpi` | `~/Github/discobsd-pico-notes` | `main`, tracking `origin/main`, clean at the live check |
| Target source | `~/Github/discobsd` | `~/Github/discobsd-pico-unofficial` | `main`, `b40c1c4fd5032ada8b651588645f33c8f856c985`, clean at the live check |
| Durable scratch | `~/worktrees/discobsd-tmp` | unchanged | retains analyzer logs, objects, the candidate ledger, and LiteBSD search evidence |

The previous paths were absent after the rename. All subsequent source reads,
status checks, and final integrity claims use the current paths. The target
commit in the candidate ledger is the measured `b40c1c4f...` commit, not the
stale handoff commit from before the rename. The notes repository remains
untouched so its observed clean/in-sync state is preserved.

