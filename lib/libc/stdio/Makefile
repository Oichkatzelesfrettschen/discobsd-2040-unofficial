#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= ${DEFS} -Os

# The stream core is the 4.4BSD stdio as 2.11BSD patch 499 shrinks it for a
# small machine: one shared default fileops block, no block-size buffer
# probing, no fseek optimization. The formatter (doprnt.c) and the scanner
# (doscan.c) are this tree's own and sit on the core's getc and putc.
CORESRC		= clrerr.c exit.c fclose.c fdopen.c findfp.c fflush.c fopen.c \
		  fpurge.c fread.c freopen.c fseek.c ftell.c fwrite.c fvwrite.c \
		  getc.c getchar.c getw.c makebuf.c rget.c fputc.c putc.c \
		  putchar.c putw.c rewind.c setbuf.c setbuffer.c setvbuf.c \
		  stdio.c ungetc.c wbuf.c flags.c \
		  refill.c wsetup.c puts.c fputs.c fgetc.c fgets.c gets.c remove.c \
		  clnup.c
FMTSRC		= doprnt.c doprnt_float.c fprintf.c printf.c snprintf.c \
		  sprintf.c vfprintf.c vprintf.c doscan.c doscan_float.c scanf.c
SRCS		= ${CORESRC} ${FMTSRC}
OBJS		= ${SRCS:.c=.o}

TAGSFILE	= tags

stdio.a:	${OBJS}
		@echo "building stdio.a"
		@${AR} cr stdio.a ${OBJS}

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

clean:
		rm -f stdio.a *.o *~ profiled/*.o tags Makefile.bak

depend:
		for i in ${SRCS}; do \
		    cc -M ${CFLAGS} $$i | awk ' { if ($$1 != prev) \
			{ if (rec != "") print rec; rec = $$0; prev = $$1; } \
			else { if (length(rec $$2) > 78) { print rec; rec = $$0; } \
			else rec = rec " " $$2 } } \
			END { print rec } ' >> makedep; done
		echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
		echo '$$r makedep' >>eddep
		echo 'w' >>eddep
		cp Makefile Makefile.bak
		ed - Makefile < eddep
		rm eddep makedep
		echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile
		echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile
		echo '# see make depend above' >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
