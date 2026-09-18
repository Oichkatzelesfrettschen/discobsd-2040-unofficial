# Reports drafted for matgla/Renode_RP2040

Two issue texts, filed against github.com/matgla/Renode_RP2040 on
2026-09-17 as issues 26 (the SSI series) and 27 (the flash model), with
the patches inline and permalinked, and kept here as sent, with the
disclosure line reworded on the tracker to thank the maintainer for the
models. Each carries its fix as a patch series under
tools/renode/patches, produced by `git format-patch` so `git am` applies
it with its message. Everything below was measured against commit
205a5e4b25440582008a4292074bb07f80a72328, the revision
tools/renode/fetch-renode-rp2040.sh pins, and the part claims were read
off a Raspberry Pi Pico's W25Q16JV with tools/flash-semantics.

Upstream's AGENTS.md pins Renode 1.16.1 and asks for its MIT file header,
its `emulation/tests` xUnit layout, and a `.robot` case per new test. The
patches keep every header untouched, add no test file, and carry no
subject tag: the repository's `[IN]`, `[NF]`, `[IP]`, `[RF]` and `[CI]`
prefixes are not documented, so the subjects are left for the maintainer
to tag. Both reports say so.

sys/arch/rp2040/doc/research/emulation.md records how each emulator
finding was measured; tools/renode/warning-classes.txt carries the log
signature of every defect that shows up as a warning.

---

## Issue 1: the boot ROM hangs in wait_ssi_ready because RP2040XIPSSI latches BUSY

> An LLM assisted in the investigation and the writing of this report and
> its patches. Everything in it was run and read back by a person; what you
> do with it is your call.

**Finding.** `RP2040XIPSSI` stores SR.BUSY in a register field, sets it
before a transfer and clears it after. Any interruption between those two
assignments leaves the flag set forever, and the RP2040 boot ROM's
`wait_ssi_ready`, which every ROM flash helper calls, spins on it. A guest
that erases or programs flash through the ROM eventually stops, at a point
that moves from run to run, with the process still at 1.4 host cores.

**Where the guest is stuck.** `machine StartGdbServer 3333`, then
`arm-none-eabi-gdb`:

```text
pc  0x200404ae
r3  0x18000000            ; XIP_SSI

0x200404aa:  push  {r0, r1, lr}
0x200404ac:  ldr   r1, [r3, #0x28]   ; SR
0x200404ae:  movs  r0, #4            ; TFE
0x200404b0:  tst   r1, r0
0x200404b2:  beq.n 0x200404ac
0x200404b4:  movs  r0, #1            ; BUSY
0x200404b6:  tst   r1, r0
0x200404b8:  bne.n 0x200404ac

TXFLR (0x18000020)  0
RXFLR (0x18000024)  1
SR    (0x18000028)  0x0f      ; BUSY | TFNF | TFE | RFNE
```

That is `wait_ssi_ready` from pico-bootrom-rp2040's
`bootrom/program_flash_generic.c`, copied to SRAM so it runs with XIP off:
wait for the transmit FIFO to empty, then for BUSY to clear. TFE is
satisfied. BUSY never clears.

**Why it latches.** A host stack sample of the CPU thread reads

```text
BaseCPU::CpuThreadBody
BaseCPU::CpuThreadBodyInner
TimeSourceBase::ReportTimeProgress
TimeSourceBase::SynchronizeVirtualTime
BaseClockSource::Advance
BaseClockSource::Update
```

so a managed thread's action runs on whichever CPU thread reports time
progress, and on a two-core RP2040 both cores enter `TransferClock`.
`RecalculateFrequencies` (from the BAUDR write callback and
`OnSystemClockChange`) and the SSIENR write callback stop the clocking
thread from a third path, and the SSIENR edge clears both FIFOs with a
transfer possibly in flight. Whichever of those lands between
`busy.Value = true` and `busy.Value = false` wins.

