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

// FILE: GameDataMatch.h //////////////////////////////////////////////////////////////////////////
// Do two machines have the same simulation inputs?
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GAME_DATA_MATCH_H_
#define _GAME_DATA_MATCH_H_

#include "Lib/BaseType.h"

/* A lockstep game only stays in step if both machines feed the same numbers into it.  Two CRCs say
	 whether they do: TheGlobalData->m_exeCRC over the executable, the version and the multiplayer
	 scripts, and TheGlobalData->m_iniCRC over the text of every INI file the simulation reads - as
	 resolved, so a loose file under Run/Data that shadows an archive is what gets CRCed, which is
	 exactly the case this fork has to catch (FileSystem::openFile asks TheLocalFileSystem first).
	 Both are exchanged when joining a game.  Comparing them is the last moment at which a data
	 mismatch is a refused join instead of a desynced match. */

enum GameDataMatchResult
{
	GAMEDATA_MATCHES = 0,			///< same executable, same INI set: the game may start
	GAMEDATA_EXE_DIFFERS,			///< different build, or different multiplayer scripts
	GAMEDATA_INI_DIFFERS,			///< same build, different INI set - the usual case, and the fixable one
	GAMEDATA_UNKNOWN,					///< the other machine did not tell us what it has
};

/** Compare what the other machine reports against what we have. */
GameDataMatchResult compareGameData( UnsignedInt theirExeCRC, UnsignedInt theirIniCRC,
																		 UnsignedInt ourExeCRC, UnsignedInt ourIniCRC );

/** A word for the log; never shown to the player. */
const char *gameDataMatchName( GameDataMatchResult result );

#endif // _GAME_DATA_MATCH_H_
