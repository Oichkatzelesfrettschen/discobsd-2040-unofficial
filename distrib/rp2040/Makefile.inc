# Partition sizes for the Raspberry Pi Pico's onboard flash, in kbytes.
#
# The root lives in the 1344K above the kernel, behind Dhara, which leaves
# 871K of logical blocks: tools/bin/flashimg -c prints that figure for the
# geometry in sys/arch/rp2040/dev/flash.h. Two sectors of partition table
# come first. Swap is not in this image: it is the raw 192K at the top of
# the chip, fl1, which the kernel writes in place.
FS_KBYTES=	840
SWAP_KBYTES=	0
U_KBYTES=	0
MI_MANIFEST=	distrib/rp2040/mi.rp2040
# The root holds about eighty objects; one inode per 16 kbytes gives 64.
FS_INODES=	160

# The flash region image and its UF2, from the disk image above. flashimg
# writes the whole 1536K region so stale journal blocks from an earlier
# image cannot survive beside the new one.
FLASHIMG=	${TOPSRC}/tools/bin/flashimg
FLASHBIN=	${TOPSRC}/distrib/${MACHINE}/flash.bin
FLASHUF2=	${TOPSRC}/distrib/${MACHINE}/flash.uf2

flash:		${FLASHUF2}

${FLASHBIN}:	${FSIMG}
		${FLASHIMG} ${FSIMG} $@

${FLASHUF2}:	${FLASHBIN}
		picotool uf2 convert --quiet ${FLASHBIN} -t bin $@ -o 0x10080000

cleanflash:
		rm -f ${FLASHBIN} ${FLASHUF2}

.PHONY:		flash cleanflash
