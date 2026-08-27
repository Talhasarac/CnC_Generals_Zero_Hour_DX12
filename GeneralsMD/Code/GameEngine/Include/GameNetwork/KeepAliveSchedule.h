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

// FILE: KeepAliveSchedule.h //////////////////////////////////////////////////////////////////////
// Desc:   When each player's keep-alive packet is due.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _KEEP_ALIVE_SCHEDULE_H_
#define _KEEP_ALIVE_SCHEDULE_H_

#include "Lib/BaseType.h"

/* A keep-alive is a packet with nothing in it, sent to a player we have nothing to say to.  Its
	 only job is to keep the hole open: a NAT drops the UDP mapping it made for our connection after
	 an idle timeout, and once that is gone the other machine's packets have nowhere to land.  The
	 comment in NetworkDefs.h says the interval "should be less than 30 just to keep firewall ports
	 open", and consumer routers of the era were commonly at 20 s or less - which is why the round
	 is bounded below that whatever the INI asks for.

	 They are staggered rather than sent together: one slot's worth of the round each, so eight
	 keep-alives do not leave the machine in the same tick as each other and the frame's commands.

	 NetworkKeepAliveDelay names the round in seconds.  EA parsed it, logged it, and then never read
	 it - ConnectionManager::doKeepAlive counted whole seconds against MAX_SLOTS in two function
	 statics instead, which made the real interval a hardcoded 7-8 s and the knob dead.  These
	 functions are what doKeepAlive now runs on, and they are here rather than in the middle of
	 ConnectionManager so a test can drive them without a socket. */

/** The round length in milliseconds, from the configured NetworkKeepAliveDelay in seconds. */
UnsignedInt keepAliveRoundMS( UnsignedInt configuredSeconds );

/** How many of maxSlots slots are due to have been sent by elapsedMS into a round of roundMS.
	  Slot 0 is due immediately, and the last slot one slot-width before the round ends. */
Int keepAliveSlotsDue( UnsignedInt elapsedMS, UnsignedInt roundMS, Int maxSlots );

/** Whether the round is over and the next one should start. */
Bool keepAliveRoundIsOver( UnsignedInt elapsedMS, UnsignedInt roundMS );

/** The bounds keepAliveRoundMS clamps to, in seconds. */
enum
{
	KEEPALIVE_MIN_ROUND_SECONDS = 2,		///< below this we are spending bandwidth on nothing
	KEEPALIVE_MAX_ROUND_SECONDS = 8,		///< the interval EA's counting loop actually produced, and
																			///< comfortably under the shortest NAT timeouts in the wild
};

#endif // _KEEP_ALIVE_SCHEDULE_H_
