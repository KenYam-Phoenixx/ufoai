/**
 * @file shared.c
 * @brief Shared functions
 */

/*
All original material Copyright (C) 2002-2010 UFO: Alien Invasion.

Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../common/common.h"
#include "utf8.h"

/**
 * @brief Returns just the filename from a given path
 * @sa Com_StripExtension
 */
const char *Com_SkipPath (const char *pathname)
{
	char const* const last = strrchr(pathname, '/');
	return last ? last + 1 : pathname;
}

char *Com_Trim (char *s)
{
	char *left, *right;

	left = s;

	while (isspace(*left))
		left++;

	right = left + strlen(left) - 1;

	while (isspace(*right))
		*right-- = 0;

	return left;
}

/**
 * @brief Like Com_Filter, but match PATTERN against any final segment of TEXT.
 */
static int Com_FilterAfterStar (const char *pattern, const char *text)
{
	register const char *p = pattern, *t = text;
	register char c, c1;

	while ((c = *p++) == '?' || c == '*')
		if (c == '?' && *t++ == '\0')
			return 0;

	if (c == '\0')
		return 1;

	if (c == '\\')
		c1 = *p;
	else
		c1 = c;

	while (1) {
		if ((c == '[' || *t == c1) && Com_Filter(p - 1, t))
			return 1;
		if (*t++ == '\0')
			return 0;
	}
}

/**
 * @brief Match the pattern PATTERN against the string TEXT;
 * @return 1 if it matches, 0 otherwise.
 * @note A match means the entire string TEXT is used up in matching.
 * @note In the pattern string, `*' matches any sequence of characters,
 * `?' matches any character, [SET] matches any character in the specified set,
 * [!SET] matches any character not in the specified set.
 * @note A set is composed of characters or ranges; a range looks like
 * character hyphen character (as in 0-9 or A-Z).
 * [0-9a-zA-Z_] is the set of characters allowed in C identifiers.
 * Any other character in the pattern must be matched exactly.
 * @note To suppress the special syntactic significance of any of `[]*?!-\',
 * and match the character exactly, precede it with a `\'.
 */
int Com_Filter (const char *pattern, const char *text)
{
	register const char *p = pattern, *t = text;
	register char c;

	while ((c = *p++) != '\0')
		switch (c) {
		case '?':
			if (*t == '\0')
				return 0;
			else
				++t;
			break;

		case '\\':
			if (*p++ != *t++)
				return 0;
			break;

		case '*':
			return Com_FilterAfterStar(p, t);

		case '[':
			{
				register char c1 = *t++;
				int invert;

				if (!c1)
					return (0);

				invert = ((*p == '!') || (*p == '^'));
				if (invert)
					p++;

				c = *p++;
				while (1) {
					register char cstart = c, cend = c;

					if (c == '\\') {
						cstart = *p++;
						cend = cstart;
					}
					if (c == '\0')
						return 0;

					c = *p++;
					if (c == '-' && *p != ']') {
						cend = *p++;
						if (cend == '\\')
							cend = *p++;
						if (cend == '\0')
							return 0;
						c = *p++;
					}
					if (c1 >= cstart && c1 <= cend)
						goto match;
					if (c == ']')
						break;
				}
				if (!invert)
					return 0;
				break;

			  match:
				/* Skip the rest of the [...] construct that already matched. */
				while (c != ']') {
					if (c == '\0')
						return 0;
					c = *p++;
					if (c == '\0')
						return 0;
					else if (c == '\\')
						++p;
				}
				if (invert)
					return 0;
				break;
			}

		default:
			if (c != *t++)
				return 0;
		}

	return *t == '\0';
}

/**
 * @brief Replaces the filename from one path with another one
 * @param[in] fileName The full path to a filename
 * @param[in] name The filename to insert into the given full path
 * @param[out] path The target buffer
 * @param[in] size The size of the target buffer
 */
void Com_ReplaceFilename (const char *fileName, const char *name, char *path, size_t size)
{
	char *slash, *end;

	Q_strncpyz(path, fileName, size);

	end = path;
	while ((slash = strchr(end, '/')) != 0)
		end = slash + 1;

	strcpy(end, name + 1);
}

