# Canonical machine registry for the maintained tree.
#
# Bmake defines MACHINE and MACHINE_ARCH from the build host before reading a
# Makefile. Plain assignments replace those built-ins, while command-line
# assignments retain precedence. The explicit-value checks reject a caller
# that tries to split a machine from its architecture or CPU.

.if !empty(.MAKEOVERRIDES:M_DISCOBSD*) || \
    !empty(.MAKEOVERRIDES:M_EXPECTED_MACHINE_*) || \
    !empty(.MAKEOVERRIDES:M_ARCHITECTURE_*)
.error private architecture registry variables cannot be overridden
.endif
.if !empty(.MAKEOVERRIDES:MSUPPORTED_MACHINES) || \
    !empty(.MAKEOVERRIDES:MMACHINE_DEFAULT) || \
    !empty(.MAKEOVERRIDES:MMACHINE_KERNEL_CONFIGS) || \
    !empty(.MAKEOVERRIDES:MMACHINE_ARCH_FLAGS) || \
    !empty(.MAKEOVERRIDES:MLEGACY_SUBDIRS) || \
    !empty(.MAKEOVERRIDES:MARCHITECTURE_STAMP)
.error derived architecture registry variables cannot be overridden
.endif

SUPPORTED_MACHINES=     rp2040 stm32
MACHINE_DEFAULT?=       rp2040
MACHINE:=               ${MACHINE_DEFAULT}

.if ${MACHINE} == "rp2040"
_EXPECTED_MACHINE_ARCH= arm
_EXPECTED_MACHINE_CPU=  cortex-m0plus
MACHINE_KERNEL_CONFIGS= PICO PICO_UART
.elif ${MACHINE} == "stm32"
_EXPECTED_MACHINE_ARCH= arm
_EXPECTED_MACHINE_CPU=  cortex-m4
MACHINE_KERNEL_CONFIGS= F405WEACTCORE F411RENUCLEO F412GDISCO \
                        F412WEACTCORE F413HDISCO F446RENUCLEO \
                        F446WEACTCORE F469IDISCO F4DISCOVERY F4VEDEVEBOX
.else
.error unsupported MACHINE=${MACHINE}; supported machines: ${SUPPORTED_MACHINES}
.endif

.if !empty(.MAKEOVERRIDES:MMACHINE_ARCH)
.if ${MACHINE_ARCH} != ${_EXPECTED_MACHINE_ARCH}
.error MACHINE=${MACHINE} requires MACHINE_ARCH=${_EXPECTED_MACHINE_ARCH}, got ${MACHINE_ARCH}
.endif
.endif
MACHINE_ARCH:=          ${_EXPECTED_MACHINE_ARCH}

.if !empty(.MAKEOVERRIDES:MMACHINE_CPU)
.if ${MACHINE_CPU} != ${_EXPECTED_MACHINE_CPU}
.error MACHINE=${MACHINE} requires MACHINE_CPU=${_EXPECTED_MACHINE_CPU}, got ${MACHINE_CPU}
.endif
.endif
MACHINE_CPU:=           ${_EXPECTED_MACHINE_CPU}

# The maintained machines share AAPCS, little-endian, soft-float Thumb. CPU
# selection is the only ISA-width difference. Callers tune optimization and
# feature definitions through COPTS, CFLAGS and DEFS; they cannot replace the
# architecture contract.
MACHINE_ARCH_FLAGS:=    -mcpu=${MACHINE_CPU} -mabi=aapcs -mlittle-endian \
                        -mthumb -mfloat-abi=soft

TOOLDIR?=               ${TOPSRC}/tools
.if !empty(.MAKEOVERRIDES:MTOOLBINDIR) && \
    ${TOOLBINDIR} != "${TOOLDIR}/bin/${MACHINE}"
.error TOOLBINDIR must be ${TOOLDIR}/bin/${MACHINE}, got ${TOOLBINDIR}
.endif
TOOLBINDIR:=            ${TOOLDIR}/bin/${MACHINE}

BUILD_LEGACY_NON_ARM?=  no
BUILD_PDP11_V6?=        no

.if ${BUILD_LEGACY_NON_ARM} != "no" && ${BUILD_LEGACY_NON_ARM} != "yes"
.error BUILD_LEGACY_NON_ARM must be yes or no
.endif
.if ${BUILD_PDP11_V6} != "no" && ${BUILD_PDP11_V6} != "yes"
.error BUILD_PDP11_V6 must be yes or no
.endif
.if ${BUILD_PDP11_V6} == "yes" && ${MACHINE} != "rp2040"
.error BUILD_PDP11_V6=yes supports MACHINE=rp2040 only
.endif

# The PDP-11/V6 subtree joins ordinary build and install traversal only after
# the caller selects the option. The non-ARM archive has no supported build
# and remains reachable through its explicit verification target alone.
LEGACY_SUBDIRS=
.if ${BUILD_PDP11_V6} == "yes"
LEGACY_SUBDIRS+=         legacy/pdp11-v6
.endif

# Source-directory objects and libraries are shared by the retained machines.
# The guard admits one canonical tuple per worktree until cleanall removes all
# shared artifacts and the stamp. Machine-qualified installed tools close the
# separate host-tool collision.
ARCHITECTURE_STAMP:=     ${TOPSRC}/distrib/obj/.build-machine
_ARCHITECTURE_CLEAN_TARGETS= clean cleanall clean-all cleantools cleankernel \
                            cleanfs cleanflash
_ARCHITECTURE_ACTIVE_TARGETS= ${.TARGETS}
.for _clean_target in ${_ARCHITECTURE_CLEAN_TARGETS}
_ARCHITECTURE_ACTIVE_TARGETS:= ${_ARCHITECTURE_ACTIVE_TARGETS:N${_clean_target}}
.endfor

.if empty(.TARGETS) || !empty(_ARCHITECTURE_ACTIVE_TARGETS)
.BEGIN:
	@sh "${TOPSRC}/tools/check-build-machine.sh" \
	    "${ARCHITECTURE_STAMP}" "${MACHINE}" \
	    "${MACHINE_ARCH}" "${MACHINE_CPU}"
.endif

.export MACHINE MACHINE_ARCH MACHINE_CPU BUILD_LEGACY_NON_ARM BUILD_PDP11_V6