**Fix.** DW_apb_ssi reports BUSY while a serial transfer is in progress,
so the model derives it: a count of transfers in flight, raised and dropped
in one `try`/`finally`, plus the depth of the transmit queue. A derived
flag cannot latch. The same patch runs the transfer state machine under one
lock, so two cores cannot interleave one command's bytes, and stops the
clocking thread before an SSIENR edge clears the queues rather than after.

**Measurement.** Free-running boots of a DiscoBSD kernel, `emulation RunFor
"30"`, no terminal tester attached, counting runs that reach the guest's
getty banner:

| model | reaching userland |
| --- | --- |
| 205a5e4b plus the FIFO lock (patch 1) | 0 of 6 |
| plus the derived BUSY flag (patch 2) | 6 of 6 |

A Robot suite does not see this: `Create Terminal Tester` pauses the
emulation at every wait and serializes the two CPU threads enough that the
race rarely fires. The suite passed against the unfixed model. A
free-running run is what separates them.

**Reproduction.** Any guest that programs flash through the ROM's
`flash_range_program` under XIP-off and polls afterwards. The kernel used
here is github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial, target
PICO_UART, booted from your `bootroms/rp2040/b2.elf` at 0 with the image at
0x10000000; `tools/renode/machine.resc` there is the machine, and
`tools/renode/boot.robot` the suite.

**Two smaller defects in the same file, fixed in the same series.**

Patch 1: `receiveBuffer` and `transmitBuffer` are `CircularBuffer<UInt32>`,
which is not thread-safe, and both threads touch both. `PeripheralDataRead`
locks `receiveBuffer` and nothing on the producing side takes that lock;
`transmitBuffer` is never locked. Counters over eight virtual seconds:
30958 DR0 writes, 30777 frames dequeued and shifted, FIFO-full branches
never taken, 181 bytes gone. One lock over both buffers; the same run then
shows 119074 writes against 118802 frames.

Patch 3: `ProcessReceive`'s `Address` state finds the FIFO empty when the
clocking thread ticks between the CPU's instruction write and its address
write. The state holds and the next tick retries, which is the ordinary
case, but the branch logs it at Error level. It now logs at Noisy, as the
Instruction branch does for the same condition.

**Environment.**

- Renode 1.17.0+20260907gitf1dd1b4af on .NET 9.0.20, Linux x86-64. Your
  AGENTS.md pins 1.16.1; `emulation/Peripherals.csproj` was retargeted from
  netstandard2.1 to net9.0 to build against this Renode's `Infrastructure`,
  and `cores/load_peripherals.py` pointed at the resulting directory. No
  peripheral source was changed for that.
- Renode_RP2040 at 205a5e4b, `cores/initialize_peripherals.resc` plus a
  Pico platform description without the LED binding.
- Guest: DiscoBSD 2.7 for RP2040, UART0 console.

**Patches.** `0001-xip-ssi-serialize-fifos.patch`,
`0002-xip-ssi-derive-busy-from-transfer-state.patch`,
`0003-xip-ssi-address-wait.patch`, attached; the same files are at
`tools/renode/patches/` in the repository named above. Each applies with
`git am` on 205a5e4b in order. The subjects carry no `[..]` tag because the
meaning of yours is not written down; retag as you see fit.

**Open after the series.** The transfer still runs from a managed thread,
so the FIFOs still cross threads and the lock is what keeps them honest.
Driving the transfer from the bus access would remove the cross-thread
FIFO entirely, and is a larger change than this series makes.

---

## Issue 2: W25QXX diverges from the part on status, erase, program and identity

> An LLM assisted in the investigation and the writing of this report and
> its patch. Everything in it was run and read back by a person; what you
> do with it is your call.

**Finding.** Six behaviors of `emulation/externals/w25q16.cs` differ from
what a W25Q16JV on a Raspberry Pi Pico does. None of them stops a boot.
Five make the model more forgiving than the part, so a guest driver bug the
silicon would punish passes under emulation; one is a one-byte overrun into
`MappedMemory`.

**What the part does.** Measured with a SRAM-resident probe
(`tools/flash-semantics` in the repository named below) that issues the raw
commands through the Pico SDK's `flash_do_cmd` on one scratch sector and
reads the sector back:

```text
JEDEC_ID                  ef 40 15
SFDP                      53 46 44 50 05 01 00 ff      ; "SFDP", v1.5
STATUS1 idle              00
STATUS1 after 0x06 WREN   02                           ; WEL
STATUS1 after 0x04 WRDI   00
sector erase 0x20         first poll 03 (WIP|WEL), 232 polls, 29072 us
                          STATUS1 after: 00; 4096 of 4096 bytes 0xff
page program 0x02         first poll 03, 3 polls, 375 us
                          STATUS1 after: 00; 256 of 256 bytes 0x0f
program 0xf0 over 0x0f    256 of 256 bytes 0x00       ; bits only clear
without an erase
32 bytes from page+0xf0   0xf0..0xff then 0x00..0x0f of the same page;
                          the next page stays 0xff  ; wraps in the page
```

**Where the model differs, and the fix for each.**

1. *Status reads are never answered.* The constructor builds
   `statusRegister` with WIP and WE, and nothing reads it. `RecognizeOperation`
   decodes 0x05, 0x35 and 0x15 to `OperationType.ReadRegister`, but
   `HandleCommand` switches on Read, ReadFast, ReadID, SFDP and Program only,
   so a status read hits the default branch, logs
   `Unhandled operation while processing byte: 0x7` -- the ordinal of
   ReadRegister, not a byte on the wire -- and returns zero. WIP reads clear
   forever, so every `flash_wait_ready` returns on its first poll; a driver
   that omitted the wait would behave identically here and fail on the
   board. One boot logs 9972 of these. The patch serves status register 1:
   WEL follows 0x06 and 0x04 and clears when a program, erase or register
   write ends, which is the sequence the board shows above.

2. *Chip erase writes zeros.* `EraseChip` calls `ZeroAll()`, while
   `EraseBytesInRange` fills with `EmptyByte` (0xff). After 0xC7 or 0x60
   the array reads the inverse of what the part reads. The patch fills with
   `EmptyByte`.

3. *Programming sets bits.* `WriteMemory` stores the raw byte. A NOR
   program only clears bits, which is why the board reads 0x00 after 0xf0
   over 0x0f. The patch stores `old & data`.

4. *Programming crosses the page.* The write address is
   `ExecutionAddress + CommandBytesHandled`, advancing without limit. The
   part wraps within its 256-byte page, as the wrap row above shows. The
   patch keeps the page base and wraps the low byte.

5. *Bounds guards are off by one.* Both read `position > Size` where
   `Size` is one past the last byte. The patch compares `>=`.

6. *JEDEC ID returns zero.* `ReadID` logs a TODO. The patch returns
   manufacturer, memory type and log2 of the size, repeating, and changes
   `memoryType` from 0x28 to 0x40: the board answers `ef 40 15`, and 0x40
   is the W25Q family code. The origin of 0x28 is not known to me
   (hypothesis: inherited from a Micron-shaped generic model).

**Measurement.** The DiscoBSD boot that reached a shell with 44567
warnings logs 34624 with the patch, the difference being the status reads,
and the console transcript is byte-identical.

**Environment.** As in issue 1: Renode 1.17.0 on .NET 9.0.20, csproj
retargeted to net9.0, Renode_RP2040 at 205a5e4b. The board is an original
Raspberry Pi Pico, RP2040 B2, whose flash answers JEDEC `ef 40 15`.

**Patch.** `0004-w25qxx-status-identity-program.patch`, attached; also at
`tools/renode/patches/` in
github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial. It applies on
top of the issue 1 series, or on 205a5e4b alone with a trivial offset. No
`[..]` subject tag, for the reason given there.

**Open after the patch.** WIP still never sets, because an erase or
program completes inside the command that issued it; a timing model would
hold it for the 29 ms and 375 us measured above. SFDP still returns zero
where the part returns a v1.5 table. Status registers 2 and 3 read as zero
and a status write is consumed without effect, because the protection and
configuration bits behind them are not modeled. Each is stated in the
source beside the code that leaves it.
