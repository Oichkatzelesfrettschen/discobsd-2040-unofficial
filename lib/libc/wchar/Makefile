#
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# The wide character members of C17 7.28, 7.29 and 7.30: one function per
# member, so a program links only what it calls.
#
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

DEFS		=
CFLAGS		+= ${DEFS} -Os -Wstrict-prototypes -Wold-style-definition

SRCS=		 btowc.c c16rtomb.c c32rtomb.c iswalnum.c iswalpha.c iswblank.c \
		 iswcntrl.c iswctype.c iswdigit.c iswgraph.c iswlower.c iswprint.c \
		 iswpunct.c iswspace.c iswupper.c iswxdigit.c mbrlen.c mbrtoc16.c \
		 mbrtoc32.c mbrtowc.c mbsinit.c mbsrtowcs.c towctrans.c towlower.c \
		 towupper.c wcrtomb.c wcscat.c wcschr.c wcscmp.c wcscoll.c wcscpy.c \
		 wcscspn.c wcslen.c wcsncat.c wcsncmp.c wcsncpy.c wcspbrk.c wcsrchr.c \
		 wcsrtombs.c wcsspn.c wcsstr.c wcstod.c wcstof.c wcstok.c wcstol.c \
		 wcstoll.c wcstoul.c wcstoull.c wcsxfrm.c wctob.c wctrans.c wctype.c \
		 wmemchr.c wmemcmp.c wmemcpy.c wmemmove.c wmemset.c

OBJS=		 btowc.o c16rtomb.o c32rtomb.o iswalnum.o iswalpha.o iswblank.o \
		 iswcntrl.o iswctype.o iswdigit.o iswgraph.o iswlower.o iswprint.o \
		 iswpunct.o iswspace.o iswupper.o iswxdigit.o mbrlen.o mbrtoc16.o \
		 mbrtoc32.o mbrtowc.o mbsinit.o mbsrtowcs.o towctrans.o towlower.o \
		 towupper.o wcrtomb.o wcscat.o wcschr.o wcscmp.o wcscoll.o wcscpy.o \
		 wcscspn.o wcslen.o wcsncat.o wcsncmp.o wcsncpy.o wcspbrk.o wcsrchr.o \
		 wcsrtombs.o wcsspn.o wcsstr.o wcstod.o wcstof.o wcstok.o wcstol.o \
		 wcstoll.o wcstoul.o wcstoull.o wcsxfrm.o wctob.o wctrans.o wctype.o \
		 wmemchr.o wmemcmp.o wmemcpy.o wmemmove.o wmemset.o

all:		wchar.a

wchar.a:	${OBJS}
		@echo "building wchar.a"
		@${AR} cr wchar.a ${OBJS}

clean:
		rm -f *.o *~ wchar.a

.PHONY:		all clean
