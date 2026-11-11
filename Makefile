# Copyright (c) 1986 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
# This makefile is designed to be run as:
#	make
#
# The `make' will compile everything, including a kernel, utilities
# and a root filesystem image.

TOPSRC!=	pwd

include ${TOPSRC}/share/mk/architecture.mk

DESTDIR?=	${TOPSRC}/distrib/obj/destdir.${MACHINE}
RELEASEDIR?=	${TOPSRC}/distrib/obj/releasedir.${MACHINE}

# Filesystem and swap sizes, in kbytes. A machine whose disk is smaller
# than an SD card overrides these from distrib/${MACHINE}/Makefile.inc,
# and U_KBYTES=0 omits the /home partition.
FS_KBYTES?=	204800
U_KBYTES?=	204800
SWAP_KBYTES?=	2048
# The machine-independent manifest; a machine with a small disk names a
# shorter one.
MI_MANIFEST?=	distrib/base/mi
# Inodes in the root filesystem; 0 leaves fsutil's one per 16 kbytes.
FS_INODES?=	0
# Machine fragments define image file targets before the root build rules.
# Keep a targetless invocation bound to the complete maintained build.
.MAIN:		all
-include	distrib/${MACHINE}/Makefile.inc

FS_INODES_ARG!=	if [ ${FS_INODES} -gt 0 ] ; then \
			echo "--inodes=${FS_INODES}" ; \
		fi

PARTITIONS!=	if [ ${U_KBYTES} -gt 0 ] ; then \
			echo "fs=${FS_KBYTES}k:swap=${SWAP_KBYTES}k:fs=${U_KBYTES}k" ; \
		elif [ ${SWAP_KBYTES} -gt 0 ] ; then \
			echo "fs=${FS_KBYTES}k:swap=${SWAP_KBYTES}k" ; \
		else \
			echo "fs=${FS_KBYTES}k" ; \
		fi

# SD card filesystem image for ${MACHINE}. A machine whose images depend on a
# board names the board's path from distrib/${MACHINE}/Makefile.inc.
FSIMG?=		${TOPSRC}/distrib/${MACHINE}/sdcard.img

# Set this to the device name for your SD card.  With this
# enabled you can use "make installfs" to copy the sdcard.img
# to the SD card.

#SDCARD          = /dev/sdb

#
# C library options: passed to libc makefile.
# See lib/libc/Makefile for explanation.
#
DEFS		=

TOOLDIR?=	${TOPSRC}/tools
TOOLBINDIR?=	${TOOLDIR}/bin/${MACHINE}
FSUTIL=		${TOOLBINDIR}/fsutil

# The caller selects the interpreter for every verifier and harness. CI and
# documented commands set PYTHON explicitly; sub-makes inherit that identity.
PYTHON?=
.export PYTHON

-include Makefile.user

#
# usr.bin builds before sbin: sbin/textbox's TOOLS loop, like
# sbin/box's and sbin/sysbox's, links each tool's objects out of the
# directory that already built them (see sbin/textbox/Makefile), and
# textbox's tools live under usr.bin.
#
SUBDIR=		share lib bin usr.bin sbin libexec usr.sbin games benchmarks

all:		build

# Optional host research tier; NumPy is not a firmware-build dependency.
.PHONY: check-graph-layout
check-graph-layout: check-python
		PYTHONDONTWRITEBYTECODE=1 ${PYTHON} tools/graph_layout_test.py
		PYTHONDONTWRITEBYTECODE=1 ${PYTHON} tools/graph_layout_trace_test.py
		PYTHONDONTWRITEBYTECODE=1 ${PYTHON} tools/performance_lab_test.py

check-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the interpreter for this target" >&2; \
			exit 2; \
		fi

# These caller-facing targets retain progress and terminate the complete
# recursive-make process group when either output or total runtime exceeds its
# bound. Internal build targets stay unwrapped so recursive make remains direct.
logged-build: check-python
		${PYTHON} tools/run_logged_gate.py --label rp2040-build-local \
		    --evidence-root .build-logs \
		    --execution-lock .build-logs/rp2040-products.lock \
		    --idle-timeout-seconds 600 --timeout-seconds 3600 \
		    -- ${MAKE} MACHINE=rp2040 build

logged-distribution: check-python
		${PYTHON} tools/run_logged_gate.py --label rp2040-distribution-local \
		    --evidence-root .build-logs \
		    --execution-lock .build-logs/rp2040-products.lock \
		    --idle-timeout-seconds 600 --timeout-seconds 3600 \
		    -- ${MAKE} MACHINE=rp2040 distribution

logged-flash: check-python
		${PYTHON} tools/run_logged_gate.py --label rp2040-flash-local \
		    --evidence-root .build-logs \
		    --execution-lock .build-logs/rp2040-products.lock \
		    --idle-timeout-seconds 600 --timeout-seconds 3600 \
		    -- ${MAKE} MACHINE=rp2040 flash

logged-kernel: check-python
		${PYTHON} tools/run_logged_gate.py --label rp2040-kernel-local \
		    --evidence-root .build-logs \
		    --execution-lock .build-logs/rp2040-products.lock \
		    --idle-timeout-seconds 600 --timeout-seconds 3600 \
		    -- ${MAKE} MACHINE=rp2040 kernel

logged-check: check-python
		${PYTHON} tools/run_logged_build_check.py --label rp2040-check-local \
		    --evidence-root .build-logs \
		    --execution-lock .build-logs/rp2040-products.lock \
		    --idle-timeout-seconds 600 --timeout-seconds 3600 \
		    -- ${MAKE} MACHINE=rp2040

.PHONY: logged-build logged-distribution logged-flash logged-kernel logged-check

# The SUS discovery tools reject unavailable comparison trees and keep
# source, installed-name, linkage and behavior evidence in separate states.
# Fixtures calibrate declaration-only and implemented API candidates; ledger
# mutations calibrate selected-image and Bourne-shell ownership checks.
check-sus-discovery: check-python
		${PYTHON} docs/research/bsd44-audit/posix_inventory_test.py
		${PYTHON} tools/check_sus_utility_ledger.py --root ${TOPSRC} --self-test

# The source-bound corpus keeps Issue 7 and Issue 8 entity membership separate
# from DiscoBSD applicability and evidence. Its negative controls reject count,
# hash, ordering, mapping and selected-image ownership drift.
check-sus-requirement-corpus: check-python
		${PYTHON} tools/check_sus_requirement_corpus.py --root ${TOPSRC} --self-test

# DPUCP-2024 accounts for every Issue 8 entity without claiming POSIX or UNIX
# conformance. The paired test mutates denominator, state, option, witness and
# claim fields; the verifier then checks the committed 1,518-row ledger.
check-dpucp-profile: check-python
		${PYTHON} tools/check_dpucp_profile_test.py
		${PYTHON} tools/check_dpucp_profile.py --root ${TOPSRC} --self-test

# The retained board record binds the approved UF2 pair, the single flash
# execution, bounded terminal receipt, seven release surfaces and thirteen
# direct observations without promoting any complete POSIX requirement.
check-dpucp-board-acceptance: check-python
		${PYTHON} tools/check_dpucp_board_acceptance_test.py
		${PYTHON} tools/check_dpucp_board_acceptance.py --root ${TOPSRC}

# The donor ledger keeps selected BSD mechanisms advisory until license,
# dependency, C17, ABI and footprint gates agree. Mutation controls reject
# provenance, requirement, dependency, size-evidence and decision drift.
check-dpucp-donor-ledger: check-python
		${PYTHON} tools/measurement_source_identity_test.py
		${PYTHON} tools/check_dpucp_donor_ledger_test.py
		${PYTHON} tools/check_dpucp_donor_ledger.py --root ${TOPSRC}

build:		check-python .WAIT symlinks tools
		$(MAKE) kernel
		$(MAKE) -C etc DESTDIR=${DESTDIR} distrib-dirs
		$(MAKE) -C include includes
		for dir in ${SUBDIR} ${LEGACY_SUBDIRS}; do \
			${MAKE} -C $$dir || exit 1; done
		for dir in ${SUBDIR} ${LEGACY_SUBDIRS}; do \
			${MAKE} -C $$dir DESTDIR=${DESTDIR} install || exit 1; done

distribution:	build
		$(MAKE) fs
.if ${MACHINE} == "rp2040"
		${PYTHON} tools/check_rp2040_account_image.py ${FSUTIL} ${FSIMG}
.endif

# check-uf2-validator calibrates the UF2 structure checker on synthetic
# images; check-uf2-images applies it to each kernel and root pair that
# discobsd-flash loads, so it follows `kernel` and `flash`. Every board
# kernel in MACHINE_KERNEL_CONFIGS pairs with ${FLASHUF2}: the root occupies
# the same flash region on every board and every kernel boots every profile's
# root, while BOARD (distrib/rp2040/Makefile.inc) picks the profile an unset
# PROFILE builds and the file name: distrib/rp2040/flash.uf2 for PICO,
# flash-${BOARD}.uf2 otherwise.
check-uf2-validator: check-python
		${PYTHON} tools/check_rp2040_uf2.py --self-test

check-uf2-images: check-python
		@set -e; for configuration in ${MACHINE_KERNEL_CONFIGS}; do \
			${PYTHON} tools/check_rp2040_uf2.py \
			    sys/arch/rp2040/compile/$$configuration/unix.uf2 \
			    ${FLASHUF2}; \
		done

.PHONY: check-uf2-validator check-uf2-images

