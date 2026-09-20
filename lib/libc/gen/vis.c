/*-
 * Copyright (c) 1989, 1993
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)vis.c	8.1 (Berkeley) 7/19/93";
#endif /* LIBC_SCCS and not lint */

#include <errno.h>
#include <vis.h>

#define MAX_VIS_BYTES 4

static size_t
encode_byte(char encoded[MAX_VIS_BYTES], unsigned char character, int flags,
    unsigned char next_character)
{
    char escape_character = '\0';
    char *output = encoded;

    if ((character >= '!' && character <= '~') ||
        ((flags & VIS_SP) == 0 && character == ' ') ||
        ((flags & VIS_TAB) == 0 && character == '\t') ||
        ((flags & VIS_NL) == 0 && character == '\n') ||
        ((flags & VIS_SAFE) != 0 &&
        (character == '\b' || character == '\a' || character == '\r'))) {
        *output++ = (char)character;
        if (character == '\\' && (flags & VIS_NOSLASH) == 0)
            *output++ = '\\';
        return (size_t)(output - encoded);
    }

    if ((flags & VIS_CSTYLE) != 0) {
        if (character >= '\a' && character <= '\r')
            escape_character = "abtnvfr"[character - '\a'];
        else if (character == ' ')
            escape_character = 's';
        if (escape_character != '\0') {
            *output++ = '\\';
            *output++ = escape_character;
            return (size_t)(output - encoded);
        }
        if (character == '\0') {
            *output++ = '\\';
            *output++ = '0';
            if (next_character >= '0' && next_character <= '7') {
                *output++ = '0';
                *output++ = '0';
            }
            return (size_t)(output - encoded);
        }
    }

    if ((character & 0177) == ' ' || (flags & VIS_OCTAL) != 0) {
        *output++ = '\\';
        *output++ = (char)((character >> 6 & 07) + '0');
        *output++ = (char)((character >> 3 & 07) + '0');
        *output++ = (char)((character & 07) + '0');
        return (size_t)(output - encoded);
    }
    if ((flags & VIS_NOSLASH) == 0)
        *output++ = '\\';
    if ((character & 0200) != 0) {
        character &= 0177;
        *output++ = 'M';
    }
    if (character < ' ' || character == 0177) {
        *output++ = '^';
        *output++ = character == 0177 ? '?' : (char)(character + '@');
    } else {
        *output++ = '-';
        *output++ = (char)character;
    }
    return (size_t)(output - encoded);
}

static char *
encode_character(char *destination, size_t destination_length, int character,
    int flags, int next_character)
{
    char encoded[MAX_VIS_BYTES];
    size_t encoded_length;
    size_t output_index;

    encoded_length = encode_byte(encoded, (unsigned char)character, flags,
        (unsigned char)next_character);
    if (encoded_length >= destination_length) {
        errno = ENOSPC;
        return 0;
    }
    for (output_index = 0; output_index < encoded_length; ++output_index)
        destination[output_index] = encoded[output_index];
    destination[encoded_length] = '\0';
    return destination + encoded_length;
}

char *
vis(char *destination, int character, int flags, int next_character)
{
    size_t encoded_length;

    encoded_length = encode_byte(destination, (unsigned char)character, flags,
        (unsigned char)next_character);
    destination[encoded_length] = '\0';
    return destination + encoded_length;
}

char *
nvis(char *destination, size_t destination_length, int character, int flags,
    int next_character)
{
    return encode_character(destination, destination_length, character, flags,
        next_character);
}

/*
 * The bounded contract leaves the destination unchanged on ENOSPC, so the
 * first pass proves every encoded atom and the terminator fit before the
 * second pass commits the result.
 */
static int
encode_string(char *destination, size_t destination_length,
    const unsigned char *source, size_t source_length, int flags)
{
    char encoded[MAX_VIS_BYTES];
    size_t encoded_length;
    size_t input_index;
    size_t output_index = 0;

    if (destination_length == 0)
        goto no_space;
    for (input_index = 0; input_index < source_length; ++input_index) {
        encoded_length = encode_byte(encoded, source[input_index], flags,
            input_index + 1 < source_length ? source[input_index + 1] : '\0');
        if (encoded_length >= destination_length - output_index)
            goto no_space;
        output_index += encoded_length;
    }

    output_index = 0;
    for (input_index = 0; input_index < source_length; ++input_index) {
        output_index += encode_byte(destination + output_index,
            source[input_index], flags,
            input_index + 1 < source_length ? source[input_index + 1] : '\0');
    }
    destination[output_index] = '\0';
    return (int)output_index;

no_space:
    errno = ENOSPC;
    return -1;
}

static size_t
string_length(const char *string)
{
    const char *end = string;

    while (*end != '\0')
        ++end;
    return (size_t)(end - string);
}

int
strvis(char *destination, const char *source, int flags)
{
    return encode_string(destination, (size_t)-1,
        (const unsigned char *)source, string_length(source), flags);
}

int
strnvis(char *destination, size_t destination_length, const char *source,
    int flags)
{
    return encode_string(destination, destination_length,
        (const unsigned char *)source, string_length(source), flags);
}

int
strvisx(char *destination, const char *source, size_t source_length, int flags)
{
    return encode_string(destination, (size_t)-1,
        (const unsigned char *)source, source_length, flags);
}

int
strnvisx(char *destination, size_t destination_length, const char *source,
    size_t source_length, int flags)
{
    return encode_string(destination, destination_length,
        (const unsigned char *)source, source_length, flags);
}
