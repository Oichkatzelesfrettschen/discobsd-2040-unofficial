# PDP-11 V7 host reference

The host-only runner executes a behavior fixture inside a PDP-11/45 running
Seventh Edition Unix under Open SIMH. The fixture covers shell redirection,
`sort`, `sed`, `awk`, `tail`, hard links, `cmp`, and filesystem byte counts.
The runner validates the complete RL02 image before execution and runs the
guest against a private copy, so the supplied image remains unchanged.

The repository contains the SIMH command script and expected transcript. The
repository contains neither simulator code nor a Unix disk image. The runner
opens no network device and performs no download. The pinned image identity is:

```text
size:    10485760 bytes
sha256:  235426852d2fdc2b7b3432f46bb2174d579a6e730f84d66f1464a2ae564a1c81
kernel:  rl2unix
device:  RL02 unit 0
```

Run the structural runner tests with:

```sh
PYTHON="${PYTHON}" bmake check-pdp11-reference
```

Run the external-image oracle with:

```sh
PYTHON="${PYTHON}" PDP11_V7_IMAGE=/path/to/unix_v7_rl.dsk \
    bmake check-pdp11-v7
```

Set `PDP11_EVIDENCE_DIR` to retain the raw simulator output, normalized guest
transcript, and provenance JSON. The provenance names the resolved simulator,
its SHA-256 digest and banner, the host package owner when available, and the
input/profile/transcript digests. The runner requires an empty destination and
refuses to overwrite retained evidence. Do not commit the evidence directory.

The transcript normalizer changes line endings, removes the harness-owned
`PDP11_PROMPT` pacing token, and changes one inode number. The normalizer first
requires both hard-link directory entries to report the same inode, then
replaces that shared filesystem-assigned value with `<same-inode>`. Every other
byte between the oracle markers remains part of the comparison.