# The kernel flash image must leave KERNEL_HEADROOM_MARGIN bytes (default in
# tools/check_kernel_headroom.py) of the FLASH region of RP2040.ld free in
# every board kernel. check-kernel-headroom-validator calibrates the checker
# on synthetic images; check-kernel-headroom applies it to each built kernel
# and then requires a margin above the region length to fail with exit 1, so
# a checker that accepts every kernel is reported.
check-kernel-headroom-validator: check-python
		${PYTHON} tools/check_kernel_headroom.py --self-test

check-kernel-headroom: check-python
.if ${MACHINE} == "rp2040"
		@set -e; elfs=; for configuration in ${MACHINE_KERNEL_CONFIGS}; do \
			elfs="$$elfs sys/arch/rp2040/compile/$$configuration/unix.elf"; \
		done; \
		${PYTHON} tools/check_kernel_headroom.py \
		    ${KERNEL_HEADROOM_MARGIN:D--margin ${KERNEL_HEADROOM_MARGIN}} $$elfs; \
		rc=0; \
		${PYTHON} tools/check_kernel_headroom.py --margin 1000000 \
		    sys/arch/rp2040/compile/PICO/unix.elf >/dev/null 2>&1 || rc=$$?; \
		if [ "$$rc" -ne 1 ]; then \
			echo "check-kernel-headroom: control margin gave exit $$rc, expected 1" >&2; \
			exit 1; \
		fi; \
		echo "PASS kernel-headroom control: a margin above the region length is rejected"
.else
		@echo "check-kernel-headroom requires MACHINE=rp2040" >&2
		@exit 2
.endif

.PHONY: check-kernel-headroom-validator check-kernel-headroom

check-account-image:	check-python .WAIT ${FSIMG}
.if ${MACHINE} == "rp2040"
		${PYTHON} tools/check_rp2040_account_image.py ${FSUTIL} ${FSIMG}
.else
		@echo "check-account-image requires MACHINE=rp2040" >&2
		@exit 2
.endif

tools:
		${MAKE} -C tools MACHINE=${MACHINE} install

kernel:		tools
		${MAKE} -C sys/arch/${MACHINE}/compile all

check-divider:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-divider requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-divider

check-swapram:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-swapram requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-swapram

check-cache-footprint:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-cache-footprint requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-cache-footprint

check-exec-spool:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-exec-spool requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-exec-spool

check-ufs-prototypes:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-ufs-prototypes requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C sys/arch/rp2040/compile check-ufs-prototypes

