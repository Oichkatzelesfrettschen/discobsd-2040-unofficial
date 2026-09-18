# Reports drafted for matgla/Renode_RP2040

Two reports, written to be submitted as issues against
github.com/matgla/Renode_RP2040 and unsent until the repository owner asks
for them. Everything below was measured against commit
205a5e4b25440582008a4292074bb07f80a72328, the revision
tools/renode/fetch-renode-rp2040.sh pins, under Renode
1.17.0+20260907gitf1dd1b4af on .NET 9.0.20, booting the DiscoBSD RP2040
kernel from the project's own copy of the pico-bootrom-rp2040 image.

The SSI report carries two patches this tree already applies, under
tools/renode/patches. The flash report carries no patch: the defects are
described with the fix each one wants, because four of them touch behavior
a downstream user may be relying on and the choice belongs upstream.

sys/arch/rp2040/doc/research/emulation.md records how each finding was
measured. tools/renode/warning-classes.txt carries the log signature of
every defect that shows up as a warning, so a reader can tell which of
these a given boot is hitting.

---

## Report 1: RP2040XIPSSI hangs the boot ROM by latching BUSY

### Summary

`RP2040XIPSSI` stores the SSI BUSY flag in a register field that
`ProcessTransmit` sets before a transfer and clears after it. Any
interruption between those two assignments leaves the flag set with nothing
left to clear it, and the RP2040 boot ROM's `wait_ssi_ready` spins on it
forever. Every free-running boot of a kernel that drives flash through the
ROM's helpers eventually wedges.

### Environment

- Renode 1.17.0+20260907gitf1dd1b4af, .NET 9.0.20, Linux x86-64
- Renode_RP2040 at 205a5e4b, `emulation/Peripherals.csproj` retargeted to
  net9.0 (netstandard2.1 does not build against this Renode's
  `Infrastructure`)
- Platform: the project's `cores/initialize_peripherals.resc` plus a
  Raspberry Pi Pico description with `bootroms/rp2040/b2.elf` loaded at 0
  and a guest image at 0x10000000
- Guest: DiscoBSD 2.7 for RP2040, UART0 console

### What happens

The boot runs normally for one to four virtual seconds, then stops with no
further console output and no further log lines while the process keeps
burning about 1.4 host cores. The point at which it stops moves between
runs.

### Evidence

