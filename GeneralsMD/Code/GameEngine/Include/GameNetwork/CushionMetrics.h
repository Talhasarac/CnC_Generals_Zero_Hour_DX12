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

// FILE: CushionMetrics.h /////////////////////////////////////////////////////////////////////////
// Desc:   How many frames of margin we have left, and when to spend some slowing down.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CUSHION_METRICS_H_
#define _CUSHION_METRICS_H_

#include "Lib/BaseType.h"

/* The "cushion" is how many frames ahead of the simulation a command arrived: the margin between
	 the network and the frame that needs it.  When it runs low the machine deliberately slows its
	 own logic rate - the self-slug - so the rest of the room can catch up before anybody stalls
	 outright.  That decision is worth getting right in both directions: slug when there is no
	 margin left and the room stutters instead of stalling, slug when there is and every player
	 pays for nothing. */

/** The margin, in frames, between the frame a command is for and the frame we are on.  Never
	  negative: a command whose frame has already gone by is no margin at all, not a negative one. */
Int frameCushion( UnsignedInt executionFrame, UnsignedInt currentFrame );

/** Whether to slow our own logic rate.  A negative minimumCushion means no sample has been taken
	  yet, which is not the same as no margin - see the sentinel note in CushionMetrics.cpp. */
Bool shouldSelfSlug( Int minimumCushion, Int runAhead, UnsignedInt slackPercent );

#endif // _CUSHION_METRICS_H_
