# RP2040 configured-source call-graph capture

`tools/capture_rp2040_callgraph.py` records a PICO source denominator and
produces a GNU cflow graph and cscope index. The denominator expands the
generated PICO Makefile's `CFILES` with bmake, then includes generated
`ioconf.c` and `vers.c`. The cscope input also includes tracked headers under
`sys`. The capture records the effective build variables, source and tool
identities, diagnostics, command arguments, and artifact hashes.

After a logged build has generated the PICO configuration and version files,
select `PYTHON` and use a fresh output directory:

```sh
"${PYTHON}" tools/compiler_matrix_identity.py --root . --manifest .build-logs/pico-graph-input.json \
    --generated-input sys/arch/rp2040/compile/PICO/ioconf.c \
    --generated-input sys/arch/rp2040/compile/PICO/swapunix.c \
    --generated-input sys/arch/rp2040/compile/PICO/vers.c --record-only
"${PYTHON}" tools/capture_rp2040_callgraph.py --root . \
    --manifest .build-logs/pico-graph-input.json --output .build-logs/pico-callgraph \
    --bmake "$(command -v bmake)" --cflow "$(command -v cflow)" \
    --cscope "$(command -v cscope)"
```

The capture checks source identity before and after execution. `direct.dot`
is a lexical graph of the C sources, not the result of preprocessing with the
recorded PICO flags. Cflow can report conditional duplicate definitions and
does not resolve indirect calls. Cscope indexes lexical references, including
headers beyond the active configuration. Neither artifact proves reachability,
callback target values, assembly edges, interrupt paths, or runtime behavior.
The tool repeatedly starts cflow at the discovered symbols with depth two
until the symbol set stabilizes. Each pass retains direct edges while avoiding
the combinatorial expansion of complete call paths. The graph and index share
a 120-second deadline and a 64 MiB output budget; output streams to files so
graph expansion cannot fill host memory.

The nonempty controller and service tables,
executable-format, nlist, assembly and flash SRAM closure
roots still require source, type and pointer-value audits. A section-collection
decision also requires linked symbol and discarded-section evidence. The
lexical capture alone does not authorize garbage collection.

The local `.build-logs/pico-callgraph-capture-v3` capture uses source-manifest
SHA-256 `159c0e3b0ae785b1407d9a1446dc615695a65a6f85c39d6d1becdb9f697e6865`.
The ignored capture contains 80 C sources, 295 cscope inputs and 2,299 DOT
edges after four passes. Its manifest SHA-256 is
`bcb3129445abdf3b50ec0bedcfc709a1fd266478b15429bb7076a36f67c08680`.
Each cflow pass reports 20 diagnostic lines, including duplicate conditional
definitions; those diagnostics qualify the graph. The capture precedes this
documentation update and is not a final-candidate receipt.

For example, `cscope -d -L -0 exec_hsaout_check -f
.build-logs/pico-callgraph-capture-v3/cscope.out` finds the function's entry
in `sys/kern/exec_conf.c`, its declaration, and its definition. A direct
caller query (`-3`) returns no caller for that table entry. The symbol query
identifies a candidate indirect edge; linked-image inspection and runtime
execution establish separate properties of that edge.

The `check-rp2040-execsw` cross gate addresses the linked executable-format
table for each configured kernel. It compares each table word with the named
linked function symbol, verifies its Thumb bit and executable load mapping,
and checks the associated name and initialized count. A PICO ELF mutation
substitutes another valid handler in each slot; the gate rejects every image.
Additional mutations cover a null pointer, cleared Thumb bit and wrong name
in each slot, plus a wrong count and truncation.

The ignored `.build-logs/execsw-linked-pointer-capture-v2` records four kernel
hashes, verifier environment, gate output and exit status. Its verifier-source
manifest SHA-256 is
`f24cc304539835f94eee5f81ead44ebbac62280e249c4e8cf5ea5ea39422031b`;
the kernel hash list SHA-256 is
`83bef134c481947b296d05ba3bc3f820985272bd81443bc43b1580b8d13bccd2`.
The capture predates the expanded per-slot mutations and this documentation
edit. The images come from the separately recorded logged build, and the verifier
source manifest does not prove their source identity. The check relies on the
ELF symbol table and does not establish runtime dispatch or other indirect
root families.

The `check-rp2040-vectors` cross gate compares every linked exception-vector
word with its named symbol and checks reserved slots and Thumb targets. The
fixture also compares the source slot list and rejects a source substitution,
changed vector words and cleared Thumb bits. Weak handlers may share the
default-handler address; the gate validates the resolved value rather than
requiring handlers to be distinct. Runtime exception delivery remains separate.

The `check-rp2040-syscalls` cross gate compares the linked syscall handler
and argument arrays with the syscall master under the disabled-network and
disabled-glob configuration. It rejects wrong addresses, argument counts,
cleared Thumb bits and a wrong initialized syscall count. The pointer checks bind to
the linked symbol table; syscall execution and other configurations require
their own controls.

The `check-rp2040-devsw` cross gate compares linked block and character switch
fields and counts against the configured `conf.o` relocations and constants.
It checks all four kernel configurations and calibrates PICO and
PICOCALC_PSRAM with field, count, same-signature handler and object-relocation
mutations. The object provides the selected build's initializer, so a separate
source and configuration identity is required. The check does not establish
runtime driver behavior.

The `check-rp2040-fileops` cross gate checks the linked `Fops` selection table
and the inode, socket and pipe callback tables against their named symbols.
The PICO fixture compares the source initializers and rejects substituted
same-field targets and cleared Thumb bits. File-type selection and operation
semantics remain runtime claims.

