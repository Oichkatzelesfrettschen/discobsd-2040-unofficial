# Callable-slot source review and calibration repairs

The owner approves a bounded read-only source review of
`af9b6b176eae93687cf84baa7c3ffdf79f7a9122` against
`9adc7a6f8db95f95bdc5a744d1aa7a27da9230a2` on 2026-11-15. Codex CLI 0.169.0
uses `gpt-6.1-sol`, medium effort, standard tier, read-only sandbox and approval
policy `never`. The accepted session is
`01a1e0cd-ab50-74d3-88de-b5db03f60a38`; its recorded turn context confirms
the model, effort and sandbox.

The launch disables all 19 configured connectors, plugins, remote plugins,
apps and both agent-enabling paths. Preflight reports zero enabled connectors.
The retained command list contains source/Git inspection and required startup
instruction, skill and memory reads. The reviewer executes no repository
programs, tests, builds, edits, delegation, network tools or hardware operations.

| Retained evidence | SHA-256 |
| --- | --- |
| `.build-logs/xip-slots-review-af9b6b17/review.raw.jsonl` | `f987290f248156d1d9f5ecfbd1ceee7f79a635611f516724a39eb5e37be03325` |
| `.build-logs/xip-slots-review-af9b6b17/verdict.txt` | `ac53218bdaaab2f6a619d56dac7f197f870f8935ee46ff0251ed69ca38b63701` |
| `.build-logs/xip-slots-review-af9b6b17/preflight.json` | `a592f415a475bfa805769ce362662f6f0aab7e749a6508c9b85274840a43577c` |
| `.build-logs/xip-slots-review-af9b6b17/prompt.txt` | `30f8b7be6f92ce6f201926a3fd7b11d0ae69f5dbc2c2886518999edb00374058` |

## Findings and deciding counterexamples

Three P2 findings concern fixture validation. The parent executes all three
counterexamples against the reviewed source before applying the repairs.

| Finding | Reproduced observation | Repaired invariant and regression |
| --- | --- | --- |
| A public collision control accepts a private-symbol error | A competing object defines only `__libc_impl_api_xor`; the generic multiple-definition oracle accepts that error as public-collision evidence | The competing public control defines only `api_xor`. The C-locale diagnostic must name that exact public symbol. A private-only duplicate rejects as unrelated evidence |
| Pairwise ownership misses a conflicting alias group | Moving both `api_alias` and its implementation alias to the `api_xor` owner preserves each pair and passes separation | Every declared group shares one implementation section, Thumb address and extent. The paired owner mutation rejects before a separated object is written; final inspection repeats the group check |
| Import inspection misses a wrong public address | Changing the application's imported `api_xor` symbol to the `api_sum` address retains valid type/size and passes all 222 modeled calls | Every imported public address equals its inspected library definition. The same metadata mutation rejects before instruction execution |

The reproductions retain their commands and objects under
`.build-logs/xip-slot-review-reproductions`; the summary is
`.build-logs/xip-slot-review-reproductions.log`. The three new controls live in
`review_controls` in `tests/libc_xip_slots/run.py`. Each malformed construction
must identify its requested symbols before the expected rejection counts.

All four repaired images retain 222 successful calls, totaling 888. The gate
requires seventeen rejected controls. Ruff lint and formatting, focused mypy
checks and the adjacent shared-libc gates retain separate execution receipts.
The parent verifies the repairs through red-to-green regression and the complete
focused suite. The external review verdict remains bound to `af9b6b17`; it
reports findings rather than a review of the later repair.

The review establishes source-derived candidates; executed reproductions and
regressions establish their fixture behavior. Production ABI generation, full
export ownership, flash fit, native imports, admission, lifecycle, signal/fork/
swap behavior and physical-board qualification remain open. The
[identity record](slot-identity.md) owns the mechanism and replay contract.
