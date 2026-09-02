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

// EarlyOptions.h
//
// Options.ini, read before the engine exists.  EarlyCommandLine.h's twin.
//
// Almost every setting can wait for TheGlobalData, and should.  One cannot: the window style is
// fixed by CreateWindow in WinMain, which runs before the memory manager, before the file system
// and long before anything has parsed a preferences file.  A window born with a caption cannot
// lose it later without flickering through a restyle, and borderless has to be born without one.
//
// So this reads the file the hard way - the user data directory out of the shell and the registry,
// then fopen and fgets - and answers one key at a time.  It is deliberately not a cache and not a
// parser: three lookups reopen the file three times, which costs microseconds once per process.
//
// The format is UserPreferences': "key = value", one per line, '=' separating, whitespace ignored
// around both halves.  A file the engine writes is always in that shape, so the two agree without
// sharing code.

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The directory the game keeps Options.ini, replays and save games in.
	*
	* GlobalData works this out the same way at startup (Documents plus a leaf name the installer
	* writes into the registry), but it does it far too late to help WinMain.  Both defaults have to
	* match GlobalData.cpp's or the two read different files. */
inline bool findUserDataDirectory( char *out, size_t outSize )
{
	if (outSize == 0)
		return false;
	out[0] = 0;

	char documents[MAX_PATH];
	if (!::SHGetSpecialFolderPathA( NULL, documents, CSIDL_PERSONAL, TRUE ))
		return false;

	char leaf[MAX_PATH] = "Command and Conquer Generals Zero Hour Data";

	// HKLM first, then HKCU: the same two hives in the same order that GetStringFromRegistry walks.
	// A localized install renames the folder here, and reading only one hive would send this at the
	// English folder while the engine writes to the translated one.
	const HKEY hives[2] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for (int hive = 0; hive < 2; ++hive)
	{
		HKEY key;
		if (::RegOpenKeyExA( hives[hive],
				"SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour",
				0, KEY_READ, &key ) != ERROR_SUCCESS)
			continue;

		DWORD type = 0;
		char value[MAX_PATH];
		DWORD size = sizeof( value );
		const bool got = ::RegQueryValueExA( key, "UserDataLeafName", NULL, &type, (LPBYTE)value, &size ) == ERROR_SUCCESS
										&& type == REG_SZ && size > 1;
		::RegCloseKey( key );

		if (got)
		{
			value[(size < sizeof( value )) ? size : sizeof( value ) - 1] = 0;
			::strncpy( leaf, value, sizeof( leaf ) - 1 );
			leaf[sizeof( leaf ) - 1] = 0;
			break;
		}
	}

	if (::_snprintf( out, outSize, "%s\\%s\\", documents, leaf ) < 0)
	{
		out[0] = 0;
		return false;
	}
	out[outSize - 1] = 0;
	return true;
}

/** The value stored under this key in an already-open preferences file.
	*
	* Split out from findEarlyOptionValue so the parsing can be tested against a file the test wrote
	* itself.  The real one reads the player's Options.ini, which a test has no business opening. */
inline bool findEarlyOptionValueIn( FILE *fp, const char *key, char *out, size_t outSize )
{
	if (outSize == 0)
		return false;
	out[0] = 0;

	const size_t keyLen = ::strlen( key );
	bool found = false;
	char line[1024];
	while (::fgets( line, sizeof( line ), fp ) != NULL)
	{
		const char *at = line;
		while (*at == ' ' || *at == '\t')
			++at;

		if (::_strnicmp( at, key, keyLen ) != 0)
			continue;

		const char *after = at + keyLen;
		while (*after == ' ' || *after == '\t')
			++after;
		if (*after != '=')
			continue;	// a longer key that merely starts the same way

		++after;
		while (*after == ' ' || *after == '\t')
			++after;

		size_t i = 0;
		while (*after != 0 && *after != '\r' && *after != '\n' && i + 1 < outSize)
			out[i++] = *after++;
		while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
			--i;
		out[i] = 0;

		found = (i > 0);
		// keep reading: UserPreferences::write dumps a std::map, so a duplicated key can only come
		// from a hand-edited file, and the last one wins is the rule the engine's own loader follows
	}

	return found;
}

/** The value stored under this key in Options.ini, or false if the file or the key is not there. */
inline bool findEarlyOptionValue( const char *key, char *out, size_t outSize )
{
	if (outSize == 0)
		return false;
	out[0] = 0;

	char path[MAX_PATH];
	if (!findUserDataDirectory( path, sizeof( path ) ))
		return false;
	::strncat( path, "Options.ini", sizeof( path ) - ::strlen( path ) - 1 );

	FILE *fp = ::fopen( path, "r" );
	if (fp == NULL)
		return false;	// no preferences file yet, which is the state every fresh install is in

	const bool found = findEarlyOptionValueIn( fp, key, out, outSize );
	::fclose( fp );
	return found;
}

/** The value stored under this key, as a whole number, clamped to [lo,hi]. */
inline int getEarlyOptionInt( const char *key, int defaultValue, int lo, int hi )
{
	char value[64];
	if (!findEarlyOptionValue( key, value, sizeof( value ) ))
		return defaultValue;

	const int parsed = ::atoi( value );
	if (parsed < lo)
		return lo;
	if (parsed > hi)
		return hi;
	return parsed;
}
