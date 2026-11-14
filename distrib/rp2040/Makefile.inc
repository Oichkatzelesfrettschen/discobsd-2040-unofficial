# Partition sizes for the Raspberry Pi Pico's onboard flash, in kbytes.
#
# The root lives in the 1536K above the 128K kernel region, behind Dhara,
# which leaves 989K of logical blocks: tools/bin/flashimg -c prints that
# figure for the geometry in sys/arch/rp2040/dev/flash.h. Two sectors of
# partition table come first. Swap is not in this image: it is the raw
# 384K at the top of the chip, fl1, which the kernel writes in place.
FS_KBYTES=	988
SWAP_KBYTES=	0
U_KBYTES=	0
# The board. Every kernel in sys/arch/rp2040/compile boots any root image
# below, so BOARD chooses only which kernel pairs with flash.uf2 and which
# root profile an unset PROFILE builds: the PICO board's default is full,
# the PicoCalc's is picocalc, which adds the closures that board carries.
# PICOCALC_PSRAM is the PicoCalc kernel with the ESP-PSRAM64H swap driver
# linked in: the same board and profile, selected only by naming it. The
# default BOARD=PICOCALC kernel carries no PSRAM driver.
BOARD?=		PICO
BOARDS=		PICO PICOCALC PICOCALC_PSRAM
.if empty(BOARDS:M${BOARD})
.error BOARD must be one of ${BOARDS}, not ${BOARD}
.endif
BOARD_PROFILE.PICO=	full
BOARD_PROFILE.PICOCALC=	picocalc
BOARD_PROFILE.PICOCALC_PSRAM=	picocalc

# Output paths. The PICO board keeps the bare names distrib/rp2040/sdcard.img,
# flash.bin and flash.uf2 that the documentation, CI and discobsd-flash
# invocations name; every other board writes sdcard-${BOARD}.img,
# flash-${BOARD}.bin and flash-${BOARD}.uf2 beside them, so building one board
# never replaces another board's image. The root Makefile reads FSIMG for the
# image rule, check-account-image, installfs and cleanfs.
.if ${BOARD} == "PICO"
BOARD_OUT_SUFFIX=
.else
BOARD_OUT_SUFFIX=	-${BOARD}
.endif
FSIMG=		${TOPSRC}/distrib/${MACHINE}/sdcard${BOARD_OUT_SUFFIX}.img
MKBOARDUF2=	${TOPSRC}/distrib/rp2040/mkboarduf2.py
UNION_KERNEL_UF2=	${TOPSRC}/sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2
BOARD_KERNEL_UF2=	${TOPSRC}/distrib/${MACHINE}/unix${BOARD_OUT_SUFFIX}.uf2

${BOARD_KERNEL_UF2}:	${UNION_KERNEL_UF2} ${MKBOARDUF2}
		${PYTHON} ${MKBOARDUF2} --board=${BOARD} ${UNION_KERNEL_UF2} $@

