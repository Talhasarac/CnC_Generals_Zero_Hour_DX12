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

// FILE: GameDataMatch.cpp ////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameNetwork/GameDataMatch.h"

GameDataMatchResult compareGameData( UnsignedInt theirExeCRC, UnsignedInt theirIniCRC,
																		 UnsignedInt ourExeCRC, UnsignedInt ourIniCRC )
{
	/* Zero is not a CRC anybody computes - the executable CRC always folds in the version number,
		 and the INI CRC always folds in several megabytes of text.  A machine that reports zero has
		 not checked its data at all, which is not something to start a lockstep game on. */
	if( theirExeCRC == 0 || theirIniCRC == 0 )
		return GAMEDATA_UNKNOWN;

	/* Report the executable first when both differ: a different build reads the same INI files into
		 different code, so its INI CRC agreeing would prove nothing and its disagreeing explains
		 nothing.  The INI difference is the one a player can actually go and fix. */
	if( theirExeCRC != ourExeCRC )
		return GAMEDATA_EXE_DIFFERS;

	if( theirIniCRC != ourIniCRC )
		return GAMEDATA_INI_DIFFERS;

	return GAMEDATA_MATCHES;
}

const char *gameDataMatchName( GameDataMatchResult result )
{
	switch( result )
	{
		case GAMEDATA_MATCHES:			return "same data";
		case GAMEDATA_EXE_DIFFERS:	return "different executable or multiplayer scripts";
		case GAMEDATA_INI_DIFFERS:	return "different INI set";
		case GAMEDATA_UNKNOWN:			return "data not reported";
	}
	return "unknown result";
}
