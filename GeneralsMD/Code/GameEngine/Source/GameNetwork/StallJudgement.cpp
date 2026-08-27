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

// FILE: StallJudgement.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   Telling a slow game apart from a broken one.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameNetwork/StallJudgement.h"

/* No engine dependencies on purpose: this is the whole decision, and a test can drive it through
	 every case in a millisecond instead of waiting out real stalls on a real network. */

StallVerdict judgeStall( UnsignedInt stallMS, UnsignedInt worstSilenceMS,
												 UnsignedInt disconnectMS, UnsignedInt silenceMS, UnsignedInt wedgedMS )
{
	/* Short stalls are the normal state of a lockstep game and get no opinion at all. */
	if( stallMS <= disconnectMS )
		return STALL_RUNNING;

	/* Somebody has stopped sending.  This is the case the disconnect screen was written for, and
		 the sooner it comes up the sooner the remaining players can vote and carry on. */
	if( worstSilenceMS >= silenceMS )
		return STALL_SILENT;

	/* Everyone is still talking to us, so nobody has dropped - but a stall this long is not going
		 to resolve itself either, and the player deserves to be told rather than left staring at a
		 frozen battlefield. */
	if( stallMS >= wedgedMS )
		return STALL_WEDGED;

	/* Stalled, and every player is still sending.  Slow, not broken: wait. */
	return STALL_WAITING;
}

Bool stallNeedsDisconnectScreen( StallVerdict verdict )
{
	return verdict == STALL_SILENT || verdict == STALL_WEDGED;
}

const char *stallVerdictName( StallVerdict verdict )
{
	switch( verdict )
	{
		case STALL_RUNNING:	return "running";
		case STALL_WAITING:	return "slow, everyone still sending";
		case STALL_SILENT:	return "a player has gone quiet";
		case STALL_WEDGED:	return "stalled past any plausible hitch";
	}
	return "unknown";
}
