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

#pragma once

// EarlyCommandLine.h
//
// A couple of options have to be read before CommandLine.cpp's parser exists: the debug log's file
// name is chosen by a static constructor (preMainInitMemoryManager -> DEBUG_INIT), and the
// one-copy-at-a-time mutex is taken in WinMain long before TheGlobalData is around to hold a flag.
//
// Both read the *wide* command line on purpose.  WinMain tokenizes lpCmdLine in place and that
// buffer is the process's ANSI command line, so GetCommandLineA is truncated to the exe name and
// the first switch by the time anything asks.  GetCommandLineW is untouched.

#include <windows.h>
#include <wchar.h>
#include <wctype.h>

/** The text just past an option in a command line, or NULL if it does not carry it.  An option is
    only an option on a word boundary, so "-win" does not match inside "-window". */
inline const wchar_t *findCommandLineOptionIn( const wchar_t *cmdLine, const wchar_t *option )
{
	if (cmdLine == NULL)
		return NULL;

	const size_t len = wcslen( option );
	for (const wchar_t *at = cmdLine; *at != 0; ++at)
	{
		if ((at == cmdLine || iswspace( at[-1] )) &&
				_wcsnicmp( at, option, len ) == 0 &&
				(at[len] == 0 || iswspace( at[len] )))
			return at + len;
	}
	return NULL;
}

/** The word after an option, if there is one and it is not itself an option. */
inline bool findCommandLineValueIn( const wchar_t *cmdLine, const wchar_t *option, char *out, size_t outSize )
{
	const wchar_t *at = findCommandLineOptionIn( cmdLine, option );
	if (at == NULL || outSize == 0)
		return false;

	while (iswspace( *at ))
		++at;
	if (*at == 0 || *at == L'-')
		return false;

	size_t i = 0;
	while (*at != 0 && !iswspace( *at ) && i + 1 < outSize)
		out[i++] = (char)*at++;
	out[i] = 0;
	return i > 0;
}

inline const wchar_t *findEarlyCommandLineOption( const wchar_t *option )
{
	return findCommandLineOptionIn( GetCommandLineW(), option );
}

inline bool findEarlyCommandLineValue( const wchar_t *option, char *out, size_t outSize )
{
	return findCommandLineValueIn( GetCommandLineW(), option, out, outSize );
}
