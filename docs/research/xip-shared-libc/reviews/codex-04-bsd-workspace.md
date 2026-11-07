1. **Major**

   **Claim:** “Against PIC plus relocation: FDPIC is unavailable on Thumb-1” ([stream:68](<streams/04-bsd-workspace.md:68>)).

   **Evidence checked:** NetBSD configures ordinary PIC separately at [arm.cc:3598](<BSD_Workspace/netbsd-src/external/gpl3/gcc/dist/gcc/config/arm/arm.cc:3598>) and rejects FDPIC specifically at [arm.cc:3619](<BSD_Workspace/netbsd-src/external/gpl3/gcc/dist/gcc/config/arm/arm.cc:3619>). GCC 16.2.0 successfully compiled a Cortex-M0+ global-data access using `-mcpu=cortex-m0plus -mthumb -Os -fpic -ffreestanding -Wall -Wextra -Werror -S -o -`; exit status was 0 and the assembly used `_GLOBAL_OFFSET_TABLE_` plus `shared_value(GOT)`.

   **Correction:** State that the cited GCC implementation rejects FDPIC on Thumb-1 while supporting ordinary PIC. Compare ordinary PIC’s generated instructions, GOT storage, relocations, and runtime cost before ranking it against fixed-address libc.

2. **Major**

   **Claim:** “a RELATIVE-only fixup pass is a few lines” ([stream:71](<streams/04-bsd-workspace.md:71>)).

   **Evidence checked:** NetBSD’s loop uses one `relocbase` both to locate relocation targets and adjust their values ([mdreloc.c:35](<BSD_Workspace/netbsd-src/libexec/ld.elf_so/arch/arm/mdreloc.c:35>), [mdreloc.c:51](<BSD_Workspace/netbsd-src/libexec/ld.elf_so/arch/arm/mdreloc.c:51>)). The retained linker script places text at `0x10100000` and writable sections at `0x20000000` ([libcx.ld:3](<measure/libcx.ld:3>)). Independently placed XIP text and SRAM data have different displacement rules; the cited uniform-bias loop alone cannot encode both.

   **Correction:** Qualify the example with its required conditions: one common load bias, writable relocation destinations, and a verified RELATIVE-only relocation set. Demonstrate those conditions for the proposed layout or specify segment-aware relocation.

3. **Minor**

   **Claim:** OpenBSD ARM startup uses “no jump tables, SSP or builtins before relocation” ([stream:53](<streams/04-bsd-workspace.md:53>)).

   **Evidence checked:** OpenBSD disables stack protection for non-amd64 architectures, including ARM ([Makefile:34](<BSD_Workspace/openbsd-src/lib/csu/Makefile:34>)). The Makefile restricts `-fno-builtin` to MIPS64 and PowerPC ([Makefile:41](<BSD_Workspace/openbsd-src/lib/csu/Makefile:41>)) and `-fno-jump-tables` to MIPS64 and PowerPC variants ([Makefile:50](<BSD_Workspace/openbsd-src/lib/csu/Makefile:50>)).

   **Correction:** Attribute only the stack-protector restriction to ARM. Establish the absence of ARM builtins and jump tables by inspecting the relevant compiled object instead of importing restrictions from other architectures.

4. **Minor**

   **Claim:** RetroBSD has “About 100 separate static programs” ([stream:17](<streams/04-bsd-workspace.md:17>)).

   **Evidence checked:** Parsing the explicit groups in [src/cmd/Makefile:13](<BSD_Workspace/retrobsd/src/cmd/Makefile:13>) through [line 60](<BSD_Workspace/retrobsd/src/cmd/Makefile:60>) produced `SUBDIR=88 STD=56 NSTD=3 SETUID=2 OPERATOR=1 KMEM=3 TTY=2`, totaling 155 compiled command entries. Entries do not necessarily equal distinct installed binaries.

   **Correction:** Report 155 enumerated compiled command targets and distinguish that count from installed executables, or cite an installation manifest supporting a binary count.

5. **Minor**

   **Claim:** “Paths are relative to the local BSD_Workspace checkout” ([stream:3](<streams/04-bsd-workspace.md:3>)).

   **Evidence checked:** `386bsd/kern/execve.c` is absent; the cited code resides at [386bsd/usr/src/kernel/kern/execve.c:175](<BSD_Workspace/386bsd/usr/src/kernel/kern/execve.c:175>). `retrobsd/exec_aout.h` is absent; the definitions and defective macro reside at [retrobsd/sys/include/exec_aout.h:54](<BSD_Workspace/retrobsd/sys/include/exec_aout.h:54>) and [line 78](<BSD_Workspace/retrobsd/sys/include/exec_aout.h:78>). The complete 2.11BSD magic list is at [exec.h:39](<BSD_Workspace/211bsd/sys/h/exec.h:39>), beyond the cited `35-40` range.

   **Correction:** Use complete workspace-relative paths and ranges that cover every claimed value.

The source survey’s core survives: the cited trees support the crunchgen size rationale and dispatch mechanism, symbol hiding, the unimplemented BSS-overlay proposal, FreeBSD’s static rescue configuration, NetBSD’s optional static PIE, RetroBSD’s fixed-address loading, and its vacuous flag extraction. The survey justifies investigating fixed-address libc beside multicall executables. The evidence does not yet show that this design beats ordinary PIC, nor does it establish comparative flash, SRAM, relocation, or execution costs.
