# Coherent boundary mechanisms and DiscoBSD exit ownership

The source donor remains `gspu/Coherent` at
`a00b86ea22c66625cf1838a71c06129f5eda2b49`; the live `master` lookup on
2026-11-12 UTC returns that same commit. DragonFlyBSD retains the preferred
current comparison role. The [donor review](coherent-donor-review.md) owns the
broader file selection and licensing disposition.

## The float token requires separate extent and acceptance states

Pinned `mwc/romana/relic/b/lib/libc/stdio/_dscan.c` has SHA-256
`2feaf7761d2984118c940acd0c6da778654932fd5e50d42f554b58cd863d3ad0`.
Its caller `_scanf.c`, SHA-256
`50686d67756f9d6e8f016e25564708878509f360560ca73db55a01dfa1463c5e`,
allocates `char buf[MAX_WIDTH]` with `MAX_WIDTH=128` and passes a width up to
128. `_dscan` stores up to that many token bytes and then stores a NUL.
The required storage bound is `capacity >= token_extent + 1`. An all-zero
128-character token supplies a representable value and isolates capacity
from numeric overflow.

A host-source experiment uses GCC 16.2.1 20260810 on x86-64 Linux, GNU17,
AddressSanitizer and UndefinedBehaviorSanitizer. The donor file remains
byte-identical; command-line names isolate its functions, `__VOID__` becomes
`void`, and host stdio/strtod supply its dependencies. Those substitutions
bound the evidence: the experiment executes neither a historical Coherent
binary nor its original libc. The original caller uses an automatic buffer;
the witness uses an exactly sized allocation to expose the same invalid index.

| Input | Destination bytes | Result |
| --- | ---: | --- |
| 127 zero digits | 128 | 127 consumed, value 0, EOF, clean exit |
| 128 zero digits | 129 | 128 consumed, value 0, EOF, clean exit |
| 128 zero digits | 128 | AddressSanitizer rejects a one-byte write at `_dscan.c:115`; process exit 1 |
| `12.5x` | 128 | 4 consumed, value 12.5, next byte `x` |
| `1e+x` | 128 | 3 consumed, value 1, next byte `x` |
| `+x` | 128 | 1 consumed, value 0, next byte `x` |

The first two cases calibrate the capacity boundary in both directions.
The valid decimal controls conversion and pushback. The incomplete tokens
show that `_dscan` returns consumed length without validating a final accepting
state; `_scanf` tests only for a zero length before assignment. Host strtod
values in these rows belong to the substituted converter. POSIX input-item
rules, rather than the host's scanf behavior, decide a target repair.

The reusable design separates `(extent, accepting_state, stored_digits,
conversion_status)`. A bounded buffer requires space for termination; adding
one byte alone leaves incomplete-token acceptance unresolved. DiscoBSD's
`doscan_float.c` already stages significant digits and a decimal exponent
instead of every input byte. Its next deciding tests feed incomplete exponents,
width cuts at each grammar transition, long zero runs and rounding-sensitive
tails through the actual scanner, checking assignment, stream position and
precision separately. No donor import or complete numeric-conformance claim
follows from this experiment.

## Signed magnitude and pointer formation constrain modernization

Coherent `mwc/romana/relic/b/lib/libc/gen/strtol.c` has SHA-256
`f5f686821fed90f2bb63e7bb4f1ed80618826b964a54ae25c3d20fab32722788`.
Its signed path sets quotient/remainder from `LONG_MAX` for both signs.
For a 32-bit long in base ten, the threshold is `(214748364, 7)`; the last
digit of the magnitude of `-2147483648` is 8. The source therefore marks the
representable minimum as overflow and sets ERANGE while returning LONG_MIN.
This is a source-and-arithmetic derivation, not execution of that donor file.
The positive control `2147483647` meets the threshold. DiscoBSD's existing
`stdlib/strtol.c` selects the unsigned magnitude of LONG_MIN for negative
input, and `tests/libc_contracts/strtox_test.c` explicitly checks unchanged
errno at LONG_MIN. Preserve that implementation and witness.

Coherent qsort's initial `sp = stack` is followed by `sp--` after the first
pop. C17 pointer arithmetic permits elements and one-past-end, so even a
non-dereferenced pointer before the first element violates the object bounds.
Replace such sentinels with an integer count: admission requires
`count < capacity`, push stores at `stack[count++]`, and pop requires
`count != 0` before reading `stack[--count]`. This rule removes the pointer
formation defect; it supplies no worst-case comparison bound. The original
384-byte ILP32 work-array estimate still needs comparison against DiscoBSD's
smaller-partition recursion. Neither queue representation is a new algorithm.

