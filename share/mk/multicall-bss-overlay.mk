# A multicall process enters exactly one applet and then exits. Applet-private
# zero-initialized storage can therefore share one address range, while libc
# and dispatcher BSS remain outside the overlay. The fragment precedes the
# main user linker script so its input sections are consumed before .bss.*.
# sys/arch/rp2040/doc/MULTICALL-BSS-OVERLAY.md records the lifetime
# proof, address-layout checks, and measured process-image reductions.
MULTICALL_BSS_LDSCRIPT= ${.CURDIR}/multicall-bss-overlay.ld
MULTICALL_BSS_GENERATOR= ${TOPSRC}/tools/generate_multicall_bss_overlay.sh
MULTICALL_BSS_VERIFIER= ${TOPSRC}/tools/verify_multicall_bss_objects.sh
MULTICALL_BSS_LDFLAGS= -Wl,-T,${MULTICALL_BSS_LDSCRIPT}
# Convert tentative applet definitions into owned BSS before the relocatable
# link so every overlay input has one explicit section owner.
MULTICALL_APPLET_COPTS= ${COPTS} -fno-common

${MULTICALL_BSS_LDSCRIPT}: ${OBJS} ${MULTICALL_BSS_GENERATOR} ${MULTICALL_BSS_VERIFIER}
	sh ${MULTICALL_BSS_VERIFIER} "${GCCPREFIX}-nm" \
	    "${GCCPREFIX}-readelf" ${OBJS}
	sh ${MULTICALL_BSS_GENERATOR} ${MULTICALL_BSS_LDSCRIPT} ${OBJS}

clean-multicall-bss-overlay:
	rm -f ${MULTICALL_BSS_LDSCRIPT}

clean: clean-multicall-bss-overlay

# The localized objects depend on compiler flags and recursive source-directory
# builds that make cannot see from a box directory. Recreate each object on
# every box build so a flag or source transition cannot preserve stale input.
.PHONY: ${OBJS} clean-multicall-bss-overlay
