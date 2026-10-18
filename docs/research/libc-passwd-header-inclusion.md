# Password public-header inclusion and ABI

A C translation unit that included `pwd.h` twice redefined `struct passwd` and
failed compilation. The public interface needs one authoritative declaration
set because callers may reach the header directly and through peer headers.
The include guard fixes repeated inclusion while preserving every field,
function signature, database key and public include name.

The unit owns `include/pwd.h` declarations and their inclusion/layout contract.
The interface uses built-in C types and requires no prerequisite header. The
header defines one seven-field record, two compatibility keys and seven typed
function declarations; the header defines no storage. No assembly consumer is
part of the captured caller set. Lookup results borrow libc's shared entry and
string buffers; `setpwfile` retains its caller's filename pointer. The libc
lookup implementations keep their independent parsing and error contracts.

| ARM ABI surface | Preserved value |
| --- | --- |
| Structure size / alignment | 28 / 4 bytes |
| Field offsets: name, password, UID, GID, gecos, directory, shell | 0, 4, 8, 12, 16, 20, 24 |
| String fields | `char *` |
| UID and GID fields | `int`, four bytes |
| Lookup results | `struct passwd *` |
| UID lookup argument | `int` |
| Name lookup argument | `const char *` |
| Iteration and keep-open results | existing `int` results |
| Database key constants | existing `'0'` and `'1'` |

`libc-passwd-header-consumers.json` records the clean parent at `d94785b8`:
95 compiled C consumers and ten affected shipped artifact groups. The compiler
dependency capture includes transitive and generated consumers; grep alone
would omit those edges. Clean parent/candidate distributions rebuild all
consumers, including maintained programs outside the shipped manifest.

`check-passwd-header-contracts` runs five required compiler profiles and two
optional host ILP32 profiles. Every executed profile accepts four include forms
and rejects three calibrated mutations: removed guard, changed UID signedness
and an incompatible lookup callback. Four additional availability-policy
controls distinguish accepted compiler input, an unavailable optional mode,
a rejected mandatory mode and a rejected source error. The unavailable-mode
control is a diagnostic fixture; actual compiler probes own live availability.
The local host executes all seven profiles, yielding 49 compiler cases.

The gate checks exact member types through member addresses so qualification
and signedness survive the type oracle. Function-pointer checks preserve the
existing BSD declarations, including `int` UID and iteration results. The
standard offsetof macro, sizeof and alignment checks describe the compiler's
layout; the ARM profiles independently pin the required 32-bit model.

The extended `t18_stdio` fixture accepts the expanded password header twice
through Smaller C and checks size, every offset, four-byte nested alignment,
integer-field widths and both key constants. All 18 Smaller C programs and the
damaged/restored stdio archive control pass. The native suite starts after host
preprocessing and uses a Linux EABI execution shim; on-device preprocessing,
physical-board execution and full native C17 development remain separate work.

The header adds an include guard and explanatory comments. The unit introduces
no runtime allocation, syscall, field or executable interface. Paired clean
static-resource acceptance remains the deciding gate before integration;
runtime stack, heap and storage traffic retain their independent states.
The source row records the header's C17 language acceptance and preserved ABI,
while password lookup behavior and complete DPUCP interface closure remain open.