## The exit registry exposes a target defect and its repair

Additional pinned Coherent sources are:

| Path | SHA-256 |
| --- | --- |
| `mwc/romana/relic/b/lib/libc/stdlib/atexit.c` | `98ad841abd1726838b3095a0b226a00dc6756aaf8d7ab1a2f72cfbab881ef2e7` |
| `mwc/romana/relic/b/lib/libc/stdio/exit.c` | `bebf061d9926c183d4b447e51256fd196914e9fc7eee78f430bb2c3292b74b21` |
| `mwc/romana/source/4.2.x/usr/include/stdlib.h` | `9e6309c1891757a52aa0cf9dc4fafcc9557c27edd6eb9739afc43527260dbd55` |

Coherent separates a 32-slot callback array from the exit member's count and
cursor. Its exit loop decrements the live count before calling each callback.
That mutation timing makes the remaining registry visible to new registrations.
The two independent capacity macros, `ATEXITN` and `_ATEXITN`, are a drift risk;
a target representation derives bounds from one declaration. A 32-slot array
costs 128 bytes for four-byte function pointers, plus count/cursor and alignment.
A linked representation pays node and allocator overhead only when used.
Allocation-independent admission is a resource-policy choice, distinct from
the standard's registration-capacity floor and documented failure result. With four-byte pointers,
DiscoBSD's selected head and static first node cost 12 bytes; each additional
registration costs 8 bytes plus allocator metadata. For `n >= 1`, the modeled
storage is `12 + (n - 1) * (8 + allocator_overhead)`, compared with Coherent's
136-byte fixed array/count/cursor. These sums exclude unrelated errno storage,
linker effects and allocator rounding. Zero-, one-, four- and 32-registration
consumer maps and peak-allocation measurements decide quick-exit storage policy.
The archive member selected by an exit-only consumer remains a separate cost.
The [allocator experiment](callback-allocation-accounting.md) measures dynamic
block extents and cold break growth independently, including calibration of
both node-size and allocator-granule controls.

[POSIX Issue 8 exit](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exit.html)
requires reverse registration order with the exception needed for registrations
after earlier handlers already execute. Its retained HTML hashes to
`b85dfdbb471577efcf48002170962e2db6aa67d129b27aea97a26b6b211c4cf9`.
The DiscoBSD comparison base is
`55d78aa0622eb1021b71aa22632ef0ed105157bd`. Its saved-list traversal advances through `p->next` even when a handler
inserts new entries at `__atexit`. A witness registers A and then B; B registers
C and then D. The original source emits `BA!` instead of `BDCA!`, where `!`
denotes modeled stream cleanup. The source correction pops the live head
before invoking it. The loop therefore observes all subsequently inserted
handlers in reverse order and reaches cleanup after the registry empties.

`tests/libc_contracts/exit_lifecycle.py` compiles the production exit/atexit
sources under private names with modeled allocation, cleanup and termination.
Six cases include a handler registering when the static node is the final
pending entry, duplicate callbacks, 32 registrations, failed allocation with
queue preservation, and an empty registry. Three compiled behavioral mutations
restore the saved traversal, omit cleanup or change status; each exits 1 with
a named lifecycle mismatch. GCC native, GCC ILP32 and Clang native strict-C17
runs pass the good cases and reject those controls. The original callback
node ownership and separate archive members remain intact; completed heap
nodes retain process-lifetime storage until termination.

The root `check-libc-exit-lifecycle` target reaches this gate. The firmware
host aggregate and required ILP32 execution inventory own release qualification.
A matched standalone Cortex-M0+ object experiment uses ARM GCC 16.2.0,
`-std=gnu17 -Wall -Wextra -Werror -fno-common -mcpu=cortex-m0plus -mabi=aapcs
-mlittle-endian -mthumb -mfloat-abi=soft -ffreestanding -Os -nostdinc`, the
target include directory and private stdio include directory, plus
`-fstack-usage`. Both objects contain 36 text bytes, zero data bytes and 8 BSS
bytes; both import only `_cleanup` and `_exit`. Compiler stack reports change
from 16 bytes to 8 bytes. Objdump confirms a four-register push in the saved
traversal and a two-register push in the candidate: the remaining-node pointer
no longer survives across a callback in a callee-saved register. The candidate
instead reloads and updates the registry. Callback cost, whole-program peak
stack, execution time and linked-image costs require separate measurements.
The clean logged RP2040 build completes with exit 0. Native and ILP32 sanitizer
runs also pass the six cases and reject the three behavioral controls.
These results retain kernel teardown as a separate execution obligation. The original `_Exit` bypass witness remains
in `tests/c17_headers/exit_probe.c`. Quick-exit adds an independent registry;
it must preserve normal-exit isolation and receive its own resource decision.