/**
 * @brief Removes the file extension from a filename
 * @sa Com_SkipPath
 * @param[in] in The incoming filename
 * @param[in] out The stripped filename
 * @param[in] size The size of the output buffer
 */
void Com_StripExtension (const char *in, char *out, const size_t size)
{
	char *out_ext = NULL;
	int i = 1;

	while (*in && i < size) {
		*out++ = *in++;
		i++;

		if (*in == '.')
			out_ext = out;
	}

	if (out_ext)
		*out_ext = 0;
	else
		*out = 0;
}

/**
 * @param path The path resp. filename to extract the extension from
 * @return @c NULL if the given path name does not contain an extension
 */
const char *Com_GetExtension (const char *path)
{
	const char *src = path + strlen(path) - 1;
	while (*src != '/' && src != path) {
		if (*src == '.')
			return src + 1;
		src--;
	}

	return NULL;
}

/**
 * @brief Sets a default extension if there is none
 */
void Com_DefaultExtension (char *path, size_t len, const char *extension)
{
	char oldPath[MAX_OSPATH];
	const char *src;

	/* if path doesn't have a .EXT, append extension
	 * (extension should include the .) */
	src = path + strlen(path) - 1;

	while (*src != '/' && src != path) {
		if (*src == '.')
			return;
		src--;
	}

	Q_strncpyz(oldPath, path, sizeof(oldPath));
	Com_sprintf(path, len, "%s%s", oldPath, extension);
}

/**
 * @brief Returns the path up to, but not including the last /
 */
void Com_FilePath (const char *in, char *out)
{
	const char *s = in + strlen(in) - 1;

	while (s != in && *s != '/')
		s--;

	Q_strncpyz(out, in, s - in + 1);
}

/**
 * @brief Compare two floats
 * @param[in] float1 The first float
 * @param[in] float2 The second float
 * @return An integer less than, equal to, or greater than zero if float1 is
 * found, respectively, to be less than,  to match, or be greater than float2
 * @note sort function pointer for qsort
 */
int Q_FloatSort (const void *float1, const void *float2)
{
	return (*(const float *)float1 - *(const float *)float2);
}

/**
 * @brief Compare two strings
 * @param[in] string1 The first string
 * @param[in] string2 The second string
 * @return An integer less than, equal to, or greater than zero if string1 is
 * found, respectively, to be less than,  to match, or be greater than string2
 * @note sort function pointer for qsort
 */
int Q_StringSort (const void *string1, const void *string2)
{
	const char *s1, *s2;
	s1 = string1;
	s2 = string2;
	if (*s1 < *s2)
		return -1;
	else if (*s1 == *s2) {
		while (*s1) {
			s1++;
			s2++;
			if (*s1 < *s2)
				return -1;
			if (*s1 > *s2)
				return 1;
		}
		return 0;
	} else
		return 1;
}

#define VA_BUFSIZE 4096
/**
 * @brief does a varargs printf into a temp buffer, so I don't need to have
 * varargs versions of all text functions.
 */
char *va (const char *format, ...)
{
	va_list argptr;
	/* in case va is called by nested functions */
	static char string[16][VA_BUFSIZE];
	static unsigned int index = 0;
	char *buf;

	buf = string[index & 0x0F];
	index++;

	va_start(argptr, format);
	Q_vsnprintf(buf, VA_BUFSIZE, format, argptr);
	va_end(argptr);

	return buf;
}

/*
============================================================================
LIBRARY REPLACEMENT FUNCTIONS
============================================================================
*/

char *Q_strlwr (char *str)
{
	char* origs = str;
	while (*str) {
		*str = tolower(*str);
		str++;
	}
	return origs;
}

#ifndef HAVE_STRNCASECMP
int Q_strncasecmp (const char *s1, const char *s2, size_t n)
{
	while (n-- != 0) {
		int c1 = *s1++;
		int c2 = *s2++;

		if (c1 != c2) {
			if ('a' <= c1 && c1 <= 'z')
				c1 += 'a' - 'A';
			if ('a' <= c2 && c2 <= 'z')
				c2 += 'a' - 'A';
			if (c1 != c2)
				return (unsigned char)c1 - (unsigned char)c2; /* strings not equal */
		}

		if (c1 == '\0')
			break;
	}

	return 0;					/* strings are equal */
}
#endif /* HAVE_STRNCASECMP */