# The K&R function-interface ledger for sys/kern and sys/sys: kr_scan.py over
# every conditional branch, unioned with the cross compiler's
# -Wold-style-definition and -Wstrict-prototypes diagnostics for PICO and
# PICO_UART. tools/c17/kr_scan_test.py calibrates the scan first.
check-c17-kernel-inventory:	check-python
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-c17-kernel-inventory requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${PYTHON} tools/c17/kr_scan_test.py
		${PYTHON} tools/c17/c17_inventory.py --make "${MAKE}" \
		    --build sys/arch/rp2040/compile/PICO \
		    --build sys/arch/rp2040/compile/PICO_UART \
		    --ledger tools/c17/kernel-ledger.txt sys/kern/*.c sys/sys/*.h

# Every Git-tracked C and header path has exactly one denominator row and one
# deterministic language, ownership, extension and hazard classification.
# Historical and archive exclusions carry exact hashes; unreviewed rows remain
# visible and make --closure fail rather than becoming implicit passes.
# The opt-in capture wrapper exercises actual target compiler, linker,
# archive and object transformations with calibrated rejected controls.
check-c17-tool-capture:	check-python
		${PYTHON} tools/c17/record_tool_test.py
		${PYTHON} tools/c17/shipped_closure_test.py
		${PYTHON} tools/c17/shipped_resources_test.py

check-c17-source-denominator:	check-python
		${PYTHON} tools/c17/source_denominator_test.py
		${PYTHON} tools/c17/source_denominator.py

# The portable mechanism and freestanding kernel use their real production
# commands.  The libc case-folding gate compiles with strict C17 on host and
# Cortex-M0+; kernel builds compile and link every PICO and PICO_UART source
# with the generated GNU17 flags.  Smaller C remains a separate surface.
check-c17-compile-profiles:	check-python .WAIT symlinks
		${MAKE} -C tests/libc_casefold check
		${MAKE} -C tests/libc_casefold check-cross
		${MAKE} kernel

check-passwd-header-contracts: check-python .WAIT symlinks
		@mkdir -p .build-logs
		${PYTHON} tools/c17/passwd_header_contracts.py --root ${TOPSRC:Q} \
		    --host-cc ${HOST_CC:Ucc:Q} \
		    --output ${TOPSRC:Q}/.build-logs/passwd-header-contracts.json

check-c17-smaller-c:	check-python .WAIT symlinks
		${MAKE} -C usr.bin/smlrc test

# The capacity registry binds each smaller-table and USIZE decision to normal,
# exhaustion, deliberate-fault and recovery cases without treating an open
# hardware receipt as a pass.
check-capacity-corpus:	check-python
		${PYTHON} tools/check_capacity_corpus.py --self-test

# The RP2040 user-window bound has one source, USER_DATA_SIZE in machparam.h.
# The production USER_TOP and USER_WINDOW_MAX macros, the RP2040.ld regions,
# the MPU user regions and the host fixtures that model the window must state
# the same bound; twelve mutations calibrate the checker.
check-user-window-contract:	check-python
		${PYTHON} tools/check_user_window_contract.py --self-test

# tools/verify_rp2040_swapram_link.sh judges every linked kernel; its verdicts
# are calibrated here on synthetic nm and objdump listings, two that pass and
# thirteen mutations that each fail with the diagnostic naming its defect.
check-swapram-link-verifier:
		sh tools/verify_rp2040_swapram_link_test.sh

# Literal formats passed to the kernel's printf(), uprintf(), tprintf() and
# log() in the PICO and PICO_UART sources use only conversions where prf() and
# GCC's format(printf) checking agree; the fixture calibrates the checker.
check-kernel-printf-formats:	check-python
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-kernel-printf-formats requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${PYTHON} tools/c17/kernel_format_check_test.py
		${PYTHON} tools/c17/kernel_format_check.py --make "${MAKE}" \
		    --build sys/arch/rp2040/compile/PICO \
		    --build sys/arch/rp2040/compile/PICO_UART

check-aout:
		${MAKE} -C tests/aout_header check

# The host filesystem library every root image is built with, exercised
# through the allocation patterns that break one and checked with the
# tree's own consistency checker after each round.
check-fs-stress:
		${MAKE} -C tests/fs_stress check

# sbin/fsck on images whose highest-numbered inode is in use. The inode
# list numbers inodes 1 through imax inclusive, and the gate builds fsck for
# the host against the tree's headers at ILP32, checks images made by the
# fsutil library in preen and repair mode, and requires every mutant
# tests/fsck_inode_range/Makefile registers, each an earlier or faulted form of
# fsck, fsutil, df or mkfs, to fail the scenarios named for it. A host without ILP32 execution
# records a skip receipt; REQUIRE_ILP32=yes and the detached aggregate
# check-ilp32-execution make it a failure.
check-fsck-inode-range:
		${MAKE} -C tests/fsck_inode_range check

# Kernel sources compiled from the tree and linked against a host harness,
# so the gate measures the code the board runs.
check-kernel:
		${MAKE} -C tests/kernel check

# The ARM libc wrapper is a cross-target contract. Keep it outside the host
# kernel aggregate so the required ILP32 execution job needs only its declared
# host compiler and runtime, while check-cross retains the exact disassembly.
check-ptrace-libc-abi:
		${MAKE} -C tests/kernel check-ptrace-libc-abi

# One local authority emits the syscall ABI, dispatch metadata, trace names,
# and errno text. The check regenerates into a private temporary directory
# before comparing every shipped output byte for byte.
regen-kernel-metadata:
		${PYTHON} tools/gen_kernel_metadata.py --root ${TOPSRC}

check-kernel-metadata:
		${PYTHON} tools/gen_kernel_metadata.py --root ${TOPSRC} --self-test
		${PYTHON} tools/gen_kernel_metadata.py --root ${TOPSRC} --check
		${PYTHON} tests/kernel_metadata/check.py --root ${TOPSRC}
		${PYTHON} tests/kernel_metadata/dispatch_contracts.py --root ${TOPSRC:Q} \
		    --host-cc ${HOST_CC:Ucc:Q} \
		    --output ${TOPSRC:Q}/.build-logs/syscall-dispatch-contracts.json
		${PYTHON} tests/kernel_metadata/errmsg_contracts.py --root ${TOPSRC:Q} \
		    --host-cc ${HOST_CC:Ucc:Q}

check-root-noatime:
		${PYTHON} tests/root_noatime/check.py --root ${TOPSRC}

check-libc-environment:
		${MAKE} -C tests/libc_environment check

# The libc callers that read a value through sysctl(3) into a fixed buffer.
# sysctl(3) reports the length the value needs rather than the length it
# copied, so the gate holds uname() and gethostname() to bounds taken from
# their own buffers. It compiles the tree's userland headers, whose size_t is
# u_int, so it runs at ILP32 and under the sanitizers beside it.
check-libc-sysctl:
		${MAKE} -C tests/libc_sysctl check

# sbin/umount's exit status over several operands, and the type list it asks
# once per operand. The gate links the program's own source against a mount
# table it owns, so nothing it decides needs a mounted filesystem. It reaches
# the tree's userland headers for struct statfs, whose size_t is u_int, so it
# runs at ILP32.
check-umount-contracts:
		${MAKE} -C tests/umount_contracts check

# usr.bin/touch against a real filesystem: what it creates, which of the two
# times it moves, and what it exits. The arithmetic behind -t and -d is
# measured separately by check-libc-mktime, against the tree's own mktime.
check-touch-contracts:
		${MAKE} -C tests/touch_contracts check

check-touch-contracts-cross:
		${MAKE} -C tests/touch_contracts check-cross

check-printf-contracts:
		${MAKE} -C tests/printf_contracts check

check-printf-contracts-ilp32:
		${MAKE} -C tests/printf_contracts check-ilp32

check-printf-contracts-cross:
		${MAKE} -C tests/printf_contracts check-cross

check-recovery-utility-contracts: check-python
		${MAKE} -C tests/recovery_utility_contracts check

check-recovery-utility-contracts-cross:
		${MAKE} -C tests/recovery_utility_contracts check-cross

check-timeout-contracts: check-python
		${MAKE} -C tests/timeout_contracts check

check-timeout-contracts-cross:
		${MAKE} -C tests/timeout_contracts check-cross

check-timeout-target-link:
		${MAKE} -C tests/timeout_contracts check-target-link

check-libc-tempfiles:
		${MAKE} -C tests/libc_tempfiles check

check-libc-ctime:
		${MAKE} -C tests/libc_contracts check-ctime

# The zone file reader, which is the one part of libc that decodes bytes a
# caller names through TZ. Every count in the file bounds a walk over a
# fixed-size array, and the gate writes malformed files to prove each bound.
check-libc-zone:
		${MAKE} -C tests/libc_contracts check-zone

# mktime and timegm, the inverse of localtime and gmtime. The gate decides
# them by the round trip rather than by a table, and runs it through a zone
# file it writes, so the offset search is exercised and not only the calendar
# arithmetic.
check-libc-mktime:
		${MAKE} -C tests/libc_contracts check-mktime

check-libc-ctime-cross:
		${MAKE} -C tests/libc_contracts check-ctime-cross

check-libc-runtime-limits:
		${MAKE} -C tests/libc_contracts check-runtime-limits

check-libc-runtime-limits-cross:
		${MAKE} -C tests/libc_contracts check-runtime-limits-cross

check-libc-difftime:
		${MAKE} -C tests/libc_contracts check-difftime

check-libc-difftime-cross:
		${MAKE} -C tests/libc_contracts check-difftime-cross

check-libc-contracts:
		${MAKE} -C tests/libc_contracts check

check-libc-assert: check-python
		HOSTCC=${HOSTCC:Ucc:Q} ${PYTHON} tests/libc_contracts/assert_check.py
		HOSTCC=${HOSTCC:Ucc:Q}' -DDISCOBSD_ASSERT_TEST=1' \
		    ${PYTHON} tests/libc_contracts/assert_check.py

check-libc-host-contracts:
		${MAKE} -C tests/libc_contracts check-host

check-libc-alarm-cross:
		${MAKE} -C tests/libc_contracts check-alarm-contract-cross

check-libc-waitpid-cross:
		${MAKE} -C tests/libc_contracts check-waitpid-contract-cross

check-libc-array-memory-line:
		${MAKE} -C tests/libc_contracts check-array-memory-line

check-libc-array-memory-line-cross:
		${MAKE} -C tests/libc_contracts check-array-memory-line-cross

# gets(3) is gone from <stdio.h> and from both libc builds, so a caller fails
# to compile rather than storing an entire line into a fixed buffer. The cross
# form reads the built lib/libc.a for the symbol as well.
check-stdio-bounds-cross:
		${MAKE} -C tests/libc_contracts check-stdio-bounds-cross

check-libc-aout-contracts:	${BOARDLIBC_WORK}/libc.a
		${MAKE} -C tests/libc_contracts check-aout

check-libc-malloc:
		${MAKE} -C tests/libc_contracts check-malloc

check-libc-qsort:
		${MAKE} -C tests/libc_contracts check-qsort

check-libc-random:
		${MAKE} -C tests/libc_contracts check-random

check-libc-random-cross:
		${MAKE} -C tests/libc_contracts check-random-cross

check-libc-strtox:
		${MAKE} -C tests/libc_contracts check-strtox

check-libc-printf:
		${MAKE} -C tests/libc_contracts check-printf

# The formatter with doprnt_float linked: %a and the decimal conversions.
check-libc-printf-float:
		${MAKE} -C tests/libc_contracts check-printf-float

# The %g conversion of the tree formatter against the host C library, with the
# mutants of the float member that each restore one defect.
check-libc-printf-g:
		${MAKE} -C tests/libc_contracts check-printf-g

# HUGE_VAL as an infinity and the overflow returns of the tree libm, ldexp,
# atof and strtod, with the mutants that each restore one defect.
check-libc-huge-val:
		${MAKE} -C tests/libc_contracts check-huge-val

# The formatted input scanner at host width, at ILP32 and under the address
# sanitizer. The sanitizer tier is the one that sees a scratch overrun
# inside the scanner's own frame. check-scanf-llong runs the 64-bit integer
# member against the host C library's conversions, and the scanner without
# that member.
check-libc-scanf:
		${MAKE} -C tests/libc_contracts check-scanf check-scanf-llong

# The r+ read/write mode switch in _filbuf and _flsbuf, over a host file,
# at host width and ILP32.
check-libc-rwmode:
		${MAKE} -C tests/libc_contracts check-rwmode

# The C17 stdio surface outside the read and write path: the mode strings
# fopen accepts and the open(2) flags they produce, fflush over every stream,
# rewind, the position pair and setvbuf. The ILP32 run links the tree's own
# findiop.c, whose FILE layout assertion holds only at four-byte pointers.
# Unbuffered input also checks descriptor-independent storage, allocation
# failure, a shared-byte mutation and the available ILP32 sanitizers.
check-libc-ansi:
		${MAKE} -C tests/libc_contracts check-ansi

check-analysis:
		${MAKE} -C tools/analysis check

check-libc-syslog:
		${MAKE} -C tests/libc_contracts check-syslog

check-libc-vis:
		${MAKE} -C tests/libc_contracts check-vis

check-sysctl-status: check-python
		${MAKE} -C tests/sysctl_status check

check-libc-output: check-python
		${MAKE} -C tests/libc_output check

check-board-libc-output: check-python ${TOPSRC}/distrib/obj/boardlibc.${MACHINE}/libc.a
		${PYTHON} tests/libc_output/check_board_archive.py ${TOPSRC} ${BOARDLIBC_GCCPREFIX}-gcc

check-libc-output-cross: check-python
		${MAKE} -C tests/libc_output check-cross

check-libc-casefold: check-python
		${MAKE} -C tests/libc_casefold check

check-libc-casefold-cross: check-python
		${MAKE} -C tests/libc_casefold check-cross

check-c17-headers: check-python
		${MAKE} -C tests/c17_headers check-host

check-c17-headers-cross: check-python
		${MAKE} -C tests/c17_headers check-cross

# The tree-libm variant compiles lib/libm against include/, whose
# include/machine link the symlinks target creates.
check-libcalc: check-python .WAIT symlinks
		${MAKE} -C tests/libcalc check

check-libbn: check-python
		${MAKE} -C tests/libbn check

# usr.bin/dc and usr.bin/bc built from the tree sources against lib/libbn and
# run on the host at native width, at -m32 and under the sanitizers, with the
# register-table heap ceilings and the fault-injection check.
check-dcbc: check-python
		${MAKE} -C tests/dcbc check check-peak

# Cortex-M0+ objects of the two libraries, each with its import allow-list
# and a known-bad negative control.
check-libcalc-cross: check-python .WAIT symlinks
		${MAKE} -C tests/libcalc check-cross

check-libbn-cross: check-python .WAIT symlinks
		${MAKE} -C tests/libbn check-cross

check-getwd-lifetime: check-python
		${MAKE} -C tests/dirent_contracts check-getwd-lifetime

check-getwd-lifetime-cross:
		${MAKE} -C tests/dirent_contracts check-getwd-cross

check-dirent-contracts:
		${MAKE} -C tests/dirent_contracts check-stream

check-dirent-contracts-cross:
		${MAKE} -C tests/dirent_contracts check-cross

check-scandir-contracts:
		${MAKE} -C tests/dirent_contracts check-scandir

check-scandir-contracts-cross:
		${MAKE} -C tests/dirent_contracts check-scandir-cross

check-cat-contracts:
		${MAKE} -C tests/cat_contracts check

# getty's tty mode derivation, setflags() in libexec/getty/subr.c, over the
# flag tables it reads, on the host.
check-getty-contracts:
		${MAKE} -C tests/getty_contracts check

# init's refused-fork paths, spawn() through runcom(), single(), dfork() and
# wstart() in sbin/init/init.c, and its -1 restart marker, on the host.
check-init-contracts:
		${MAKE} -C tests/init_contracts check

check-cat-contracts-cross:
		${MAKE} -C tests/cat_contracts check-cross

check-colrm-contracts:
		${MAKE} -C tests/colrm_contracts check

check-colrm-contracts-cross:
		${MAKE} -C tests/colrm_contracts check-cross

check-unifdef-contracts:
		${MAKE} -C tests/unifdef_contracts check

check-unifdef-contracts-cross:
		${MAKE} -C tests/unifdef_contracts check-cross

# bin/dd's operand arithmetic, its input position after a failed read, and
# its seek and truncation behavior. The operands wrap where long and off_t
# are four bytes, which is what the RP2040 compiles to, so the gate builds
# the program at ILP32 and answers its open, read and lseek calls itself; a
# host that cannot build 32-bit says so and skips.
check-dd-contracts:
		${MAKE} -C tests/dd_contracts check

# usr.bin/find's locate database builders, bigram and code. Both read one
# pathname per line, and a record holds at most MAXPATH-1 bytes, so the gate
# drives that boundary and decodes the database back through an independent
# reader.
check-find-contracts:
		${MAKE} -C tests/find_contracts check

check-find-contracts-cross:
		${MAKE} -C tests/find_contracts check-cross

# games/backgammon/subs.c reads the terminal's own interrupt and erase
# characters rather than fixed bytes: <sys/ttychars.h> defaults CERASE to
# 0177, so a quit test written as '\177' takes the erase character before
# table.c and save.c can compare it, and an erase-visual test written as
# either '\010' or '\177' alone gets the other display terminal wrong. The
# gate links the shipped subs.c and drives readc() and crterase() over both.
check-backgammon-contracts:
		${MAKE} -C tests/backgammon_contracts check

check-backgammon-contracts-cross:
		${MAKE} -C tests/backgammon_contracts check-cross

check-rmdir-contracts:
		${MAKE} -C tests/rmdir_contracts check

check-rmdir-contracts-cross:
		${MAKE} -C tests/rmdir_contracts check-cross

check-tee-contracts:
		${MAKE} -C tests/tee_contracts check

check-tee-contracts-cross:
		${MAKE} -C tests/tee_contracts check-cross

# usr.bin/time linked with sbin/utilbox's dispatcher and a wait3 shim, run
# as "time" and as "utilbox time": the exit status of a normal exit, of a
# signal death (128 plus the signal), of a missing program (127), of a
# program without execute permission (126) and of a failed wait3 (125), and
# the retry of an interrupted wait3. Mutants that drop the retry, restore
# the shifted status or break the 127 or the 126 must fail under gcc and
# clang.
check-time-contracts:
		${MAKE} -C tests/time_contracts check

check-time-contracts-cross:
		${MAKE} -C tests/time_contracts check-cross

# lib/libtcl's own source under the address and undefined sanitizers, at the
# byte extents and counters it once held in unsigned short: values, results,
# substituted words, expression operands, "end" indexes, regexp backtracking
# and compiled regexp programs, each just past 65535. A mutated copy of the
# library restores each narrow width and must fail its case.
check-libtcl-contracts:
		${MAKE} -C tests/libtcl_contracts check

check-du-contracts:
		${MAKE} -C tests/du_contracts check

check-du-contracts-cross:
		${MAKE} -C tests/du_contracts check-cross

check-resize-contracts:
		${MAKE} -C tests/resize_contracts check

check-resize-contracts-cross:
		${MAKE} -C tests/resize_contracts check-cross

check-libc-string-security:
		${MAKE} -C tests/libc_contracts check-string-security

check-libc-string-security-cross:
		${MAKE} -C tests/libc_contracts check-string-security-cross

check-id-aliases:
		${MAKE} -C tests/id_aliases check

check-tiny-utility-multicall:
		${MAKE} -C tests/tiny_utility_multicall check

check-portable-utilities:
		${MAKE} -C tests/portable_utilities check

check-legacy-pdp11-v7-runner:	check-python .WAIT
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "check-legacy-pdp11-v7-runner requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/pdp11-v6 BUILD_PDP11_V6=yes MACHINE=rp2040 \
		    PYTHON=${PYTHON} check-v7-runner

check-legacy-pdp11-v7-reference:	check-python .WAIT
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "check-legacy-pdp11-v7-reference requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/pdp11-v6 BUILD_PDP11_V6=yes \
		    MACHINE=rp2040 PYTHON=${PYTHON} \
		    PDP11_V7_IMAGE=${PDP11_V7_IMAGE} \
		    PDP11_EVIDENCE_DIR=${PDP11_EVIDENCE_DIR} check-v7-reference

legacy-non-arm-verify:
		@if [ x"${BUILD_LEGACY_NON_ARM}" != x"yes" ]; then \
			echo "legacy-non-arm-verify requires BUILD_LEGACY_NON_ARM=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/non-arm BUILD_LEGACY_NON_ARM=yes check

legacy-pdp11-v6-build:
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "legacy-pdp11-v6-build requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/pdp11-v6 BUILD_PDP11_V6=yes \
		    MACHINE=rp2040 all

legacy-pdp11-v6-install:
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "legacy-pdp11-v6-install requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/pdp11-v6 BUILD_PDP11_V6=yes \
		    MACHINE=rp2040 DESTDIR=${DESTDIR} install

legacy-pdp11-v6-distribution:
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "legacy-pdp11-v6-distribution requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} MACHINE=rp2040 BUILD_PDP11_V6=yes distribution

check-architecture-isolation:
		sh tools/architecture-isolation/check-architecture-isolation.sh \
		    "${TOPSRC}" "${MAKE}"

check-fgrep-capacity:
		${MAKE} -C usr.bin/fgrep test

# The CRC-32 shared by the packed executable loader and the packer, against
# zlib, with mutants of the table and the shifts that must each be rejected.
check-crc32:	check-python
		${MAKE} -C tests/crc32 check

# The packed a.out container: the loader's stream loop and the packer
# against header and stream corruption, truncation and forged lengths.
check-hsaout:
		sh tools/verify_packed_root_configs.sh
		${MAKE} -C tests/hsaout check

# The port's heatshrink decoder against the vendored decoder over every
# a.out in the distribution tree, random data, truncations and corrupted
# streams, then mutants of the decoder and its stream loop that must each
# fail the comparison.
check-hsx-decoder:	check-python
		${MAKE} -C tests/hsaout decoder

check-config-makefile:
		${MAKE} -C tools/config check

check-agent-instructions: check-python
		${PYTHON} -B tools/agent_instructions_test.py
		${PYTHON} -B tools/check_agent_instructions.py --root ${TOPSRC}

.PHONY: check-agent-instructions

check-run-logging: check-python
		${PYTHON} tools/run_logged_gate_test.py
		${PYTHON} tools/run_logged_build_check_test.py
		${PYTHON} tools/recover_logged_execution_test.py
		${PYTHON} tools/build_observability_test.py
		${PYTHON} tools/storage_counter_driver_test.py

.PHONY: check-run-logging

check-battlestar-generation: check-python
		${PYTHON} tools/check_battlestar_generation.py --root ${TOPSRC}

.PHONY: check-battlestar-generation

# Relink every shipped multicall executable through the shared map seam before
# the finite ledger compares dispatch names, manifest links, libc members and
# packed-root arithmetic. The gate opens the root image directly, so its
# dependency must build the image instead of accepting worktree residue, and
# --root-image hands the checker the selected board's ${FSIMG}: the retained
# receipt keeps the PICO path distrib/rp2040/sdcard.img as its identity. The
# maps remain ignored build evidence. Map timestamps record successful a.out
# conversion, which finishes after the linker writes the map.
# The gate measures every member each box carries against the unfiltered
# manifest, so it unsets the selection's member lists and builds the full
# composition whatever PROFILE, IMAGE_WITH and IMAGE_WITHOUT keep.
check-multicall-composition: check-python .WAIT symlinks .WAIT ${FSIMG}
		@set -eu; \
		unset GAMEBOX_TOOLS PUZZLEBOX_TOOLS; \
		map_root=${TOPSRC}/.build-logs/multicall-link-maps; \
		mkdir -p "$$map_root"; \
		box_directories=$$(${PYTHON} tools/check_multicall_composition.py \
		    --root ${TOPSRC} --list-boxes); \
		for directory in $$box_directories; do \
			program=$$(${MAKE} -C "${TOPSRC}/$$directory" MACHINE=${MACHINE} -V PROG); \
			rm -f "$$map_root/$$program.map"; \
			${MAKE} -C "${TOPSRC}/$$directory" MACHINE=${MACHINE} clean; \
			${MAKE} -C "${TOPSRC}/$$directory" MACHINE=${MACHINE} \
			    LINK_MAP="$$map_root/$$program.map" all; \
			test -s "$$map_root/$$program.map"; \
			touch "$$map_root/$$program.map"; \
		done
		rm -f "${TOPSRC}/.build-logs/multicall-link-maps/sh.map"
		${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} clean
		${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} all \
		    LINK_MAP="${TOPSRC}/.build-logs/multicall-link-maps/sh.map"
		test -s "${TOPSRC}/.build-logs/multicall-link-maps/sh.map"
		touch "${TOPSRC}/.build-logs/multicall-link-maps/sh.map"
		${PYTHON} tools/check_multicall_composition.py --root ${TOPSRC} \
		    --map-root ${TOPSRC}/.build-logs/multicall-link-maps \
		    --root-image ${FSIMG}
		${PYTHON} tools/check_multicall_composition_test.py

.PHONY: check-multicall-composition

check-comment-hygiene: check-python
		${PYTHON} tools/changed_comments_test.py
		${PYTHON} tools/check_changed_comments.py --root ${TOPSRC} --working

check-changed-comments: check-python
		@set -eu; \
		if [ -z "${CHANGED_COMMENT_BASE}" ] || \
		    [ -z "${CHANGED_COMMENT_HEAD}" ]; then \
			echo "check-changed-comments requires CHANGED_COMMENT_BASE and CHANGED_COMMENT_HEAD" >&2; \
			exit 2; \
		fi; \
		${PYTHON} tools/check_changed_comments.py --root ${TOPSRC} \
		    --base ${CHANGED_COMMENT_BASE:Q} \
		    --revision ${CHANGED_COMMENT_HEAD:Q}

.PHONY: check-comment-hygiene check-changed-comments

# Added text: whitespace errors anywhere and British spellings in Markdown
# prose (tools/check_changed_text.py). check-text-hygiene calibrates the
# checker on throwaway repositories; check-changed-text enforces it on an
# explicit commit range, the same pair of commits check-changed-comments
# compares.
check-text-hygiene: check-python
		${PYTHON} tools/changed_text_test.py

check-changed-text: check-python
		@set -eu; \
		if [ -z "${CHANGED_COMMENT_BASE}" ] || \
		    [ -z "${CHANGED_COMMENT_HEAD}" ]; then \
			echo "check-changed-text requires CHANGED_COMMENT_BASE and CHANGED_COMMENT_HEAD" >&2; \
			exit 2; \
		fi; \
		${PYTHON} tools/check_changed_text.py --root ${TOPSRC} \
		    --base ${CHANGED_COMMENT_BASE:Q} \
		    --revision ${CHANGED_COMMENT_HEAD:Q}

.PHONY: check-text-hygiene check-changed-text

# Production synchronization is independent of the generic-swap line-width test.
check-config-generated-sync: check-python
		HOST_CC=${HOST_CC:Ucc:Q} ${PYTHON} tools/config/generated_sync_test.py
		HOST_CC=${HOST_CC:Ucc:Q} ${PYTHON} tools/config/check_generated_sync.py --root ${TOPSRC} --make ${MAKE}

check-config-include-order: check-python
		HOST_CC=${HOST_CC:Ucc:Q} ${PYTHON} tools/config/check_include_order.py --root ${TOPSRC} --make ${MAKE}

.PHONY: check-config-generated-sync check-config-include-order

# Clean controls must compile; deliberate warnings must fail using the actual
# evaluated commands, including leaves that replace CFLAGS.
check-warning-policy-host:
		${MAKE} -C tests/warning_policy check-host

check-warning-policy-cross:
		${MAKE} -C tests/warning_policy check-cross

check-sh-gnu17: symlinks
		${MAKE} -C bin/sh clean
		${MAKE} -C bin/sh check-gnu17 all

check-sh-c17-contracts: symlinks
		${MAKE} -C bin/sh clean
		${MAKE} -C bin/sh check-strict-c17 check-gnu17 all

.PHONY: check-sh-gnu17 check-sh-c17-contracts

# The inherited sources once passed bare identifiers through macros that
# quoted their parameter names.  Compile the exact consumers so their C17
# assertions bind each terminal action to its intended control byte.
check-control-char-contracts:
		${MAKE} -C tests/control_char_contracts check
		rm -f games/cribbage/io.o usr.bin/tip/cmds.o \
		    usr.bin/tip/cmdtab.o usr.bin/tip/vars.o
		${MAKE} -C games/cribbage io.o
		${MAKE} -C usr.bin/tip cmds.o cmdtab.o vars.o

# A failure injected into each step of lib/Makefile's install and clean
# loops and of the kernel link recipe, with the steps after it asserted
# never to run and the parent make asserted to fail.
check-build-failure:
		${MAKE} -C tests/build_failure check

# The evacuation test compiles the kernel's swapram.c and subr_rmap.c on
# the host; its Makefile is written for GNU make. bmake under -j advertises
# its jobserver to children as "-j N -J fd,fd" in MAKEFLAGS, and GNU make
# rejects -J and prints its usage, so the environment is cleared for the one
# recipe in this tree that runs a foreign make.
check-swapram-evac:
		env -u MAKEFLAGS -u MFLAGS \
		    make -C sys/arch/rp2040/test/swapram evac

check-flash-swap:
		${MAKE} -C tests/rp2040/flash_swap check
		${MAKE} -C sys/arch/rp2040/compile check-flash-swap

check-dhara-metadata: check-python
		${MAKE} -C tests/rp2040/dhara_bounds check PYTHON=${PYTHON:Q}

check-dhara-amplification: check-python
		${MAKE} -C tests/rp2040/dhara_amplification check PYTHON=${PYTHON:Q}

# NetBSD files imported verbatim match tools/netbsd-imports.manifest; the
# selftest first shows verify rejects an altered byte, a missing file, an
# unlisted file and a missing revision.
check-netbsd-imports: check-python
		${PYTHON} tools/netbsd_imports.py selftest
		${PYTHON} tools/netbsd_imports.py verify

check-v7fs-core: check-python
		${MAKE} -C tests/v7fs_core check PYTHON=${PYTHON:Q}

# The inter-core rings and SIO FIFO doorbell of corering.c against a model
# of the FIFO registers, under the address, undefined-behavior and thread
# sanitizers, with sixteen mutants that must fail.
check-corering:
		${MAKE} -C tests/corering check

# Core 1 as the kernel's observer, sys/arch/rp2040/rp2040/core1.c, in three
# evidence classes. check-core1 runs the two decision functions of core1.h
# on the host with mutants. check-core1-image calibrates
# tools/verify_rp2040_core1_image.py on linked fixtures, holds every built
# kernel's core 1 code to SRAM, requires that at least one kernel carries
# such code, and requires every source line that takes execute-in-place
# down to follow a declared section. check-core1-emulation runs a linked
# kernel's own core 0 and core 1 code on two emulated cores and rebuilds
# the kernel from mutated source to calibrate itself; it needs Unicorn
# under ${PYTHON}, so it stands outside "check" as check-renode does.
check-core1:
		${MAKE} -C tests/core1 check

check-core1-image: check-python
		PYTHON=${PYTHON:Q} GCCPREFIX=${GCCPREFIX:Q} \
		    sh tests/rp2040/core1_image/check.sh
		@set -e; elfs=; for configuration in ${MACHINE_KERNEL_CONFIGS}; do \
			elfs="$$elfs sys/arch/rp2040/compile/$$configuration/unix.elf"; \
		done; \
		${PYTHON} tools/verify_rp2040_core1_image.py $$elfs | tee core1-image.out; \
		if ! grep -q '^PASS core1-image' core1-image.out; then \
			rm -f core1-image.out; \
			echo "check-core1-image: no built kernel carries a core 1 image" >&2; \
			exit 1; \
		fi; \
		rm -f core1-image.out

CORE1_EMULATION_BOARD?=	PICO_UART
check-core1-emulation: check-python
		PYTHON=${PYTHON:Q} MAKE=${MAKE:Q} \
		    sh tests/core1/check-emulation.sh ${CORE1_EMULATION_BOARD}

.PHONY: check-core1 check-core1-image check-core1-emulation

# The rump-kernel size measurement behind docs/research/tiny-rump-components.md.
# It needs a NetBSD checkout, which CI lacks, so it stays outside check:
# NETBSD_SRC defaults to BSD_Workspace/netbsd-src beside the primary checkout,
# and MEASURE_OUTDIR, when given, must be absent or empty.
measure-tiny-rump:
		NETBSD_SRC=${NETBSD_SRC:Q} MEASURE_OUTDIR=${MEASURE_OUTDIR:Q} \
		    sh docs/research/tiny-rump-components/measure.sh

check-storage-counters: tools
		sh tools/verify_storage_counters.sh

check-storage-correctness: check-python
		${PYTHON} tests/kernel/check_correctness.py

check-namei-user-path: check-python
		${PYTHON} tests/rp2040/namei_path/check_origin.py --root ${TOPSRC:Q}
		sh tests/rp2040/namei_path/check.sh

check-rp2040-usercopy-span: check-python
		${PYTHON} tests/rp2040/usercopy_span/check.py \
		    --root ${TOPSRC:Q} --host-cc ${HOST_CC:Ucc:Q} \
		    --output ${TOPSRC:Q}/.build-logs/usercopy-span-contracts.json

check-usb-reset:
		${MAKE} -C tests/rp2040/usb_reset check

# The PicoCalc console's terminal emulator and key translation against a
# model panel, three calibrating mutants, and the font table's sync with
# its BDF source; tests/picocalc/Makefile states each.
check-picocalc-host:	check-python
		${MAKE} -C tests/picocalc check

# The PicoCalc PSRAM driver against a PIO simulator and a timing model of
# the ESP-PSRAM64H, and calibrating mutants; tests/psram/Makefile
# states each.
check-psram-pio:	check-python
		${MAKE} -C tests/psram check

check-usb-line-coding:
		${MAKE} -C tests/rp2040/usb_line_coding check

check-usb-cdc-requests:
		${MAKE} -C tests/rp2040/usb_cdc_requests check

check-usb-standard-requests:
		${MAKE} -C tests/rp2040/usb_standard_requests check

check-usb-set-configuration:
		${MAKE} -C tests/rp2040/usb_set_configuration check

check-usb-set-address-interface:
		${MAKE} -C tests/rp2040/usb_set_address_interface check

check-usb-get-descriptor:
		${MAKE} -C tests/rp2040/usb_get_descriptor check

check-flash-device-bounds:
		${MAKE} -C tests/rp2040/flash_device_bounds check

check-clock-catchup:
		${MAKE} -C tests/rp2040/clock_catchup check

# tools/elf2aout over the layouts the ARM linker produces. The gate assembles
# and links its own fixtures with the cross toolchain and reads
# lib/elf32-arm.ld, so what it needs is tools, the toolchain and ${PYTHON}
# rather than a built tree; it is a cross contract gate for the isolation the
# other members of that list have, its own temporary directory per run.
check-elf2aout:	tools
		@if [ x"${MACHINE}" != x"rp2040" ]; then \
			echo "check-elf2aout requires MACHINE=rp2040" >&2; \
			exit 2; \
		fi
		${MAKE} -C tests/rp2040/elf2aout_layout check

# Test tiers. Each gate above is its own target; the tiers group them by
# what the host needs, so a machine without the ARM cross toolchain or
# qemu-arm runs the tiers it has and names the rest.
# The cross, qemu, and board-build tiers run after
# "bmake MACHINE=rp2040 build". sys/arch/rp2040/doc/TESTING.md carries
# the matrix, and .github/workflows/firmware.yml runs "check" on Linux.
#
# A tier that compiles names symlinks: share/mk/sys.mk spells the compile
# line -nostdinc -I${TOPSRC}/include, and sys/sys/param.h and
# sys/sys/types.h reach <machine/machparam.h> and <machine/types.h> through
# include/machine. That link is generated rather than tracked, so a fresh
# checkout carries none and "clean" removes it, and .WAIT holds back the
# gates sharing the dependency line until it points at ${MACHINE}. The lint
# and host-package tiers compile no C, and bin/sh/tests/posix-sh.sh builds
# the shell against the host's headers, so those two reach nothing under
# include.
HOST_GATES=	check-architecture-isolation \
		check-agent-instructions check-uf2-validator \
		check-kernel-headroom-validator \
		check-run-logging check-battlestar-generation \
		check-sus-discovery check-sus-requirement-corpus check-dpucp-profile \
		check-dpucp-donor-ledger check-dpucp-board-acceptance \
		check-c17-source-denominator \
		check-capacity-corpus check-user-window-contract \
		check-swapram-link-verifier \
		check-comment-hygiene check-text-hygiene \
		check-warning-policy-host check-build-failure check-analysis \
		check-test-execution \
		check-libc-host-contracts check-libc-assert check-c17-headers \
		check-dirent-contracts check-getwd-lifetime check-libc-output check-sysctl-status check-libc-casefold check-scandir-contracts check-getty-contracts check-init-contracts \
		check-cat-contracts check-colrm-contracts check-unifdef-contracts \
		check-dd-contracts check-find-contracts \
		check-rmdir-contracts check-tee-contracts check-time-contracts \
		check-libtcl-contracts \
		check-du-contracts check-resize-contracts \
		check-aout check-kernel check-fs-stress check-fsck-inode-range \
		check-kernel-metadata check-root-noatime \
		check-libc-environment check-libc-sysctl \
		check-umount-contracts check-backgammon-contracts \
		check-touch-contracts check-printf-contracts \
		check-recovery-utility-contracts check-timeout-contracts \
		check-libc-tempfiles check-libcalc check-libbn check-dcbc \
		check-id-aliases check-tiny-utility-multicall \
		check-portable-utilities \
		check-fgrep-capacity check-crc32 check-config-makefile check-swapram-evac \
		check-dhara-metadata check-dhara-amplification \
		check-netbsd-imports check-v7fs-core check-corering check-core1 \
		check-namei-user-path check-rp2040-usercopy-span \
		check-storage-correctness \
		check-usb-reset check-usb-line-coding check-usb-cdc-requests \
		check-usb-standard-requests \
		check-usb-set-configuration \
		check-usb-set-address-interface check-usb-get-descriptor \
		check-flash-device-bounds check-clock-catchup \
		check-config-generated-sync check-config-include-order \
		check-fs-profiles check-picocalc-host check-psram-pio
HOST_PROGRAM_GATES=	check-stevie-host check-kilo-host \
		check-menu-host check-tail-host check-sort-host check-keen-host \
		check-bubble-host check-fifteen-host check-fourfall-host check-coil-host check-ridge-host \
		check-puzzlebox-host check-calc-host check-sh-editor \
		check-tar-host check-textbox-host check-cpio-host \
		check-compress-host
CROSS_CONTRACT_GATES=	check-warning-policy-cross check-passwd-header-contracts \
		check-control-char-contracts \
		check-sh-c17-contracts .WAIT check-sh-heap-boundary \
		check-libc-aout-contracts \
		check-c17-headers-cross \
		check-libc-alarm-cross \
		check-libc-waitpid-cross \
		check-libc-ctime-cross check-touch-contracts-cross \
		check-printf-contracts-cross check-recovery-utility-contracts-cross \
		check-timeout-contracts-cross \
		check-libc-random-cross \
		check-libc-runtime-limits-cross \
		check-libc-difftime-cross \
		check-dirent-contracts-cross check-getwd-lifetime-cross check-libc-output-cross check-board-libc-output check-libc-casefold-cross check-scandir-contracts-cross \
		check-libcalc-cross check-libbn-cross \
		check-cat-contracts-cross check-colrm-contracts-cross \
		check-unifdef-contracts-cross \
		check-find-contracts-cross \
		check-backgammon-contracts-cross \
		check-rmdir-contracts-cross check-tee-contracts-cross \
		check-time-contracts-cross \
		check-du-contracts-cross check-resize-contracts-cross \
		check-libc-string-security-cross \
		check-stdio-bounds-cross check-elf2aout

# No object of a shipped program defines a global symbol that lib/libc.a also
# defines, because `ld --just-symbols` against a shared libc image rebinds such
# a symbol to the libc address without a diagnostic. The gate reads the built
# program objects, so it runs before the composition gates clean them.
check-libc-export-namespace:
		sh tools/verify_libc_export_namespace.sh --selftest ${TOPSRC:Q}
		sh tools/verify_libc_export_namespace.sh ${TOPSRC:Q}

# Composition gates clean and relink shared program directories, so the cross
# graph runs them after the parallel read-only contract group.
CROSS_COMPOSITION_GATES=	check-libc-array-memory-line-cross \
		.WAIT check-timeout-target-link .WAIT \
		check-multicall-composition

# Shell scripts under shellcheck at error severity and Python under ruff.
check-lint:
		sh tools/check-lint.sh

# Host C compiler and ${PYTHON}: the libc, utility and kernel-model gates and
# every maintained program with a host build and a suite. Each program owns a
# target so the jobserver can schedule independent directories.
check-legacy-pdp11-v6:	check-python .WAIT
		@if [ x"${BUILD_PDP11_V6}" != x"yes" ]; then \
			echo "check-legacy-pdp11-v6 requires BUILD_PDP11_V6=yes" >&2; \
			exit 2; \
		fi
		${MAKE} -C legacy/pdp11-v6 BUILD_PDP11_V6=yes \
		    MACHINE=rp2040 PYTHON=${PYTHON} check-v6

check-stevie-host:
		${MAKE} -C usr.bin/stevie test

check-kilo-host:
		${MAKE} -C usr.bin/kilo test

check-menu-host:
		${MAKE} -C usr.bin/menu test

check-tail-host:
		${MAKE} -C usr.bin/tail test

check-sort-host:
		${MAKE} -C usr.bin/sort test

check-keen-host:
		${MAKE} -C games/keen test

check-bubble-host:
		${MAKE} -C games/bubble test

check-fifteen-host:
		${MAKE} -C games/fifteen test

check-fourfall-host:
		${MAKE} -C games/fourfall test

check-coil-host:
		${MAKE} -C games/coil test

check-ridge-host: check-python
		${MAKE} -C games/ridge test

check-puzzlebox-host:
		${MAKE} -C games/pzcommon test

check-calc-host: check-python .WAIT symlinks
		${MAKE} -C usr.bin/calc test

check-sh-editor:
		${MAKE} -C bin/sh/tests test

# bin/sh owns a private allocator and stack arena over sbrk(2). A linked libc
# allocator would establish a second arena over the same break, so the target
# image must use the shell's allocation-free directory reader instead.
check-sh-heap-boundary: check-python
		${PYTHON} bin/sh/tests/target_heap_boundary.py --self-test
		${PYTHON} bin/sh/tests/target_heap_boundary.py bin/sh/sh.dis

check-tar-host:
		sh bin/tar/tests/tartest.sh
		PYTHON=${PYTHON:Q} sh bin/tar/tests/timestamp_identity_test.sh
		${PYTHON} tools/test_execution.py run tar.hardlink-identity.native -- \
		    sh bin/tar/tests/hardlink_identity_test.sh
		${PYTHON} tools/test_execution.py run tar.regular-publication.native -- \
		    sh bin/tar/tests/regular_publication_identity_test.sh

check-textbox-host:
		sh usr.bin/textbox/tests/run.sh

check-cpio-host: check-python
		${PYTHON} tools/test_execution.py run cpio.archive.native -- \
		    sh usr.bin/cpio/tests/cpiotest.sh
		${PYTHON} tools/test_execution.py run cpio.cleanup.native -- \
		    sh usr.bin/cpio/tests/cleanup_identity_test.sh

check-compress-host: check-python
		PYTHON=${PYTHON:Q} sh usr.bin/compress/tests/compresscheck.sh

# Every root filesystem profile composes, and the checker still decides.
# distrib/rp2040/mkmanifest.py resolves each profile in
# distrib/rp2040/profiles against the closure markers in
# distrib/rp2040/mi.rp2040 and rejects a composition whose hard links,
# parent directories or declared closure dependencies do not resolve;
# --selftest feeds it the compositions that must be rejected, so a checker
# that stopped deciding fails here rather than reporting a clean run.
# tools/fsutil/fsutil.c's add_hardlink() only warns on a missing source and
# leaves the exit status at 0, which is the image this gate keeps from
# being written. check-image-selection.sh holds the Make variables
# IMAGE_WITH and IMAGE_WITHOUT to the same standard: it refuses selections
# that must fail, with the reason, and reads back the member lists the
# games Makefiles receive. sys/arch/rp2040/doc/PROFILES.md is the authority.
check-fs-profiles:	check-python .WAIT
		${PYTHON} distrib/rp2040/mkmanifest.py --check-all \
		    ${FS_CLOSURE_ARGS} ${FS_PROFILE_ARGS} ${FS_SELECT_ARGS} \
		    --append=distrib/rp2040/md.rp2040
		${PYTHON} distrib/rp2040/mkmanifest.py --selftest \
		    ${FS_CLOSURE_ARGS} ${FS_PROFILE_ARGS} ${FS_SELECT_ARGS} \
		    --append=distrib/rp2040/md.rp2040
		sh distrib/rp2040/check-image-selection.sh ${MAKE}

check-host:	check-python .WAIT symlinks .WAIT ${HOST_GATES} ${HOST_PROGRAM_GATES}

# sys/kern/subr_prf.c walks its arguments as four-byte slots, so the kernel's
# own printf is faithful only at ILP32. The gate is an ordinary 32-bit
# userspace binary, not a virtual machine: what it needs is the width of a
# pointer. A host that cannot build 32-bit says so and skips.
check-kernel-ilp32:
		${MAKE} -C tests/kernel check-ilp32

check-tty-layout-cross:
		${MAKE} -C tests/kernel check-tty-layout-cross

check-console-c17-cross:
		${MAKE} -C tests/kernel check-console-c17-cross

check-rp2040-shutdown:
		${MAKE} -C tests/kernel check-shutdown

check-rp2040-shutdown-cross:	check-python kernel
		${MAKE} -C tests/kernel check-shutdown-order
		${PYTHON} tools/verify_rp2040_shutdown_order.py --selftest
		@set -e; for configuration in ${MACHINE_KERNEL_CONFIGS}; do \
			tool_prefix=$$(${MAKE} -C sys/arch/rp2040/compile/$$configuration \
			    -V ARM_GCC_PREFIX); \
			${PYTHON} tools/verify_rp2040_shutdown_order.py \
			    --objdump "$${tool_prefix}-objdump" \
			    --machdep-object \
			    sys/arch/rp2040/compile/$$configuration/machdep.o \
			    --helper-object \
			    sys/arch/rp2040/compile/$$configuration/shutdown_sync.o; \
		done

# The designated multilib job requires receipts from every executable variant.
# The Python boundary runs a separate make with a private receipt directory.
check-ilp32-execution: check-python
		${PYTHON} tools/test_execution.py aggregate --make ${MAKE} \
		    ${.MAKEOVERRIDES:@variable@--make-variable ${variable}=${${variable}:Q}@} \
		    ${TEST_EXECUTION_REPORT:D--report ${TEST_EXECUTION_REPORT:Q}}

check-ilp32-execution-recipes: check-python .WAIT symlinks .WAIT \
		check-posix-sh check-kernel .WAIT check-kernel-ilp32 check-libc-sysctl \
		check-dd-contracts check-umount-contracts check-fsck-inode-range \
		check-backgammon-contracts check-libc-host-contracts check-scandir-contracts \
		check-dirent-contracts check-getwd-lifetime check-libc-output check-sysctl-status check-libc-casefold check-textbox-host \
		check-tar-host check-cpio-host \
		check-libtcl-contracts check-printf-contracts \
		check-printf-contracts-ilp32 check-recovery-utility-contracts \
		check-timeout-contracts check-libcalc check-libbn

check-test-execution: check-python
		${PYTHON} tools/test_execution_test.py

.PHONY: check-ilp32-execution check-ilp32-execution-recipes check-test-execution \
	check-sh-heap-boundary

# The POSIX conformance run of bin/sh builds the shell as a 32-bit host
# binary (it keeps pointers in int), so it needs a Linux x86-64 host with
# the 32-bit libraries and stands apart from the portable host tier.
check-posix-sh:
		${PYTHON} tools/test_execution.py run shell.posix.ilp32 -- \
		    sh bin/sh/tests/posix-sh.sh
		${PYTHON} bin/sh/tests/check_shell_witnesses.py
		${PYTHON} tests/sysctl_status/check_workload.py ${TOPSRC} bin/sh/tests/out/sh

# The arm cross toolchain and a built tree: isolated contract directories run
# concurrently. Kernel gates stay ordered because they rebuild and inspect the
# same PICO, PICO_UART, SwapRAM and exec-spool outputs. The assembler runs last
# because its link proof rebuilds the shared a.out libc.
check-cross-contracts:	${CROSS_CONTRACT_GATES}

check-cross-kernel:	check-c17-tool-capture .WAIT check-divider .WAIT check-swapram .WAIT \
		check-cache-footprint .WAIT check-exec-spool .WAIT \
		check-ufs-prototypes .WAIT check-c17-kernel-inventory .WAIT \
		check-kernel-printf-formats .WAIT check-tty-layout-cross .WAIT \
		check-console-c17-cross .WAIT \
		check-ptrace-libc-abi .WAIT \
		check-hsaout .WAIT check-hsx-decoder .WAIT check-flash-swap .WAIT \
		check-rp2040-shutdown-cross .WAIT check-storage-counters .WAIT \
		check-core1-image .WAIT \
		check-kernel-headroom

check-cross-assembler:
		${MAKE} -C usr.bin/as/tests test
		${MAKE} -C tests/rp2040/divider_ownership check

check-cross:	check-python .WAIT symlinks .WAIT check-cross-contracts .WAIT \
		check-libc-export-namespace .WAIT ${CROSS_COMPOSITION_GATES} .WAIT \
		check-cross-kernel .WAIT \
		check-cross-assembler

# qemu-arm: the u-area exchange loop, the AEABI division helpers over a
# divider model, the Smaller C suite, the libcalc and libbn tests, the calc
# line interface, the dc evaluator, the libc strtol/strtoul, random and
# printf contracts and the tree formatter's float conversions and math
# library returns executed as Thumb-1 programs rather than only linked, and
# a DiscoBSD a.out run through tests/qemu_aout's syscall loader with the
# tree's libc, crt0 and boot ROM float glue.
# tests/qemu_cpu_model first proves that the ARMv6-M CPU model the libc
# contracts run on faults on Thumb-2 and unaligned accesses.
check-qemu:	check-python .WAIT symlinks
		REQUIRE_QEMU=1 ${MAKE} -C tests/qemu_cpu_model check
		${MAKE} -C tests/rp2040/uarea_exchange check
		REQUIRE_QEMU=1 ${MAKE} -C tests/rp2040/divider_helpers check
		REQUIRE_QEMU=1 ${MAKE} -C usr.bin/smlrc test
		REQUIRE_QEMU=1 ${MAKE} -C tests/libcalc check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C usr.bin/calc check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C tests/libbn check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C tests/dcbc check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C tests/libc_contracts check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C tests/c17_headers check-qemu
		REQUIRE_QEMU=1 ${MAKE} -C tests/qemu_aout check

# Renode with the third-party RP2040 models: the kernel boots from the real
# boot ROM and the gate asserts the console lines through the device probe.
# Not in "check": it wants an emulator, a fetched model tree and a built
# image, and tools/renode/check-boot.sh names whichever is missing. It loads
# the selected board's root, ${FLASHBIN}: every kernel boots every root.
check-renode:
		sh tools/renode/check-boot.sh ${FLASHBIN}

# The PICOCALC kernel under Renode with the ST7365P panel and the keyboard
# controller modeled: the boot banner, the panel's registers, a login and a
# command typed through the I2C keyboard, all read back from the decoded
# frame. Outside "check" for the reason check-renode is, and a separate
# evidence class from the board: tools/renode/check-picocalc.sh names what
# is missing. It loads ${FLASHBIN}, the selected board's root, so
# BOARD=PICOCALC pairs the PICOCALC kernel with the PICOCALC profile's root.
check-renode-picocalc:
		sh tools/renode/check-picocalc.sh ${FLASHBIN}

# The SRAM-resident board probes, flash-id and flash-semantics, built
# against a Pico SDK that tools/pico-sdk/sdk-path.sh resolves. Outside
# check for the reason check-renode is: it wants cmake and a fetched SDK.
check-flash-id:
		sh tools/pico-sdk/check-flash-id.sh

# The discobsd-host Python package: its own lint and tests.
check-host-package:	check-python .WAIT
		cd distrib/rp2040/host && ruff check . && ${PYTHON} -m pytest -q

# The on-device regression programs under tests/rp2040 build and convert
# to a.out; the board runs them.
check-board-build:	symlinks
		${MAKE} -C tests/rp2040 all

check:		check-lint check-host check-posix-sh check-cross check-qemu \
		check-host-package check-board-build

fs:		check-python .WAIT $(FSIMG)

# The image is staged from ${DESTDIR}; etc/passwd, etc/shadow and etc/group
# reach ${DESTDIR}/etc only through etc's own distribution target. Depending on
# it here refreshes them for every image path -- fs, flash, and distribution --
# rather than only the full distribution target, which is why a bare
# "bmake flash" used to reflash a stale account database.
etc-distribution:
		${MAKE} -C etc DESTDIR=${DESTDIR} distribution

# The multicall games link only the members the image selection keeps
# (GAMEBOX_TOOLS and PUZZLEBOX_TOOLS, distrib/rp2040/Makefile.inc), and an
# image path after build names the selection again, so each image relinks
# and restages both boxes: a gamebox staged by an earlier, narrower build
# would otherwise ship without a member whose link the manifest installs.
# The dispatch source is regenerated on every build and the member objects
# stay, so an unchanged selection relinks in place.
multicall-staging:
		for box in games/gamebox games/puzzlebox; do \
			${MAKE} -C ${TOPSRC}/$$box MACHINE=${MACHINE} \
			    DESTDIR=${DESTDIR} install || exit 1; \
		done

# /etc/release names the build the root came from: the commit (with
# -dirty when the tree had changes), the date, and the builder's host OS,
# so a board answers "which firmware is this?" with "cat /etc/release".
# A tree without git history says so rather than failing.
${FSIMG}:	distrib/${MACHINE}/md.${MACHINE} ${MI_MANIFEST} distrib/base/mi.home \
		etc-distribution multicall-staging
		rm -f $@ distrib/$(MACHINE)/_manifest
		commit=`git -C ${TOPSRC} describe --always --dirty --tags 2>/dev/null || echo unknown`; \
		printf 'DiscoBSD/${MACHINE} root built %s from %s on %s\n' \
		    "`date -u +%Y-%m-%dT%H:%M:%SZ`" "$$commit" "`uname -s`" \
		    > ${DESTDIR}/etc/release
		cat ${MI_MANIFEST} distrib/$(MACHINE)/md.$(MACHINE) > distrib/$(MACHINE)/_manifest
		$(FSUTIL) --repartition=${PARTITIONS} $@
		${FSUTIL} --new --partition=1 ${FS_INODES_ARG} --manifest=distrib/${MACHINE}/_manifest $@ ${DESTDIR}
		[ ${U_KBYTES} -eq 0 ] || \
		$(FSUTIL) --new --partition=3 --manifest=distrib/base/mi.home $@ distrib/home

release:
		${MAKE} -C etc MACHINE=${MACHINE} RELEASEDIR=${RELEASEDIR} \
		    FSIMG=${FSIMG} release

clean:
		status=0; rm -f *~ include/machine || status=1; \
		for dir in ${SUBDIR} ${LEGACY_SUBDIRS}; do \
			${MAKE} -C $$dir clean || status=1; \
		done; exit $$status

cleantools:
		status=0; for machine in ${SUPPORTED_MACHINES}; do \
			${MAKE} -C tools MACHINE=$$machine clean || status=1; \
		done; exit $$status

cleankernel:
		status=0; for machine in ${SUPPORTED_MACHINES}; do \
			${MAKE} -C sys/arch/$$machine/compile \
			    MACHINE=$$machine clean || status=1; \
		done; exit $$status

cleantests:
		status=0; for mf in tests/*/Makefile tests/rp2040/*/Makefile \
		    sys/arch/rp2040/test/swapram/Makefile; do \
			[ -f "$$mf" ] || continue; \
			grep -q '^clean:' "$$mf" || continue; \
			${MAKE} -C "$${mf%/Makefile}" clean || status=1; \
		done; exit $$status

# Every board's root image and flash image, which distrib/rp2040/Makefile.inc
# names sdcard${BOARD_OUT_SUFFIX}.img and flash${BOARD_OUT_SUFFIX}.{bin,uf2},
# so a clean after one board's build also removes another's.
cleanfs:
		rm -f distrib/$(MACHINE)/_manifest distrib/$(MACHINE)/_manifest.*
		rm -f $(FSIMG) distrib/$(MACHINE)/sdcard.img \
		    distrib/$(MACHINE)/sdcard-*.img distrib/$(MACHINE)/flash.bin \
		    distrib/$(MACHINE)/flash.uf2 distrib/$(MACHINE)/flash-*.bin \
		    distrib/$(MACHINE)/flash-*.uf2

# A tuple switch is safe only after every source-directory artifact class has
# cleaned successfully. The serialized recipe avoids overlapping cleanup of
# shared directories under -j and leaves the stamp in place on any failure.
cleanall:
		sh tools/clean-build-machines.sh "${TOPSRC}" \
		    "${MAKE}" ${SUPPORTED_MACHINES}

# cleanall, then every untracked file left, ignored files included, against
# tools/clean-residue-allow.txt.  It destroys the build products, so it runs
# last and outside the logged-check aggregate.
check-clean-residue:
		sh tools/check-clean-residue.sh --selftest
		${MAKE} cleanall
		sh tools/check-clean-residue.sh "${TOPSRC}" \
		    "${TOPSRC}/tools/clean-residue-allow.txt"

symlinks:
		rm -f include/machine
		ln -s $(MACHINE) include/machine

installfs:
		@[ -n "${SDCARD}" ] || (echo "SDCARD not defined." && exit 1)
		@[ -f $(FSIMG) ] || $(MAKE) $(FSIMG)
		sudo dd bs=1M if=${FSIMG} of=${SDCARD}

.PHONY:		check-clean-residue check-storage-counters check-storage-correctness check-python check-sus-discovery check-sus-requirement-corpus check-dpucp-profile check-dpucp-donor-ledger check-dpucp-board-acceptance check-warning-policy-host check-warning-policy-cross check-sh-gnu17 check-sh-c17-contracts \
		check-control-char-contracts check-build-failure check-analysis all \
		build distribution release tools kernel check-divider check-swapram \
		check-cache-footprint check-exec-spool check-ufs-prototypes \
		check-tty-layout-cross check-console-c17-cross \
		check-c17-tool-capture check-c17-kernel-inventory check-c17-source-denominator check-c17-compile-profiles check-c17-smaller-c check-capacity-corpus check-user-window-contract check-swapram-link-verifier check-kernel-printf-formats check-elf2aout check-dhara-metadata check-dhara-amplification check-namei-user-path check-rp2040-usercopy-span check-usb-reset check-picocalc-host check-psram-pio check-usb-line-coding check-usb-cdc-requests check-usb-standard-requests check-usb-set-configuration check-usb-set-address-interface check-usb-get-descriptor check-flash-device-bounds check-clock-catchup check-kernel check-kernel-ilp32 check-ptrace-libc-abi \
		check-rp2040-shutdown check-rp2040-shutdown-cross check-libc-export-namespace \
		check-kernel-metadata check-root-noatime \
		check-account-image regen-kernel-metadata check-fs-stress \
		check-fsck-inode-range \
		check-libc-environment check-libc-sysctl check-umount-contracts \
		check-touch-contracts check-printf-contracts \
		check-printf-contracts-ilp32 check-printf-contracts-cross \
		check-recovery-utility-contracts \
		check-recovery-utility-contracts-cross check-timeout-contracts \
		check-timeout-contracts-cross check-timeout-target-link \
		check-libc-tempfiles check-libc-ctime \
		check-libc-ctime-cross check-libc-zone check-libc-mktime \
		check-libc-runtime-limits check-libc-runtime-limits-cross \
		check-libc-difftime check-libc-difftime-cross check-libc-contracts \
		check-libc-host-contracts check-libc-alarm-cross \
		check-libc-waitpid-cross \
		check-libc-aout-contracts check-libc-malloc \
		check-libc-qsort check-libc-strtox check-libc-printf \
		check-libc-random check-libc-random-cross check-libc-scanf \
		check-libc-rwmode check-libc-ansi check-libc-syslog check-libc-vis \
		check-libc-printf-float check-libc-printf-g check-libc-huge-val check-dirent-contracts \
		check-dirent-contracts-cross check-scandir-contracts \
		check-scandir-contracts-cross check-colrm-contracts \
		check-colrm-contracts-cross check-unifdef-contracts \
		check-unifdef-contracts-cross check-libc-string-security \
		check-libc-string-security-cross check-id-aliases \
		check-find-contracts check-find-contracts-cross \
		check-tiny-utility-multicall check-fgrep-capacity check-crc32 check-hsaout check-hsx-decoder \
		check-config-makefile check-portable-utilities \
		check-architecture-isolation legacy-non-arm-verify \
		legacy-pdp11-v6-build legacy-pdp11-v6-install \
		legacy-pdp11-v6-distribution check-legacy-pdp11-v7-runner \
		check-legacy-pdp11-v7-reference check-legacy-pdp11-v6 \
		check-stevie-host check-kilo-host \
		check-menu-host check-tail-host check-sort-host check-keen-host \
		check-bubble-host check-fifteen-host check-fourfall-host check-coil-host check-ridge-host \
		check-puzzlebox-host check-calc-host check-sh-editor check-tar-host \
		check-textbox-host check-cpio-host check-compress-host \
		check-lint check-host \
		check-posix-sh check-cross-contracts check-cross-kernel \
		check-cross-assembler check-cross check-qemu check-renode \
		check-renode-picocalc \
		check-host-package check-board-build check symlinks etc-distribution \
		multicall-staging \
		${FSIMG} fs installfs clean cleantools cleankernel cleantests cleanfs cleanall \
		check-fs-profiles check-dd-contracts \

# Architecture-specific debugging and loading.
-include sys/arch/${MACHINE}/conf/Makefile.inc

.PHONY: check-getwd-lifetime check-getwd-lifetime-cross

.PHONY: check-c17-headers check-c17-headers-cross
.PHONY: check-libc-casefold check-libc-casefold-cross check-libcalc check-libbn check-dcbc \
	check-libcalc-cross check-libbn-cross

.PHONY: check-libc-output check-libc-output-cross

.PHONY: check-board-libc-output

.PHONY: check-sysctl-status

.PHONY: check-passwd-header-contracts
