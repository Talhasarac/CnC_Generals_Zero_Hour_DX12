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

// FILE: KeepAliveSchedule.cpp ////////////////////////////////////////////////////////////////////
// Desc:   When each player's keep-alive packet is due.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameNetwork/KeepAliveSchedule.h"

UnsignedInt keepAliveRoundMS( UnsignedInt configuredSeconds )
{
	/* The knob comes out of an INI file, so it can be anything, including zero.  Clamping rather
		 than trusting it is the point of having the round in one place: too short wastes bandwidth
		 on a full lobby, too long and the NAT mapping is gone before we refresh it, which is the
		 failure this packet exists to prevent. */
	if( configuredSeconds < KEEPALIVE_MIN_ROUND_SECONDS )
		configuredSeconds = KEEPALIVE_MIN_ROUND_SECONDS;
	if( configuredSeconds > KEEPALIVE_MAX_ROUND_SECONDS )
		configuredSeconds = KEEPALIVE_MAX_ROUND_SECONDS;

	return configuredSeconds * 1000;
}

Int keepAliveSlotsDue( UnsignedInt elapsedMS, UnsignedInt roundMS, Int maxSlots )
{
	if( maxSlots <= 0 )
		return 0;

	UnsignedInt perSlot = roundMS / (UnsignedInt)maxSlots;
	if( perSlot == 0 )
		return maxSlots;		// a round shorter than one slot: everybody is due at once

	Int due = (Int)(elapsedMS / perSlot) + 1;		// slot 0 is due at elapsed zero
	if( due > maxSlots )
		due = maxSlots;

	return due;
}

Bool keepAliveRoundIsOver( UnsignedInt elapsedMS, UnsignedInt roundMS )
{
	return elapsedMS >= roundMS;
}
