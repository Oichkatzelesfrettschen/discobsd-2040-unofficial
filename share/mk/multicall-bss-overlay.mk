# A multicall process enters exactly one applet and then exits. Applet-private
# zero-initialized storage can therefore share one address range, while libc
# and dispatcher BSS remain outside the overlay. The fragment precedes the
# main user linker script so its input sections are consumed before .bss.*.
# sys/arch/rp2040/doc/research/multicall-bss-overlay.md records the lifetime
# proof, address-layout checks, and measured process-image reductions.
MULTICALL_BSS_LDSCRIPT= ${.CURDIR}/multicall-bss-overlay.ld
MULTICALL_BSS_GENERATOR= ${TOPSRC}/tools/generate_multicall_bss_overlay.sh
MULTICALL_BSS_LDFLAGS= -Wl,-T,${MULTICALL_BSS_LDSCRIPT}

${MULTICALL_BSS_LDSCRIPT}: ${OBJS} ${MULTICALL_BSS_GENERATOR}
	sh ${MULTICALL_BSS_GENERATOR} ${MULTICALL_BSS_LDSCRIPT} ${OBJS}

clean-multicall-bss-overlay:
	rm -f ${MULTICALL_BSS_LDSCRIPT}

clean: clean-multicall-bss-overlay

.PHONY: clean-multicall-bss-overlay
