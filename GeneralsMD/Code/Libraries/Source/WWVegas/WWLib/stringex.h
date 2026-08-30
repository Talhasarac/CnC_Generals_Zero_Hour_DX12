/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//----------------------------------------------------------------------------
// Bounded string copies.
//
// This codebase is full of strncpy/strcat into fixed buffers, and strncpy does
// not terminate when the source fills the destination - so half of those call
// sites truncate into an unterminated buffer and the next read runs off the
// end.  strlcpy/strlcat always terminate, and return the length the call
// *wanted* to write, so a caller can tell truncation happened.
//
// Names and semantics are the well known BSD ones, so a call site converted
// from strncpy/strcat reads the way anyone would expect it to.
//----------------------------------------------------------------------------

#pragma once

#include <stddef.h>
#include <string.h>

// Number of characters before the first zero character.
template<typename T> size_t strlen_t(const T *str)
{
	const T *begin = str;
	while (*str)
		++str;
	return static_cast<size_t>(str - begin);
}

// As strlen_t, but never looks past maxlen characters.
template<typename T> size_t strnlen_t(const T *str, size_t maxlen)
{
	const T *begin = str;
	const T *end = str + maxlen;
	while (str < end && *str)
		++str;
	return static_cast<size_t>(str - begin);
}

// Copies src into dst, at most dstsize-1 characters, and always terminates.
// Returns the length of src - so a return >= dstsize means it was truncated.
template<typename T> size_t strlcpy_t(T *dst, const T *src, size_t dstsize)
{
	const size_t srclen = strlen_t(src);
	if (dstsize != 0)
	{
		const size_t copylen = (srclen >= dstsize) ? dstsize - 1 : srclen;
		memcpy(dst, src, copylen * sizeof(T));
		dst[copylen] = T(0);
	}
	return srclen;								// length it tried to create
}

// Appends src to dst within dstsize, and always terminates.
// Returns the length dst+src would have had - a return >= dstsize means truncation.
template<typename T> size_t strlcat_t(T *dst, const T *src, size_t dstsize)
{
	const size_t dstlen = strnlen_t(dst, dstsize);
	const size_t srclen = strlen_t(src);
	if (dstlen == dstsize)
		return dstsize + srclen;				// no room at all, dst was not terminated

	size_t copylen = dstsize - dstlen - 1;
	if (copylen > srclen)
		copylen = srclen;
	if (copylen > 0)
		memcpy(dst + dstlen, src, copylen * sizeof(T));
	dst[dstlen + copylen] = T(0);
	return dstlen + srclen;						// length it tried to create
}

inline size_t strlcpy(char *dst, const char *src, size_t dstsize) { return strlcpy_t(dst, src, dstsize); }
inline size_t strlcat(char *dst, const char *src, size_t dstsize) { return strlcat_t(dst, src, dstsize); }
inline size_t wcslcpy(wchar_t *dst, const wchar_t *src, size_t dstsize) { return strlcpy_t(dst, src, dstsize); }
inline size_t wcslcat(wchar_t *dst, const wchar_t *src, size_t dstsize) { return strlcat_t(dst, src, dstsize); }
