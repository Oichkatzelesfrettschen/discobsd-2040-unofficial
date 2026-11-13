/*
 * Parser and restart policy for init's daemon table; daemons.h describes
 * the /etc/daemons format and the policies.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>

#include "daemons.h"

#define	ISBLANK(c)	((c) == ' ' || (c) == '\t')

/* A field holds no blanks; returns its length and sets *start. */
static size_t
find_last_field(const char *line, size_t end, size_t *start)
{
	size_t i = end;

	while (i > 0 && !ISBLANK(line[i - 1]))
		i--;
	*start = i;
	return end - i;
}

static size_t
skip_blanks_back(const char *line, size_t end)
{
	while (end > 0 && ISBLANK(line[end - 1]))
		end--;
	return end;
}

/* Decimal 0..65535, the full field; returns -1 otherwise. */
static long
parse_id(const char *s, size_t len)
{
	long value = 0;
	size_t i;

	if (len == 0 || len > 5)
		return (-1);
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9')
			return (-1);
		value = value * 10 + (s[i] - '0');
	}
	return (value > 65535 ? -1 : value);
}

int
dtab_parse(const char *line, struct dtab *d)
{
	size_t end, start, len, colon, i, out;
	long uid, gid;
	int blank, words;

	end = strlen(line);
	while (end > 0 && (line[end - 1] == '\n' || line[end - 1] == '\r'))
		end--;
	for (i = 0; i < end && ISBLANK(line[i]); i++)
		;
	if (i == end || line[i] == '#')
		return (0);

	memset(d, 0, sizeof(*d));

	/* policy */
	end = skip_blanks_back(line, end);
	len = find_last_field(line, end, &start);
	if (len == 7 && strncmp(line + start, "backoff", 7) == 0)
		d->policy = DPOLICY_BACKOFF;
	else if (len == 6 && strncmp(line + start, "capped", 6) == 0)
		d->policy = DPOLICY_CAPPED;
	else
		return (-1);

	/* chroot */
	end = skip_blanks_back(line, start);
	len = find_last_field(line, end, &start);
	if (len == 1 && line[start] == '-')
		d->root[0] = '\0';
	else if (len > 0 && len < DROOTSIZ && line[start] == '/') {
		memcpy(d->root, line + start, len);
		d->root[len] = '\0';
	} else
		return (-1);

	/* uid:gid */
	end = skip_blanks_back(line, start);
	len = find_last_field(line, end, &start);
	for (colon = 0; colon < len && line[start + colon] != ':'; colon++)
		;
	if (colon == len)
		return (-1);
	uid = parse_id(line + start, colon);
	gid = parse_id(line + start + colon + 1, len - colon - 1);
	if (uid < 0 || gid < 0)
		return (-1);
	d->uid = (uid_t)uid;
	d->gid = (gid_t)gid;

	/* command: what precedes uid:gid, blanks folded to one space */
	end = skip_blanks_back(line, start);
	if (end == start || line[i] != '/')
		return (-1);
	out = 0;
	blank = 0;
	words = 1;
	for (; i < end; i++) {
		if (ISBLANK(line[i])) {
			blank = 1;
			continue;
		}
		if (out + blank + 1 >= DCMDSIZ)
			return (-1);
		/* dtab_argv() passes at most DNARGS - 1 words to execve(). */
		if (blank && ++words > DNARGS - 1)
			return (-1);
		if (blank)
			d->cmd[out++] = ' ';
		blank = 0;
		d->cmd[out++] = line[i];
	}
	d->cmd[out] = '\0';
	return (1);
}

int
dtab_same(const struct dtab *a, const struct dtab *b)
{
	return (strcmp(a->cmd, b->cmd) == 0 && strcmp(a->root, b->root) == 0 &&
	    a->uid == b->uid && a->gid == b->gid && a->policy == b->policy);
}

int
dtab_argv(char *buf, char **argv)
{
	int argc = 0;

	while (*buf != '\0' && argc < DNARGS - 1) {
		argv[argc++] = buf;
		while (*buf != '\0' && *buf != ' ')
			buf++;
		if (*buf == ' ')
			*buf++ = '\0';
	}
	argv[argc] = (char *)0;
	return (argc);
}

int
dtab_exited(struct dtab *d, time_t now)
{
	int delay, i;

	d->pid = -1;
	if (now - d->started >= DHEALTHY_RUN)
		d->fails = 0;
	d->fails++;
	if (d->policy == DPOLICY_CAPPED) {
		if (d->fails > DCAPPED_TRIES) {
			d->disabled = 1;
			d->pid = 0;
			return (-1);
		}
		delay = DCAPPED_DELAY;
	} else {
		delay = 1;
		for (i = 1; i < d->fails && delay < DBACKOFF_MAX; i++)
			delay *= 2;
		if (delay > DBACKOFF_MAX)
			delay = DBACKOFF_MAX;
	}
	d->next_try = now + delay;
	return (delay);
}

void
dtab_describe(char *buf, size_t len, const struct dtab *d, int status,
    int delay)
{
	char what[24], next[32];

	if (status == -1)
		snprintf(what, sizeof(what), "cannot fork");
	else if (WIFSIGNALED(status))
		snprintf(what, sizeof(what), "killed by signal %d",
		    (int)WTERMSIG(status));
	else
		snprintf(what, sizeof(what), "exited %d",
		    (int)WEXITSTATUS(status));
	if (delay < 0)
		snprintf(next, sizeof(next), "disabled until SIGHUP");
	else
		snprintf(next, sizeof(next), "restart in %d s", delay);
	snprintf(buf, len, "init: %s: %s; %s\n", d->cmd, what, next);
}
