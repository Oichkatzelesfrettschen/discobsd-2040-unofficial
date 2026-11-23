/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdio.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>

struct variable {
	const char *name;
	int selector;
};

static const struct variable variables[] = {
	{ "ARG_MAX", _SC_ARG_MAX },
	{ "CHILD_MAX", _SC_CHILD_MAX },
	{ "NGROUPS_MAX", _SC_NGROUPS_MAX },
	{ "OPEN_MAX", _SC_OPEN_MAX },
	{ "_POSIX_JOB_CONTROL", _SC_JOB_CONTROL },
	{ "NPROCESSORS_CONF", _SC_NPROCESSORS_CONF },
	{ "NPROCESSORS_ONLN", _SC_NPROCESSORS_ONLN },
	{ "PAGESIZE", _SC_PAGESIZE },
	{ "PAGE_SIZE", _SC_PAGESIZE },
	{ "_POSIX_DEVICE_CONTROL", _SC_DEVICE_CONTROL },
	{ "SYMLOOP_MAX", _SC_SYMLOOP_MAX }
};

static const struct variable path_variables[] = {
	{ "NAME_MAX", _PC_NAME_MAX },
	{ "LINK_MAX", _PC_LINK_MAX },
	{ "PIPE_BUF", _PC_PIPE_BUF },
	{ "PATH_MAX", _PC_PATH_MAX },
	{ "_POSIX_NO_TRUNC", _PC_NO_TRUNC },
	{ "_POSIX_CHOWN_RESTRICTED", _PC_CHOWN_RESTRICTED },
	{ "POSIX2_SYMLINKS", _PC_2_SYMLINKS },
	{ "POSIX_ALLOC_SIZE_MIN", _PC_ALLOC_SIZE_MIN },
	{ "_POSIX_TIMESTAMP_RESOLUTION", _PC_TIMESTAMP_RESOLUTION },
};

static int
print_path(void)
{
	char path[sizeof(_PATH_STDPATH)];
	size_t length;

	errno = 0;
	length = confstr(_CS_PATH, path, sizeof(path));
	if (length == 0) {
		if (errno == 0)
			return puts("undefined");
		return -1;
	}
	if (length > sizeof(path)) {
		errno = ERANGE;
		return -1;
	}
	return puts(path);
}

int
main(int argc, char **argv)
{
	const char *name;
	size_t i;
	long value;
	int result, path_selector = 0;

	if (argc > 1 && strcmp(argv[1], "--") == 0) {
		argc--;
		argv++;
	}
	if (argc < 2 || argc > 3) {
		fputs("usage: getconf system_var | getconf path_var pathname\n", stderr);
		return 1;
	}
	name = argv[1];
	for (i = 0; i < sizeof(path_variables) / sizeof(path_variables[0]); i++)
		if (strcmp(name, path_variables[i].name) == 0) {
			path_selector = path_variables[i].selector;
			break;
		}
	if (argc == 3 && path_selector) {
		errno = 0;
		value = pathconf(argv[2], path_selector);
	} else if (argc == 3 || path_selector) {
		fprintf(stderr, "getconf: invalid operand count for %s\n", name);
		return 1;
	} else if (strcmp(name, "PATH") == 0) {
		result = print_path();
		goto output;
	} else {
		for (i = 0; i < sizeof(variables) / sizeof(variables[0]); i++)
			if (strcmp(name, variables[i].name) == 0)
				break;
		if (i == sizeof(variables) / sizeof(variables[0])) {
			fprintf(stderr, "getconf: unknown variable: %s\n", name);
			return 1;
		}
		errno = 0;
		value = sysconf(variables[i].selector);
	}
	if (value != -1)
		result = printf("%ld\n", value);
	else if (errno == 0)
		result = puts("undefined");
	else
		result = -1;
output:
	if (result < 0 || fflush(stdout) == EOF || ferror(stdout)) {
		perror("getconf");
		return 1;
	}
	return 0;
}