`machine StartGdbServer 3333`, then `arm-none-eabi-gdb` at the stall:

    pc  0x200404ae
    r3  0x18000000            ; XIP_SSI base

    0x200404aa:  push  {r0, r1, lr}
    0x200404ac:  ldr   r1, [r3, #0x28]   ; SR
    0x200404ae:  movs  r0, #4            ; TFE
    0x200404b0:  tst   r1, r0
    0x200404b2:  beq.n 0x200404ac
    0x200404b4:  movs  r0, #1            ; BUSY
    0x200404b6:  tst   r1, r0
    0x200404b8:  bne.n 0x200404ac

That is `wait_ssi_ready` from `bootrom/program_flash_generic.c`, copied into
SRAM so it runs with XIP disabled: wait for the transmit FIFO to empty, then
for BUSY to clear. The SSI registers read

    TXFLR (0x18000020)  0
    RXFLR (0x18000024)  1
    SR    (0x18000028)  0x0f      ; BUSY | TFNF | TFE | RFNE

TFE is satisfied. BUSY is set and never clears, so the loop cannot exit.

A host stack sample of the CPU thread shows where the race comes from:

    BaseCPU::CpuThreadBody
    BaseCPU::CpuThreadBodyInner
    TimeSourceBase::ReportTimeProgress
    TimeSourceBase::SynchronizeVirtualTime
    BaseClockSource::Advance
    BaseClockSource::Update

A managed thread's action runs from `BaseClockSource.Update` on whichever
CPU thread reports time progress, so on a two-core RP2040 both cores can
enter `TransferClock` at once. `RecalculateFrequencies` (reached from the
BAUDR write callback and from `OnSystemClockChange`) and the SSIENR write
callback stop the clocking thread from a third path, and the SSIENR edge
clears both FIFOs while a transfer may be in flight.

### Suggested fix

Stop storing the flag. DW_apb_ssi reports BUSY while a serial transfer is in
progress, which the model can derive: a count of transfers in flight, raised
and dropped in one `try`/`finally`, plus the depth of the transmit queue.
A derived flag cannot latch, whatever interrupts the transfer.

Two smaller changes go with it: take one lock across the whole transfer
state machine, so two cores cannot interleave the bytes of one command, and
stop the clocking thread before an SSIENR edge clears the queues rather than
after, so no half-sent command straddles the reset.

The patch is
tools/renode/patches/0002-xip-ssi-derive-busy-from-transfer-state.patch in
github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial.

### Measurement

Free-running boots, `emulation RunFor "30"`, no terminal tester attached,
counting how many reach the guest's getty banner:

| model | reaching userland |
| --- | --- |
| 205a5e4b with the FIFO lock below | 0 of 6 |
| plus the derived BUSY flag | 6 of 6 |

A Robot suite is the wrong instrument for this one: `Create Terminal Tester`
pauses the emulation at every wait, which serializes the two CPU threads
enough that the race rarely fires. The suite passed against the unfixed
model, and only free-running runs separate them.

### The related FIFO defect, for completeness

`receiveBuffer` and `transmitBuffer` are `CircularBuffer<UInt32>`, which is
not thread-safe, and the same two threads touch both: the CPU thread through
the DR0 register callbacks and the TXFLR, RXFLR and SR value providers, and
the clocking thread through `ProcessTransmit`, `ProcessReceive` and
`PushToReceiveFifo`. `PeripheralDataRead` takes `lock (receiveBuffer)` and
nothing on the producing side takes that lock, so it excludes nothing;
`transmitBuffer` is never locked.

Instrumented with counters over eight virtual seconds: 30958 DR0 write
callbacks against 30777 frames dequeued and shifted, with the FIFO-full
branches never taken. 181 bytes entered the transmit buffer and left no
trace. Serializing both buffers on one lock is
tools/renode/patches/0001-xip-ssi-serialize-fifos.patch. It is a workaround
for the shape of the model; driving transfers from the bus access rather
than from a managed thread would remove the cross-thread FIFO entirely.

---

## Report 2: W25QXX fidelity gaps

### Summary

Six defects in `emulation/externals/w25q16.cs`, none of which stops a boot.
Each one makes the model more permissive than the part, so a driver bug the
hardware would punish passes under emulation.

### 1. Status register reads are never served

`W25QXX` builds a `statusRegister` in its constructor with WIP at bit 0 and
WE at bit 1. Nothing ever reads it: `grep statusRegister` finds the
construction and the field declaration and no third use.

`RecognizeOperation` decodes 0x05, 0x35 and 0x15 into
`OperationType.ReadRegister` and 0x01, 0x31, 0x11 into
`OperationType.WriteRegister`, but `HandleCommand` switches only on `Read`,
`ReadFast`, `ReadID`, `ReadSerialFlashDiscoveryParameter` and `Program`. A
status read therefore falls to the default branch, logs

    Unhandled operation while processing byte: 0x7

and returns zero. The 0x7 is the decoded operation type, not a byte on the
wire; a noisy-level log confirms it, with `Decoded operation: ReadRegister`
on the line before.

Consequence: WIP reads clear forever, so every `flash_wait_ready` -- in the
boot ROM and in any guest driver -- returns on its first poll. Erases and
programs appear instantaneous. A driver that omitted the wait entirely
would behave identically here and fail on silicon.

Nearly ten thousand of these are logged during one DiscoBSD boot.

Fix: serve `statusRegister` for `ReadRegister` and accept a byte into it for
`WriteRegister`, and hold WIP set for the duration of an erase or program
if a timing model is wanted.

### 2. Chip erase writes zeros where the part writes ones

`EraseBytesInRange` fills with `EmptyByte`, correctly. `EraseChip` calls
`underlyingMemory.ZeroAll()` instead, so a 0xC7 or 0x60 leaves the array
reading 0x00 where an erased NOR flash reads 0xFF. A guest that erases the
chip and then checks for blank media sees the opposite of what the part
reports.

Fix: fill with `EmptyByte`, the way `EraseBytesInRange` does.

### 3. Programming does not require an erase first

`WriteMemory` calls `underlyingMemory.WriteByte(position, data)`. NOR flash
programming can only clear bits, so silicon delivers `old & data` and cannot
raise a bit that is already zero. The model will happily turn 0x00 into
0xFF. A driver that programs a page twice without erasing between, or that
miscomputes an erase boundary, is corrected by the model rather than caught
by it.

Fix: write `old & data`. It costs one read and makes the failure visible as
the corruption it is on the part.

### 4. Programming runs past the end of a page

`ReadFromMemory` and `TryVerifyWriteToMemory` both compute
`ExecutionAddress + CommandBytesHandled` and increment linearly. A W25Q page
program wraps at the 256-byte page boundary and overwrites the start of the
same page; the model writes on into the next page. A driver that sends more
than a page in one command gets a different result here than on the part.

Fix: wrap the write address within its 256-byte page.

### 5. The bounds guards are off by one

Both guards read

    if (position > underlyingMemory.Size)

where `position == Size` is already one past the last byte. The last
comparison that should be rejected is accepted, and the read or write
reaches `MappedMemory` out of range.

Fix: `>=`.

### 6. JEDEC ID and SFDP return zero

`HandleCommand` answers `ReadID` and
`ReadSerialFlashDiscoveryParameter` with

    this.Log(LogLevel.Info, "TODO: implement READ ID");

and returns zero. A guest that identifies its flash through 0x9F or reads
SFDP sees an all-zero part. The RP2040 boot ROM does not depend on either,
so a boot survives, but any driver that sizes or configures flash from the
ID gets nothing to work with.

Fix: answer 0x9F from the constructed capacity and a configurable
manufacturer and device ID, and either implement a minimal SFDP table or
reject the command so the guest can fall back.

### What none of these are

None of the six stalls a boot. Numbers 1, 2, 3, 4 and 6 make the model more
forgiving than the part, and number 5 is a one-byte overrun into
`MappedMemory` rather than a guest-visible difference. They are listed
together because a downstream user who trusts this model to validate a
flash driver should know which checks it is not performing.
