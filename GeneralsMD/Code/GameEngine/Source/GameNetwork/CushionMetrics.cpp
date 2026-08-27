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

// FILE: CushionMetrics.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   How many frames of margin we have left, and when to spend some slowing down.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameNetwork/CushionMetrics.h"

Int frameCushion( UnsignedInt executionFrame, UnsignedInt currentFrame )
{
	/* EA wrote this as `UnsignedInt cushion = executionFrame - TheGameLogic->getFrame()`, which is
		 fine right up until a command arrives for a frame that has already run - a late relay, or a
		 command caught up with after the run-ahead shrank.  The subtraction then wraps to around
		 four billion, and the caller hands that to FrameMetrics::addCushion, whose parameter is Int:
		 4294967295 arrives as -1, which is exactly that class's "no sample yet" sentinel.  One late
		 command therefore erased the minimum cushion for the window. */
	Int diff = (Int)(executionFrame - currentFrame);		// signed difference, wraparound-safe
	if( diff < 0 )
		diff = 0;

	return diff;
}

Bool shouldSelfSlug( Int minimumCushion, Int runAhead, UnsignedInt slackPercent )
{
	/* Negative is FrameMetrics' "nothing measured yet", set by init() and never produced by a real
		 sample once frameCushion clamps.  It used to reach here as an UnsignedInt through
		 ConnectionManager::getMinimumCushion's return type, so it read as four billion frames of
		 margin - the largest cushion representable, from the state that knows the least. */
	if( minimumCushion < 0 )
		return FALSE;

	if( runAhead <= 0 )
		return FALSE;

	return minimumCushion < selfSlugThreshold( runAhead, slackPercent );
}

Int selfSlugThreshold( Int runAhead, UnsignedInt slackPercent )
{
	/* The threshold is the slack the run-ahead was given: once the margin has eaten into that, the
		 next hiccup is a stall rather than a wobble.  With a floor, because the slack is a
		 percentage of a run-ahead that is pinned to MIN_RUNAHEAD for every link in practice - see
		 SELFSLUG_MIN_THRESHOLD_FRAMES. */
	Int threshold = (Int)((runAhead * (Int)slackPercent) / 100);
	if( threshold < SELFSLUG_MIN_THRESHOLD_FRAMES )
		threshold = SELFSLUG_MIN_THRESHOLD_FRAMES;

	return threshold;
}