The `check-rp2040-ioconf` cross gate compares generated device initialization
words with the matching configuration object and validates named driver
descriptors and probe addresses. The shipped configurations have empty
controller and service arrays; the gate checks their BSS boundaries but
does not execute startup. Source/object identity and nonempty controller or
service tables require additional evidence.

The configured RP2040 terminal path has no `linesw` table: a source search
finds `nldisp` only as an unused declaration, and the four captured linked
images contain neither symbol. `TIOCGETD` reports zero and `TIOCSETD` accepts
only zero in `sys/kern/tty.c`; terminal drivers call the tty routines directly.
Their open routines also assign `t_oproc` callbacks at runtime. The
`check-rp2040-tty-roots` cross gate checks the corresponding object
relocations and linked literals for configured UART, USB and PicoCalc drivers.
The fixture checks source assignments and mutates linked literals, including
same-signature substitutions and cleared Thumb bits. The gate does not prove
the store executes or establish terminal behavior; the `nldisp` observation
is limited to the searched tree and the captured linked kernels. Optional PTY
output callbacks are outside the four captured configurations and remain open.

The `check-rp2040-nlist` cross gate compares configured export relocations
with linked names and addresses. Object provenance and runtime lookups remain
separate evidence.

The `check-rp2040-callout-roots` cross gate checks selected scheduler, terminal
and PicoCalc timeout callback references against object relocations and linked
literals. The PICO configurations have three checked references; PicoCalc
configurations add five. The fixture verifies source timeout references and
rejects changed literals, cleared Thumb bits and alternate same-signature
callbacks. The gate does not prove callback registration, timing, argument
identity or delivery; those remain runtime and behavior-test obligations.

## XIP-disabled intervals and SRAM code

The ignored `.build-logs/ramfunc-phase-audit` captures the `.ramfunc`
disassembly of all four linked kernels and the corresponding `flash.o` and
`machdep.o` relocations. The PICOCALC_PSRAM capture also includes
`psram_xfer.o`. The `sha256.txt` manifest has SHA-256
`a77c100841d394d93a3569700c8b620f2d6285cd397aaa17b17e12fb8af1ca82`;
`psram-sha256.txt` has SHA-256
`e041368a58ef99c2d6e3531fd5c747e85668b5d2d8bfd24c1c79a80bb22d7da3`.
The files record the object, ELF, source and parameter hashes. They do not
independently prove that those objects were built from the recorded sources.
The prior logged-build receipt records its own successful build; the capture
postdates that receipt and is not a final-candidate build identity.
The local `verify_observed_sequence.py` records the direct and indirect call
order in each captured erase and program body and rejects a removed indirect
call. The script SHA-256 is
`250a6c783da465e1ecea822c91b8a5399257eea3997342d04c22263624d6a85b`;
the `call-sequences.txt` SHA-256 is
`8b371e5b354cd33c2b371093dbefda4cf452dd740818d19ce5744b46b2abe807`.
The script does not resolve an indirect target or inspect every memory load.

The PICO linked image places `flash_erase`, `flash_program` and
`flash_enter_xip` in SRAM. Its `.ramfunc` also contains veneers that jump to
the flash-resident `watchdog_site` and `clock_catchup` functions. In the
captured erase and program sequences the watchdog call occurs before the
ROM exit-XIP call, and the watchdog and clock calls occur again after the
`flash_enter_xip` call. A rule forbidding every flash address anywhere in
`.ramfunc` therefore rejects the captured image even though those calls lie
outside the source-designated XIP-disabled interval. The rule for a future
closure verifier must track the interval, direct branches, literal loads,
indirect call provenance, buffer addresses and the return from the copied
boot stage. A changed call order or a flash load inside the interval is a
negative control; a blanket flash-reference check is not an adequate oracle.

`flash_rom_init` resolves the ROM functions and copies the second-stage boot code
before an erase or program; the XIP-disabled interval calls the selected ROM
erase or program routine, flushes the cache and executes the SRAM copy. The
captured instructions and source ordering do not establish the values returned
by the running ROM lookup, the program-data buffer's full provenance, ROM
internal behavior, core-1 activity, interrupt masking on physical hardware,
or that XIP is functional on return. Those remain required runtime and
specification checks. `bootsel_pressed` has a separate flash-unavailable
interval while the QSPI select pin floats. PICOCALC_PSRAM also places
`psram_frame` and `psram_recover` in `.ramfunc` to bound PIO FIFO service
latency; their timing contract is distinct from the flash programming interval.
The capture alone does not authorize section collection.

The ignored `.build-logs/exec-dispatch-arm-qemu-controls` capture exercises
the extracted production `exec_check` body and `execsw` initializer with
recording inode, access, read and loader stubs. System ARM GCC 16, Arm GNU GCC
13 and Clang 23 each execute eight optimization levels and twelve controls or
mutants, for 288 qemu-user runs. The controls include packed-enabled and
packed-disabled tables and a null slot; the mutants change permission and read
failure handling, residual accounting, error continuation, and the packed
handler address. The source-manifest SHA-256 is
`6d65dac97de9f3085fa97bbc7574ec13c6513cb863b4528d6cb469e8a4af5700`, and
the gate log SHA-256 is
`9c1af5d75d0ff1ed2947b835d5c2cb2c90d2b2e1cf9154b1dd70204af487936e`.
The capture predates this documentation edit. The emulator runs exercise the
dispatcher with stubbed dependencies; physical privilege, filesystem, decoder,
recursive interpreter execution, and timing remain separate evidence.
