/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 * The Berkeley software License Agreement specifies the terms and
 * conditions for redistribution.
 */
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * The pointer vector and its ownership bytes share one allocation.  Strings
 * inherited from crt0 or supplied through putenv() remain borrowed; setenv()
 * strings belong to libc.  Publishing a replacement vector only after it is
 * complete keeps allocation failures from damaging the active environment.
 */
static char **managed_environ;
static unsigned char *managed_owned;
static size_t managed_capacity;

static size_t
environment_count(char **environment)
{
	size_t count;

	if (environment == NULL)
		return 0;
	for (count = 0; environment[count] != NULL; ++count)
		;
	return count;
}

static int
environment_name_length(const char *name, size_t *length)
{
	const char *cursor;

	if (name == NULL || *name == '\0') {
		errno = EINVAL;
		return -1;
	}
	for (cursor = name; *cursor != '\0'; ++cursor) {
		if (*cursor == '=') {
			errno = EINVAL;
			return -1;
		}
	}
	*length = (size_t)(cursor - name);
	return 0;
}

static int
environment_find(const char *name, size_t name_length, size_t *offset)
{
	size_t index;

	if (environ == NULL)
		return 0;
	for (index = 0; environ[index] != NULL; ++index) {
		if (strncmp(environ[index], name, name_length) == 0 &&
		    environ[index][name_length] == '=') {
			*offset = index;
			return 1;
		}
	}
	return 0;
}

static int
environment_manage(size_t required_capacity)
{
	char **new_environ;
	unsigned char *new_owned;
	char **old_managed_environ;
	size_t current_count;
	size_t allocation_size;
	size_t index;

	current_count = environment_count(environ);
	if (required_capacity < current_count)
		required_capacity = current_count;
	if (environ == managed_environ &&
	    required_capacity <= managed_capacity)
		return 0;
	if (required_capacity >
	    (((size_t)-1 - sizeof(char *)) / (sizeof(char *) + 1))) {
		errno = ENOMEM;
		return -1;
	}
	allocation_size = sizeof(char *) +
	    required_capacity * (sizeof(char *) + 1);
	new_environ = (char **)malloc(allocation_size);
	if (new_environ == NULL)
		return -1;
	new_owned = (unsigned char *)(new_environ + required_capacity + 1);
	for (index = 0; index < current_count; ++index)
		new_environ[index] = environ[index];
	new_environ[current_count] = NULL;
	memset(new_owned, 0, required_capacity);

	old_managed_environ = managed_environ;
	if (environ == old_managed_environ) {
		memcpy(new_owned, managed_owned, current_count);
		managed_environ = new_environ;
		managed_owned = new_owned;
		managed_capacity = required_capacity;
		environ = new_environ;
		free(old_managed_environ);
	} else {
		/*
		 * An application may publish a vector containing pointers into the
		 * prior managed environment.  Preserve the abandoned allocation
		 * because libc cannot prove that those aliases have disappeared.
		 */
		managed_environ = new_environ;
		managed_owned = new_owned;
		managed_capacity = required_capacity;
		environ = new_environ;
	}
	return 0;
}

int
setenv(const char *name, const char *value, int rewrite)
{
	char *new_entry;
	char *old_entry;
	size_t current_count;
	size_t name_length;
	size_t value_length;
	size_t offset;
	size_t entry_size;
	int found;

	if (environment_name_length(name, &name_length) < 0)
		return -1;
	if (value == NULL) {
		errno = EINVAL;
		return -1;
	}
	found = environment_find(name, name_length, &offset);
	if (found && !rewrite)
		return 0;
	value_length = strlen(value);
	if (name_length > (size_t)-1 - 2 ||
	    value_length > (size_t)-1 - name_length - 2) {
		errno = ENOMEM;
		return -1;
	}
	entry_size = name_length + value_length + 2;
	new_entry = (char *)malloc(entry_size);
	if (new_entry == NULL)
		return -1;
	memcpy(new_entry, name, name_length);
	new_entry[name_length] = '=';
	memcpy(new_entry + name_length + 1, value, value_length + 1);

	current_count = environment_count(environ);
	if (environment_manage(found ? current_count : current_count + 1) < 0) {
		free(new_entry);
		return -1;
	}
	if (!found) {
		offset = current_count;
		environ[offset + 1] = NULL;
	} else {
		old_entry = environ[offset];
		if (managed_owned[offset])
			free(old_entry);
	}
	environ[offset] = new_entry;
	managed_owned[offset] = 1;
	return 0;
}

int
putenv(char *string)
{
	char *equals;
	char *old_entry;
	size_t current_count;
	size_t name_length;
	size_t offset;
	int found;

	if (string == NULL || string[0] == '\0' ||
	    (equals = strchr(string, '=')) == NULL || equals == string) {
		errno = EINVAL;
		return -1;
	}
	name_length = (size_t)(equals - string);
	found = environment_find(string, name_length, &offset);
	current_count = environment_count(environ);
	if (environment_manage(found ? current_count : current_count + 1) < 0)
		return -1;
	if (!found) {
		offset = current_count;
		environ[offset + 1] = NULL;
	} else {
		old_entry = environ[offset];
		if (managed_owned[offset] && old_entry != string)
			free(old_entry);
	}
	environ[offset] = string;
	managed_owned[offset] = 0;
	return 0;
}

int
unsetenv(const char *name)
{
	char *old_entry;
	size_t current_count;
	size_t name_length;
	size_t offset;
	size_t index;

	if (environment_name_length(name, &name_length) < 0)
		return -1;
	if (!environment_find(name, name_length, &offset))
		return 0;
	current_count = environment_count(environ);
	if (environment_manage(current_count) < 0)
		return -1;
	do {
		old_entry = environ[offset];
		if (managed_owned[offset])
			free(old_entry);
		for (index = offset; index < current_count; ++index) {
			environ[index] = environ[index + 1];
			if (index + 1 < current_count)
				managed_owned[index] = managed_owned[index + 1];
		}
		--current_count;
		managed_owned[current_count] = 0;
	} while (environment_find(name, name_length, &offset));
	return 0;
}
