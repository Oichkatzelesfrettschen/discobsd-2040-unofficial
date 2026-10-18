# Generated syscall dispatch declaration contract

The GLOB-enabled dispatch table referenced `rdglob` and `wrglob` without visible
prototypes. A captured PICO compiler command reproduced the failure while the
default configuration compiled. The declaration collection in `systm.h` also
repeated the handler set already owned by `sys/kern/syscalls.master`.

`tools/gen_kernel_metadata.py` emits `sys/sys/syscall_dispatch.h` from the master.
The guarded private interface owns 120 unique handler declarations, the
`void (*)(void)` handler type, byte argument-count and naturally aligned pointer
arrays, `nsysent`, and `syscall_name(unsigned int)`. The master names 119 unique
handlers; the network-error fallback contributes `nonet`. Generator conditions
add fallback declarations when their corresponding table branches need them.

The header needs built-in C types only. Its argument-count declaration uses
`unsigned char`, the underlying type of the existing `uint8_t`. The generated
C files retain `<sys/stdint.h>` for their definitions and offset tables.
A native fixture reproduced Smaller C's rejection of an unnecessary `long long`
typedef when the private header included all of `<sys/stdint.h>`; removing that
dependency preserves the measured native input contract. Compiler checks verify
compatibility between the public array declaration and `uint8_t`.

`systm.h` keeps its existing prerequisite kernel types, device-switch include,
integer-type exposure and other declarations. Its syscall interface comes from
the generated header. Kernel `glob.h` delegates to the same owner; user `glob.h`
retains `int rdglob(int)` and `int wrglob(int, int)`. The generated table and
trace-name source include the narrow interface directly. `kernel_errmsg` remains
in `systm.h` because the errno master owns that independent mechanism.

The RP2040 and maintained STM32 syscall adapters retain their existing include
entry points and zero-argument calling convention. The public numeric
`include/syscall.h` remains byte-identical, including assembly consumers through
`lib/libc/arm/sys/SYS.h`. Slot count 156, syscall 23, dispatch slot 66, the public
vfork alias, sbrk spelling and zero-argument slot 81 retain their existing pins.
`INET` and `GLOB_ENABLED` retain definedness semantics: a defined zero enables
the corresponding branch. The change adds declarations rather than optional
networking or GLOB runtime qualification.

`check-kernel-metadata` owns generation freshness, ABI/capacity contracts and
`tests/kernel_metadata/dispatch_contracts.py`. The dispatch gate runs 121 cases:
standalone/repeated inclusion, integer-type include compatibility, both kernel
`glob.h` orders, user function-pointer types, GCC/Clang strict-C17 header and
source compilation, exact production profiles for PICO/PICO_UART, and numeric
assembly preprocessing. Host stub executables check every handler pointer,
argument count, handler invocation and trace name in four INET/GLOB combinations
and a defined-zero configuration. The stubs isolate table selection and calling
types; host execution leaves the real optional handler implementations unrun.

Known-bad controls remove a handler or fallback declaration, change a prototype,
supply an incompatible callback, disable the include guard, stale the generated
header and replace `nonet` with `nosys`. The guard control counts preprocessing
rather than expecting a duplicate typedef error, because C17 permits repeating
an identical typedef. `check-c17-smaller-c` includes `t19_dispatch` for the private
header and kernel `glob.h`; `t18_stdio` also accepts repeated user `glob.h` input.
The native execution harness uses its established Linux-EABI syscall adapter.

The header joins the installed-header manifest in `distrib/base/mi`.
The tiny RP2040 root retains its selected manifest and header-free toolchain
profile. Dependency capture counts tracked generated files as source inputs;
the existing metadata freshness gate binds those bytes to the master and
generator. The generated-input provider map retains its established scope.

The clean parent distribution at `b55c5124` passed in 245.6 seconds and retains
36 artifact groups. Candidate consumer rebuilds, resource comparison, full
aggregate and hosted CI remain integration gates. Language acceptance for the
three generated files and `glob.h` covers the declared header/table/name
contracts. Handler-definition migration, the broader `systm.h` collection,
syscall copying, runtime stack/heap, persistence and silicon behavior retain
independent open states.