# The root filesystem profile. distrib/rp2040/mi.rp2040 brackets each
# optional feature between "#closure NAME" and "#endclosure NAME",
# distrib/rp2040/profiles declares which closures a profile keeps and what
# each closure depends on, and mkmanifest.py composes the manifest the
# image rule reads; the markers are comments, which tools/fsutil/manifest.c
# skips whole. sys/arch/rp2040/doc/PROFILES.md is the authority.
PROFILE?=	${BOARD_PROFILE.${BOARD}}
FS_PROFILES=	distrib/rp2040/profiles
FS_CLOSURES=	distrib/rp2040/mi.rp2040
FS_PROFILE_ARGS=	--profiles=distrib/rp2040/profiles
FS_CLOSURE_ARGS=	--manifest=distrib/rp2040/mi.rp2040
FS_SELECT_ARGS=
MI_MANIFEST_VARIANT=
# Per-build additions and removals on top of PROFILE: space-separated
# closure or group names (bmake MACHINE=rp2040 list-closures prints them).
# mkmanifest.py refuses an unknown name, a closure both added and removed,
# and any dependency the result leaves unmet.
#   bmake MACHINE=rp2040 BOARD=PICOCALC IMAGE_WITHOUT="toolchain toolchainlib" distribution
#   bmake MACHINE=rp2040 PROFILE=base IMAGE_WITH="gamesdir puzzlebox lightup" distribution
# The names reach the shell unquoted, so they hold to the alphabet that
# mkmanifest.py accepts.
IMAGE_WITH?=
IMAGE_WITHOUT?=
.for _v in IMAGE_WITH IMAGE_WITHOUT
.if !empty(${_v}:M*[^-A-Za-z0-9_]*)
.error ${_v} takes closure or group names of letters, digits, - and _, not ${${_v}:M*[^-A-Za-z0-9_]*}
.endif
.endfor
FS_IMAGE_ARGS=	${IMAGE_WITH:@w@--with=${w}@} ${IMAGE_WITHOUT:@w@--without=${w}@}
.if ${BUILD_PDP11_V6} == "yes"
FS_PROFILES+=	legacy/pdp11-v6/distrib/rp2040/profiles
FS_CLOSURES+=	legacy/pdp11-v6/distrib/rp2040/mi.rp2040
FS_PROFILE_ARGS+=	--profiles=legacy/pdp11-v6/distrib/rp2040/profiles
FS_CLOSURE_ARGS+=	--manifest=legacy/pdp11-v6/distrib/rp2040/mi.rp2040
FS_SELECT_ARGS+=	--select-closure=pdp11 --select-closure=v6disk
MI_MANIFEST_VARIANT=	.pdp11-v6
.endif
MKMANIFEST=	distrib/rp2040/mkmanifest.py
MI_MANIFEST:=	distrib/rp2040/_manifest.${PROFILE}${MI_MANIFEST_VARIANT}
FS_COMPOSE=	${PYTHON} ${MKMANIFEST} --profile=${PROFILE} \
		    ${FS_CLOSURE_ARGS} ${FS_PROFILE_ARGS} ${FS_SELECT_ARGS} \
		    ${FS_IMAGE_ARGS} --append=distrib/rp2040/md.rp2040

# The multicall binaries link only the members the selection keeps: the
# games Makefiles read GAMEBOX_TOOLS and PUZZLEBOX_TOOLS, which stay unset
# (and every member builds) when PYTHON names no interpreter yet. A
# selection mkmanifest.py refuses leaves them empty, and check-image-selection,
# which build and fs wait for, reports the refusal before any program builds.
.if !empty(PYTHON)
.for _h in gamebox puzzlebox
${_h:tu}_TOOLS!=	${FS_COMPOSE} --members=${_h} 2>/dev/null || true
.export ${_h:tu}_TOOLS
.endfor
.endif
# The shipped root uses fewer than 100 objects. Eight inode blocks provide
# 128 slots while recovering eight 1-KB data blocks from the fixed image.
FS_INODES=	128

# The composed manifest the image rule concatenates with md.rp2040. The
# md part reaches the checker as --append, so a hard link that crosses the
# two files still resolves against one object set. IMAGE_WITH and
# IMAGE_WITHOUT change the composition without changing a file the rule
# names, so the phony prerequisite recomposes on every invocation and
# emit() leaves the file alone when the text is unchanged.
${MI_MANIFEST}:	${FS_CLOSURES} ${FS_PROFILES} ${MKMANIFEST} image-selection
		${FS_COMPOSE} --output=$@

image-selection:

# Decide the selection alone, ahead of the program builds.
check-image-selection:	check-python .WAIT
		${FS_COMPOSE} >/dev/null

# Print each closure, its groups, dependencies, measured cost and the
# selection PROFILE, IMAGE_WITH and IMAGE_WITHOUT keep (marked with *).
list-closures:	check-python .WAIT
		@${PYTHON} ${MKMANIFEST} --list-closures --profile=${PROFILE} \
		    ${FS_CLOSURE_ARGS} ${FS_PROFILE_ARGS} ${FS_SELECT_ARGS} \
		    ${FS_IMAGE_ARGS} --append=distrib/rp2040/md.rp2040

