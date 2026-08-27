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

// FILE: StallJudgement.h /////////////////////////////////////////////////////////////////////////
// Desc:   Telling a slow game apart from a broken one.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _STALL_JUDGEMENT_H_
#define _STALL_JUDGEMENT_H_

#include "Lib/BaseType.h"

/* The game stops on a frame it has not got everyone's commands for.  That is normal - it happens
	 for a moment on any link with jitter - and the disconnect screen is the opposite of harmless:
	 it takes over the display, starts a vote and can end with somebody dropped.  So it must not be
	 the answer to "we waited a while".

	 Two different things look identical from inside the logic loop:

	   - a player has gone away.  Nothing arrives from them: no commands, no acks, no keep-alives.
	   - the game is merely slow.  Packets from everyone keep arriving; the frame we need has not
	     been assembled yet because somebody's machine, or the link, is behind.

	 The first needs the disconnect screen.  The second needs to be left alone; showing the screen
	 there converts a two-second hitch into an interruption and a wrongful vote.  Telling them apart
	 needs one more number than EA used: how long it has been since anything at all arrived from the
	 quietest player. */

enum StallVerdict
{
	STALL_RUNNING = 0,		///< not stalled long enough to care
	STALL_WAITING,				///< stalled, but everyone is still talking to us: slow, not broken
	STALL_SILENT,					///< stalled, and somebody has stopped talking: this is a disconnect
	STALL_WEDGED,					///< stalled far past any plausible hitch, whoever is still talking
};

/**
	* Decide what a stall means.
	*
	* @param stallMS						how long the logic frame has failed to advance
	* @param worstSilenceMS			the longest any connected player has gone without sending us anything
	* @param disconnectMS				how long a stall may last before it is worth explaining (the
	*													NetworkDisconnectTime knob)
	* @param silenceMS					how long a player may say nothing before we call them gone
	* @param wedgedMS						the ceiling: past this the screen comes up regardless, because a
	*													game this far behind will not catch up on its own
	*/
StallVerdict judgeStall( UnsignedInt stallMS, UnsignedInt worstSilenceMS,
												 UnsignedInt disconnectMS, UnsignedInt silenceMS, UnsignedInt wedgedMS );

/** TRUE for the two verdicts that warrant taking over the screen. */
Bool stallNeedsDisconnectScreen( StallVerdict verdict );

const char *stallVerdictName( StallVerdict verdict );

#endif // _STALL_JUDGEMENT_H_
