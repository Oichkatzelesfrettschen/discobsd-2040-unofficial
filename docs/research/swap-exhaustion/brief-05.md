# Brief 05: adversarial review, root-cause analysis and landing of PR #407

This brief is the goal file for one interactive program on branch
`kern/fork-swap-reservation` (PR #407, "fork: reserve the child's swap before
publication and refuse with ENOMEM"). Re-read it at every item boundary and
after every context compaction. The program's durable record is
`docs/research/swap-exhaustion/review-05.md`; the operator reads only that
file's "Handoff" section, so everything that matters goes there.

## State at hand-off

- The branch carries the reservation change (`70916b72`), a merge of main
  (`5cd62ae3`), a clang build fix for the fork gate (`c695ee0c`) and a
  SwapRAM link-verifier and fixture-shim fix (`c138f456`). The first three
  CI failures were: clang rejecting K&R definitions in the fork gate's
  compiles (macOS job); `check-swapram`'s verifier requiring a relocation to
  `swapram_out`, which `vm_swap.c` no longer calls; and the SwapRAM
  evacuation fixture missing a `bzero` declaration.
- The "rp2040 kernel, flash image and gates" job still fails on `c138f456`,
  cause not yet read. The orchestrator stopped a local `logged-check` on this
  worktree mid-run; `.build-logs/rp2040-products.lock.ownership.json`
  remains. If a logged target refuses to start, inspect the host and the
  terminal status receipt and resolve it only through
  `tools/recover_logged_execution.py`, as `AGENTS.md` requires.

## Work, in order

1. Root-cause every CI failure on the branch's latest commit: read the failed
   job logs with `gh run view --log-failed --repo
   Oichkatzelesfrettschen/discobsd-2040-unofficial` (always pass `--repo`),
   separate genuine failures from injected-failure self-tests, and record for
   each the observed line, the mechanism, why local acceptance missed it, and
   the class of gate that would have caught it.
2. Adversarially review the whole PR diff (`git diff origin/main...HEAD`)
   against `docs/research/swap-exhaustion/review-02.md` sections on commits 2
   and 3: transaction ordering in `newproc()`/`fork1()`, every reservation
   state transition, SwapRAM reservation keyed to the child before
   `swapram_inherit()`, the new `wakeup(&runout)` in `swap_image_release()`,
   statistics accounting, `vfork()`, process 1's creation, the non-aligned
   (`#ifndef SWAP_IMAGE_ALIGN`) paths, and the STM32 build. Mark each finding
   FATAL, SERIOUS, MINOR or SOUND with file:line evidence, and calibrate any
   new gate against a known-good and a known-bad input.
3. Fix every FATAL and SERIOUS finding and every CI failure, as new commits
   on this branch. Commit messages: component-and-mechanism subject, a body
   of one to five sentences. No AI attribution of any kind.
4. Run the complete local acceptance once, after the last fix:

   ```
   export PYTHON=/usr/bin/python3
   bmake MACHINE=rp2040 logged-build
   bmake MACHINE=rp2040 logged-check
   bmake MACHINE=rp2040 check-kernel-headroom
   ```

   Each must exit 0 with zero warnings. Also build the fork gate under
   `HOST_CC=clang` to match the macOS runner.
5. Push the branch (`git push origin kern/fork-swap-reservation`; never force)
   and watch CI with `gh pr checks 407 --repo
   Oichkatzelesfrettschen/discobsd-2040-unofficial` until every check
   settles. Repeat the root-cause and fix loop for any new failure, at most
   three rounds; a failure that a rerun clears and that no diff line reaches
   is a flake, recorded with its evidence. Do not merge the PR.

## review-05.md

Write the RCA table, the review findings, the fixes with their commits, the
gate results with counts, and the CI outcome. End with a "Handoff" section of
at most fifteen lines: the PR's final state, each commit added, anything
unresolved, and anything that needs a board run.

## Standing rules

- Work only in this worktree. `git commit` and `git push` of this branch are
  authorized; history rewrite of pushed commits, force push, merging, and
  touching other branches or worktrees are not.
- Network use is limited to `git push`/`git fetch` of this repository and
  `gh` calls against it.
- No package installs. Never open `/dev/ttyACM*` or `/dev/serial/*`, and never
  run `discobsd-flash` or a `picotool` command that talks to a device;
  host-only `picotool uf2 convert` inside the build is permitted.
- Keep secrets, local absolute paths and private hostnames out of commits and
  of `review-05.md`.

## Commit protocol (added by the orchestrator)

The sandbox mounts this worktree's registered Git metadata read-only, so
`git add` and `git commit` fail on `index.lock`; the orchestrator's shell can
write it. Do not retry Git writes. For each commit, in series order, write
`.commit-requests/NN.paths` (one repository-relative path per line, every
path the commit adds, modifies or deletes) and `.commit-requests/NN.msg`
(subject line, blank line, body), with NN starting at 01. A path belongs to
exactly one request; split a file's hunks across commits only by finishing
the earlier commit's state first. When every request of the series is written,
write `.commit-requests/READY` and keep working on acceptance. The
orchestrator commits the requests in order, writes each resulting hash to
`.commit-requests/NN.hash`, and pushes when the brief authorizes it. Cite
those hashes in the Handoff section. `.commit-requests/` is excluded from Git.

## Wrap-up order (added by the orchestrator; supersedes the remaining work items)

Stop new implementation and stop or skip any long acceptance run. Then:

1. Write commit requests now for every change in the working tree, in a
   series that builds commit by commit, following "Commit protocol" above,
   and write `.commit-requests/READY`. Leave nothing uncommitted except
   `.commit-requests/`.
2. Spend the rest of the session on the adversarial review only: attack the
   whole branch diff (`git diff origin/main...HEAD` plus the requested
   commits) for correctness, boot safety, every changed invariant, stale
   consumers of removed interfaces, and gates that would pass on broken input.
   Do not fix anything. Record each finding in `review-05.md` as FATAL, SERIOUS,
   MINOR or SOUND with file:line evidence, the failing scenario, and the fix
   you recommend; the orchestrator resolves them.
3. End `review-05.md` with the "Handoff" section (at most fifteen lines): the
   commit requests written, the findings ranked, which acceptance gates ran
   and which did not, and anything that needs a board run. Write the review
   file itself as the last commit request if it changed after step 1.