build fs:	check-image-selection

.PHONY:		image-selection check-image-selection list-closures

# The top-level Makefile does not include share/mk/sys.mk, so the tool
# paths and install command it defines are not otherwise in scope here.
TOOLDIR?=	${TOPSRC}/tools
TOOLBINDIR?=	${TOOLDIR}/bin/${MACHINE}
# Plain "=": bmake's own built-in sys.mk sets INSTALL=install ahead of this
# file, and "?=" would leave that in place instead of the tree's binstall.
INSTALL=	${TOOLBINDIR}/binstall -U

# The flash region image and its UF2, from the disk image above. flashimg
# writes the whole 1536K region so stale journal blocks from an earlier
# image cannot survive beside the new one.
FLASHIMG=	${TOOLBINDIR}/flashimg
FLASHBIN=	${TOPSRC}/distrib/${MACHINE}/flash${BOARD_OUT_SUFFIX}.bin
FLASHUF2=	${TOPSRC}/distrib/${MACHINE}/flash${BOARD_OUT_SUFFIX}.uf2
# Where the region starts: FLASH_XIP_BASE + FLASH_FS_OFFSET in
# sys/arch/rp2040/dev/flash.h, which flashimg also prints.
FLASH_FS_ADDR=	0x10020000

flash:		check-account-image .WAIT ${BOARD_KERNEL_UF2} ${FLASHUF2}

${FLASHBIN}:	${FSIMG}
		${FLASHIMG} ${FSIMG} $@

${FLASHUF2}:	${FLASHBIN}
		picotool uf2 convert --quiet ${FLASHBIN} -t bin $@ -o ${FLASH_FS_ADDR}

cleanflash:
		rm -f ${BOARD_KERNEL_UF2} ${FLASHBIN} ${FLASHUF2}

.PHONY:		flash cleanflash

# The board's own /usr/lib/libc.a: distrib/rp2040/mkboardlibc.py compiles
# just the members distrib/rp2040/boardlibc-members names (the closure
# distrib/rp2040/libc-sink.c pulls out of the full, 331 KB lib/libc.a, read
# off a linker map -- see research/board-libc.md) with the tree's own as
# and ar, in a.out format throughout, since usr.bin/ld on the board reads
# a.out only. lib/libc_aout/libc's full a.out build does not fit the root
# and is not installed for rp2040 (see lib/libc_aout/libc/Makefile).
PYTHON?=
MKBOARDLIBC=	${TOPSRC}/distrib/rp2040/mkboardlibc.py
BOARDLIBC_WORK=	${TOPSRC}/distrib/obj/boardlibc.${MACHINE}

# Matches share/mk/sys.mk's own GCCPREFIX branch for MACHINE_ARCH=arm: the
# top-level Makefile does not include sys.mk, so this repeats the same
# per-host-OS lookup rather than assuming the Linux path.
_HOST_OSNAME!=	uname -s
BOARDLIBC_GCCPREFIX!=if command -v arm-none-eabi-gcc >/dev/null 2>&1 ; then \
			echo "$$(command -v arm-none-eabi-gcc | sed 's/-gcc$$//')" ; \
		elif [ x"${_HOST_OSNAME}" = x"OpenBSD" ] ; then \
			echo "/usr/local/bin/arm-none-eabi" ; \
		elif [ x"${_HOST_OSNAME}" = x"FreeBSD" ] ; then \
			echo "/usr/local/gcc-arm-embedded/bin/arm-none-eabi" ; \
		else \
			echo "/usr/bin/arm-none-eabi" ; \
		fi