/**
 * @brief Safe strncpy that ensures a trailing zero
 * @param dest Destination pointer
 * @param src Source pointer
 * @param destsize Size of destination buffer (this should be a sizeof size due to portability)
 */
#ifdef DEBUG
void Q_strncpyzDebug (char *dest, const char *src, size_t destsize, const char *file, int line)
#else
void Q_strncpyz (char *dest, const char *src, size_t destsize)
#endif
{
#ifdef DEBUG
	if (!dest)
		Sys_Error("Q_strncpyz: NULL dest (%s, %i)", file, line);
	if (!src)
		Sys_Error("Q_strncpyz: NULL src (%s, %i)", file, line);
	if (destsize < 1)
		Sys_Error("Q_strncpyz: destsize < 1 (%s, %i)", file, line);
#endif
	UTF8_strncpyz(dest, src, destsize);
}

/**
 * @brief Safely (without overflowing the destination buffer) concatenates two strings.
 * @param[in] dest the destination string.
 * @param[in] src the source string.
 * @param[in] destsize the total size of the destination buffer.
 * @return pointer destination string.
 * never goes past bounds or leaves without a terminating 0
 */
void Q_strcat (char *dest, const char *src, size_t destsize)
{
	size_t dest_length;
	dest_length = strlen(dest);
	if (dest_length >= destsize)
		Sys_Error("Q_strcat: already overflowed");
	Q_strncpyz(dest + dest_length, src, destsize - dest_length);
}

/**
 * @brief copies formatted string with buffer-size checking
 * @param[out] dest Destination buffer
 * @param[in] size Size of the destination buffer
 * @param[in] fmt Stringformat (like printf)
 * @return false if overflowed - true otherwise
 */
qboolean Com_sprintf (char *dest, size_t size, const char *fmt, ...)
{
	va_list ap;
	int len;

	if (!fmt)
		return qfalse;

	va_start(ap, fmt);
	len = Q_vsnprintf(dest, size, fmt, ap);
	va_end(ap);

	if (len <= size - 1)
		return qtrue;

	/* number of char */
	len = size - 1;

	/* check for UTF8 multibyte sequences */
	if (len > 0 && (unsigned char) dest[len - 1] >= 0x80) {
		int i = len - 1;
		while ((i > 0) && ((unsigned char) dest[i] & 0xc0) == 0x80)
			i--;
		if (UTF8_char_len(dest[i]) + i > len) {
			dest[i] = '\0';
		}
#ifdef DEBUG
		else {
			/* the '\0' is already at the right place */
			len = i + UTF8_char_len(dest[i]);
			assert(dest[len] == '\0');
		}
#endif
	}

	return qfalse;
}

/**
 * @brief Safe (null terminating) vsnprintf implementation
 */
int Q_vsnprintf (char *str, size_t size, const char *format, va_list ap)
{
	int len;

#if defined(_WIN32)
	len = _vsnprintf(str, size, format, ap);
	str[size - 1] = '\0';
#ifdef DEBUG
	if (len == -1)
		Com_Printf("Q_vsnprintf: string (%.32s...) was truncated (%i) - target buffer too small ("UFO_SIZE_T")\n", str, len, size);
#endif
#else
	len = vsnprintf(str, size, format, ap);
#ifdef DEBUG
	if ((size_t)len >= size)
		Com_Printf("Q_vsnprintf: string (%.32s...) was truncated (%i) - target buffer too small ("UFO_SIZE_T")\n", str, len, size);
#endif
#endif

	return len;
}

const char *Q_stristr (const char *str, const char *substr)
{
	const size_t sublen = strlen(substr);
	while (*str) {
		if (!Q_strncasecmp(str, substr, sublen))
			break;
		str++;
	}
	if (!(*str))
		str = NULL;
	return str;
}
