/*
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/types.h>
#include <syslog.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

#define	STDERR_FILENO	2
#define	SYSLOG_BUFSIZE	512

static	int	LogFile = -1;		/* fd for log */
static	int	LogStat = 0;		/* status bits, set by openlog() */
static	const char *LogTag = NULL;	/* string to tag the entry with */
static	int	LogFacility = LOG_USER;	/* default facility code */
static	int	LogMask = 0xff;		/* mask of priorities to be logged */

extern	int	errno;			/* error number */

static char *advance_buffer(char *, size_t *, int);
static char *append_string(char *, size_t *, const char *);

static char *
advance_buffer(char *cursor, size_t *remaining, int length)
{
	size_t used;

	used = length < 0 ? 0 : (size_t)length;
	if (used >= *remaining)
		used = *remaining - 1;
	*remaining -= used;
	return cursor + used;
}

static char *
append_string(char *cursor, size_t *remaining, const char *source)
{
	while (*source != '\0' && *remaining > 1) {
		*cursor++ = *source++;
		(*remaining)--;
	}
	return cursor;
}

/*
 * syslog, vsyslog --
 *	print message on log file; output is intended for syslogd(8).
 *	No sockets: logfile is used.
 */
void
vsyslog(int pri, const char *fmt, va_list ap)
{
	char tbuf[SYSLOG_BUFSIZE];
	char number[13];	/* delimiters, ten uint32 digits, and NUL */
	char *cursor, *message, *stdp = tbuf;
	FILE message_stream;
	size_t remaining;
	time_t now;
	struct tm *local_time;
	int cnt, fd, length, saved_errno;
	pid_t pid;

#define	INTERNALLOG	LOG_ERR|LOG_CONS|LOG_PERROR|LOG_PID
	/* Check for invalid bits. */
	if (pri & ~(LOG_PRIMASK|LOG_FACMASK)) {
		syslog(INTERNALLOG,
		    "syslog: bad fac/pri: %x", pri);
		pri &= LOG_PRIMASK|LOG_FACMASK;
	}

	/* Check priority against setlogmask values. */
	if (!(LOG_MASK(LOG_PRI(pri)) & LogMask))
		return;

	saved_errno = errno;

	/* Set default facility if none specified. */
	if ((pri & LOG_FACMASK) == 0)
		pri |= LogFacility;

	/* Two bytes remain outside the formatter for the logfile CRLF. */
	cursor = tbuf;
	remaining = sizeof(tbuf) - 2;
	(void)sprintf(number, "<%u>", (unsigned int)pri);
	cursor = append_string(cursor, &remaining, number);

	(void)time(&now);
	local_time = localtime(&now);
	if (local_time != NULL) {
		length = (int)strftime(cursor, remaining, "%h %e %T ",
		    local_time);
		cursor = advance_buffer(cursor, &remaining, length);
	}
	if (LogStat & LOG_PERROR)
		stdp = cursor;
	if (LogTag == NULL)
		LogTag = __progname;
	if (LogTag != NULL)
		cursor = append_string(cursor, &remaining, LogTag);
	if (LogStat & LOG_PID) {
		(void)sprintf(number, "[%u]", (unsigned int)getpid());
		cursor = append_string(cursor, &remaining, number);
	}
	if (LogTag != NULL && remaining > 1) {
		*cursor++ = ':';
		remaining--;
	}
	if (LogTag != NULL && remaining > 1) {
		*cursor++ = ' ';
		remaining--;
	}

	/* An exhausted string stream leaves _base free to carry saved errno text. */
	message_stream._flag = _IOWRT | _IOSTRG | _IOSYSLOG;
	message_stream._ptr = cursor;
	message_stream._base = strerror(saved_errno);
	message_stream._cnt = (int)remaining - 1;
	(void)_doprnt(fmt, ap, &message_stream);
	cursor = message_stream._ptr;
	*cursor = '\0';
	cnt = (int)(cursor - tbuf);

	/* Output to stderr if requested. */
	if (LogStat & LOG_PERROR) {
		struct iovec iov[2];
		register struct iovec *v = iov;

		v->iov_base = stdp;
		v->iov_len = cnt - (stdp - tbuf);
		++v;
		v->iov_base = "\n";
		v->iov_len = 1;
		(void)writev(STDERR_FILENO, iov, 2);
	}

	/* Get connected, output the message to the local logger. */
	if (LogFile == -1)
		openlog(LogTag, LogStat | LOG_NDELAY, 0);
	*cursor++ = '\r';
	*cursor++ = '\n';
	*cursor = '\0';
	cnt += 2;
	if (write(LogFile, tbuf, cnt) == cnt)
		return;

	/*
	 * Output the message to the console; don't worry about blocking,
	 * if console blocks everything will.  Make sure the error reported
	 * is the one from the syslogd failure.
	 *
	 * 2.11BSD has to do a more complicated dance because we do not
	 * want to acquire a controlling terminal (bad news for 'init'!).
	 * Until either the tty driver is ported from 4.4 or O_NOCTTY
	 * is implemented we have to fork and let the child do the open of
	 * the console.
	 */
	if (LogStat & LOG_CONS) {
		pid = vfork();
		if (pid == -1)
			return;
		if (pid == 0) {
			fd = open(_PATH_CONSOLE, O_WRONLY, 0);
			if (fd >= 0) {
				message = strchr(tbuf, '>');
				if (message != NULL)
					message++;
				else
					message = tbuf;
				(void)write(fd, message,
				    cnt - (message - tbuf));
				(void)close(fd);
			}
			_exit(0);
		}
		while (waitpid(pid, NULL, NULL) == -1 && (errno == EINTR))
			;
	}
}

void
syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}

void
openlog(const char *ident, int logstat, int logfac)
{
	if (ident != NULL)
		LogTag = ident;
	LogStat = logstat;
	if (logfac != 0 && (logfac &~ LOG_FACMASK) == 0)
		LogFacility = logfac;

	if (LogFile == -1) {
		if (LogStat & LOG_NDELAY) {
			LogFile = open(_PATH_MESSAGES, O_WRONLY|O_APPEND);
			if (LogFile == -1)
				return;
			(void)fcntl(LogFile, F_SETFD, 1);
		}
	}
}

void
closelog(void)
{
	if (LogFile != -1)
		(void)close(LogFile);
	LogFile = -1;
}

/* setlogmask -- set the log mask level */
int
setlogmask(int pmask)
{
	int omask;

	omask = LogMask;
	if (pmask != 0)
		LogMask = pmask;
	return (omask);
}