${BOARDLIBC_WORK}/libc.a:	${MKBOARDLIBC} \
				${TOPSRC}/lib/libc/stdlib/abs.c \
				${TOPSRC}/lib/libc/stdlib/labs.c \
				${TOPSRC}/lib/libc/stdlib/iso_exit.c \
				${TOPSRC}/lib/libc/stdlib/at_quick_exit.c \
				${TOPSRC}/lib/libc/stdlib/quick_exit.c \
				${TOPSRC}/lib/libc/stdlib/quick_exit.h \
				${TOPSRC}/distrib/rp2040/boardlibc-members \
				${TOPSRC}/include/string.h \
				${TOPSRC}/include/_featuretest.h \
				${TOPSRC}/lib/libc/arm/gen/rom_float_members \
				${TOPSRC}/lib/libc/arm/gen/rom_float.inc \
				${TOPSRC}/lib/libc/arm/gen/rom_float_*.S \
				${TOPSRC}/lib/libc/stdio/doprnt.c \
				${TOPSRC}/lib/libc/stdio/printf.c \
				${TOPSRC}/lib/libc/stdio/sprintf.c \
				${TOPSRC}/lib/libc/stdio/fprintf.c \
				${TOPSRC}/lib/libc/stdio/vfprintf.c \
				${TOPSRC}/lib/libc/stdio/output.h \
				${TOPSRC}/lib/libc/stdio/fputs.c \
				${TOPSRC}/lib/libc/gen/confstr.c \
				${TOPSRC}/lib/libc/gen/ctermid.c \
				${TOPSRC}/lib/libc/gen/difftime.c \
				${TOPSRC}/lib/libc/gen/raise.c \
				${TOPSRC}/lib/libc/gen/stpcpy.c \
				${TOPSRC}/lib/libc/gen/stpncpy.c \
				${TOPSRC}/lib/libc/gen/strndup.c \
				${TOPSRC}/lib/libc/gen/strnlen.c \
				${TOPSRC}/lib/libc/gen/sysconf.c \
				${TOPSRC}/lib/libc/gen/vis.c \
				${TOPSRC}/lib/libc/string/explicit_bzero.c \
				${TOPSRC}/lib/libc/string/timingsafe_bcmp.c \
				${TOOLBINDIR}/as ${TOOLBINDIR}/ar ${TOOLBINDIR}/ranlib
		${INSTALL} -d ${DESTDIR}/usr/lib
		${PYTHON} ${MKBOARDLIBC} --toolbindir=${TOOLBINDIR} \
			--gccprefix=${BOARDLIBC_GCCPREFIX} \
			--include=${TOPSRC}/include --workdir=${BOARDLIBC_WORK} \
			--out=$@

# crt0.o comes from the existing a.out startup build (lib/libc_aout/startup),
# which already compiles lib/startup-arm/crt0.c with the tree's own as via
# -B; it needs no reduction since it is one small object.
${DESTDIR}/usr/lib/crt0.o:	${TOOLBINDIR}/as
		${MAKE} -C ${TOPSRC}/lib/libc_aout/startup MACHINE=${MACHINE} \
			TOOLDIR=${TOOLDIR}
		${INSTALL} -d ${DESTDIR}/usr/lib
		${INSTALL} ${TOPSRC}/lib/libc_aout/crt0.o $@


# distrib/rp2040/cc is a plain POSIX sh script, not a program a Makefile
# builds; install it straight from the source tree.
${DESTDIR}/usr/bin/cc:	${TOPSRC}/distrib/rp2040/cc
		${INSTALL} -d ${DESTDIR}/usr/bin
		${INSTALL} -m 555 ${TOPSRC}/distrib/rp2040/cc $@

# lib/libc_aout installs its whole archive at the same path during the
# build, so the trimmed one is copied over it right before the image is
# made. The copy is newer than the symbol table mkboardlibc.py dated, and
# the board's ld reads an archive more than two seconds newer than its
# table as unsorted, so ranlib -t dates the table again after the copy, as
# lib/libc_aout's install targets do.
boardlibc-install:	${BOARDLIBC_WORK}/libc.a ${DESTDIR}/usr/lib/crt0.o \
			${DESTDIR}/usr/bin/cc
		${INSTALL} -d ${DESTDIR}/usr/lib
		${INSTALL} ${BOARDLIBC_WORK}/libc.a ${DESTDIR}/usr/lib/libc.a
		${TOOLBINDIR}/ranlib -t ${DESTDIR}/usr/lib/libc.a

${FSIMG}:	boardlibc-install

.PHONY:		boardlibc-install