## DragonFlyBSD quick-exit traversal has the same registration boundary

The source search uses `git show` at DragonFlyBSD revision
`805ca132aededa6ae4c8b3499a2d5c934d78fe56`, path
`lib/libc/stdlib/quick_exit.c`. The source SHA-256 is
`3b2b334f9db8c945930ad57a9f3f5b8edfe6cca72ad8365a5704c57c1e28d571`.
Its registration function prepends an allocated node to `quick_exit_fns`;
its termination loop walks a saved `fn` through `fn->next`. A callback's
successful registrations therefore enter the head without entering the saved
traversal. The same mutation boundary explains the DiscoBSD exit defect above.

[POSIX Issue 8 quick_exit](https://pubs.opengroup.org/onlinepubs/9799919799/functions/quick_exit.html)
requires reverse registration order, with the exception for functions already
called when a new registration occurs. It also requires isolation from normal
exit handlers and final termination as if through `_Exit(status)`.
The retained reference HTML hashes to
`6112e047becb7241ad64fd9458ec381bcb6aa59ab5500a0639d6dcdd15920b46`.
These rules distinguish permissible callback registration from recursive exit
or mixed exit/quick-exit calls, whose behavior is undefined.

| Host-source case | Retained donor | Live-head-pop control |
| --- | --- | --- |
| A then B; B registers C then D | `BA`, rejection against `BDCA` | `BDCA`, pass |
| B alone; B registers C then D | `B`, rejection against `BDC` | `BDC`, pass |
| Duplicate callback | `AA`, pass | `AA`, pass |
| 32 registrations | 32 calls, pass | 32 calls, pass |
| Failed allocation after A | A remains registered, pass | A remains registered, pass |
| Empty registry | Status 7, pass | Status 7, pass |

The fixture sets `__isthreaded=0` and supplies lock declarations that permit
host compilation. The experiment establishes the single-thread traversal
behavior under modeled allocation and termination; it establishes neither
DragonFlyBSD runtime behavior in threaded processes nor a complete quick-exit
implementation. The control only changes traversal to pop the live head before
the callback. Callback node reclamation, lock ownership, independent registries,
target storage policy and native packaging retain separate obligations.

The reusable invariant places the mutation before the callback, because the
callback can mutate the registry. Coherent's count decrement and the linked
live-head pop express that same ordering through different representations.
The algorithm has prior art; the contribution is a calibrated boundary witness
that prevents donor preference from concealing a target contract violation.

The following replay uses a content-pinned donor and the retained exit fixture.
It compiles only the inspected translation unit and generates temporary files;
it executes no donor build scripts. `CC` selects a host C17 compiler.

```sh
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
: "${DRAGONFLY_DONOR:?select the donor checkout}"
export PYTHON DRAGONFLY_DONOR
"${PYTHON}" - <<'PY'
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

revision = "805ca132aededa6ae4c8b3499a2d5c934d78fe56"
source = subprocess.check_output([
    "git", "-C", os.environ["DRAGONFLY_DONOR"], "show",
    revision + ":lib/libc/stdlib/quick_exit.c"])
if hashlib.sha256(source).hexdigest() != (
        "3b2b334f9db8c945930ad57a9f3f5b8edfe6cca72ad8365a5704c57c1e28d571"):
    raise SystemExit("donor source identity differs")
fixture = Path("tests/libc_contracts/exit_lifecycle_test.c").read_bytes()
if hashlib.sha256(fixture).hexdigest() != (
        "a0f6f077b0898960b6cfd29a5e6674e9e6b4a45cae5cf48d443c6cd20d416c68"):
    raise SystemExit("fixture identity differs")
fixture = "int __isthreaded;\n" + fixture.decode().replace('!";', '";')
before = "for (fn = quick_exit_fns; fn != NULL; fn = fn->next) {"
after = ("while ((fn = quick_exit_fns) != NULL) {\n"
         "               quick_exit_fns = fn->next;")
original = source.decode()
if original.count(before) != 1:
    raise SystemExit("donor traversal anchor differs")
compiler = shlex.split(os.environ.get("CC", "cc"))
flags = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]
renames = ["-Dat_quick_exit=db_atexit", "-Dquick_exit=db_exit",
           "-Dmalloc=exit_test_malloc", "-D_exit=exit_test_terminate"]
with tempfile.TemporaryDirectory(prefix="quick-exit-donor-") as temporary:
    work = Path(temporary)
    (work / "fixture.c").write_text(fixture)
    (work / "libc_private.h").write_text("extern int __isthreaded;\n")
    (work / "spinlock.h").write_text(
        "typedef int spinlock_t;\n#define _SPINLOCK(p) ((void)(p))\n"
        "#define _SPINUNLOCK(p) ((void)(p))\n")
    for label, text in (("retained", original),
                        ("live-head-pop", original.replace(before, after))):
        (work / "donor.c").write_text(text)
        subprocess.run(compiler + flags + renames + ["-c", str(work / "donor.c"),
                       "-o", str(work / "donor.o")], check=True, timeout=60)
        program = work / label
        subprocess.run(compiler + flags + [str(work / "fixture.c"),
                       str(work / "donor.o"), "-o", str(program)],
                       check=True, timeout=60)
        for case in ("nested", "last", "duplicate", "capacity", "failure", "empty"):
            result = subprocess.run([str(program), case], capture_output=True,
                                    text=True, timeout=10, check=False)
            failures = {
                "nested": "lifecycle mismatch: events=BA expected=BDCA status=7\n",
                "last": "lifecycle mismatch: events=B expected=BDC status=7\n"}
            diagnostic = failures.get(case, "") if label == "retained" else ""
            if result.returncode != bool(diagnostic) or result.stderr != diagnostic:
                raise SystemExit(f"unexpected {label}/{case}: {result}")
            print(label, case, "expected rejection" if diagnostic else "pass")
PY
```

## Scanner witness replay

Place the following C fixture in a caller-owned build directory as `probe.c`.
Its SHA-256 is `2b9ada06d12dd0414b250e58c2b8631f5f3e3779fcb459be239471ed60b7adc1`.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int coherent_dscan(char *, FILE *, int, double *);

int
main(int argc, char **argv)
{
    char input[129];
    size_t length, capacity;
    char *buffer;
    FILE *stream;
    double result = -17.0;
    int consumed, next;

    if (argc != 3)
        return 2;
    capacity = (size_t)strtoul(argv[2], NULL, 10);
    if (strcmp(argv[1], "127") == 0 || strcmp(argv[1], "128") == 0) {
        length = (size_t)strtoul(argv[1], NULL, 10);
        memset(input, '0', length);
        input[length] = '\0';
    } else {
        length = strlen(argv[1]);
        if (length >= sizeof(input))
            return 2;
        memcpy(input, argv[1], length + 1);
    }
    buffer = malloc(capacity);
    stream = tmpfile();
    if (buffer == NULL || stream == NULL)
        return 3;
    if (fwrite(input, 1, length, stream) != length ||
        fseek(stream, 0, SEEK_SET) != 0)
        return 4;
    consumed = coherent_dscan(buffer, stream, 128, &result);
    next = fgetc(stream);
    printf("input=%s capacity=%zu consumed=%d value=%.17g next=%d\n",
        argv[1], capacity, consumed, result, next);
    if (fclose(stream) != 0)
        return 5;
    free(buffer);
    return 0;
}
```

The following commands require the content-pinned donor checkout and a native
GCC sanitizer runtime. The commands compile source and perform host temporary
file I/O; they do not execute donor build scripts or archived executables.

```sh
set -eu
: "${COHERENT_DONOR:?select the pinned donor checkout}"
cc -std=gnu17 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -D__VOID__=void \
    -D_dscan=coherent_dscan -D_dassign=coherent_dassign \
    "$COHERENT_DONOR/mwc/romana/relic/b/lib/libc/stdio/_dscan.c" \
    probe.c -o probe
./probe 127 128
./probe 128 129
./probe 12.5x 128
./probe 1e+x 128
./probe +x 128
if ./probe 128 128 > overflow.log 2>&1; then
    printf '%s\n' 'expected capacity violation remains undetected' >&2
    exit 1
fi
rg 'ERROR: AddressSanitizer: heap-buffer-overflow' overflow.log
rg 'WRITE of size 1' overflow.log
```

Source hashes, positive boundary controls and the diagnostic class constrain
interpretation. A compiler failure, unrelated crash or missing sanitizer
runtime supplies no confirming result. The archive review retains all other
unreviewed Coherent families and source-permission questions.

The matched clean distribution comparison retains fifteen increases of one to
four packed bytes per executable with unchanged block allocations and loaded
sizes. [The exception review](exit-lifecycle-resource-exception.md) records the
exact rejected default verdict, alternatives, margins and owner approval.
