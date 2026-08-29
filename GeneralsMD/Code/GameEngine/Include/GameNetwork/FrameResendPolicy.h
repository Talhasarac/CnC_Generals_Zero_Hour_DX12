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

// FILE: FrameResendPolicy.h //////////////////////////////////////////////////////////////////////
// Desc:   When to ask a player again for a frame that never arrived.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _FRAME_RESEND_POLICY_H_
#define _FRAME_RESEND_POLICY_H_

#include "Lib/BaseType.h"

/* A frame's command count arrives exactly once, in a FRAMEINFO packet.  Until it does, FrameData
	 has nothing to compare its command list against and reports FRAMEDATA_NOTREADY - and the logic
	 waits.  That is correct while the packet is in flight and wrong the moment it is lost, because
	 FrameData only ever escalates to FRAMEDATA_RESEND when it holds *more* commands than were
	 announced.  The case where the announcement itself never came has no exit, and
	 ConnectionManager::requestFrameDataResend - which exists, and which the other machine answers -
	 was reachable from nowhere else.

	 A real match: the host sat on execution frame 30 for twenty seconds with frames 31 through 59
	 already in hand, on a link measuring 51 ms round trip and 227 ms retry timeout, and sent zero
	 resend requests in the whole game.  What finally freed it was the disconnect screen's own
	 handshake, which by design waits out NetworkStallCeilingTime - so a single dropped packet cost
	 twenty seconds and an accusing vote screen for a player who was never gone.

	 Asking is cheap; asking too eagerly is not.  Every frame in the run-ahead window is legitimately
	 "not ready" for a while, so a request before the packet has had time to arrive is pure traffic,
	 and traffic is what loses packets.  The wait is therefore sized on the link the game measured
	 for itself, not on a constant: give the packet two of that connection's retry timeouts to show
	 up, then ask, and keep asking no more often than that. */

enum
{
	/* The measured retry timeout is derived from a smoothed round trip and can read absurdly low on
		 a LAN or high after a hitch.  These bound what it is allowed to buy. */
	FRAME_RESEND_MIN_TIMEOUT_MS = 100,
	FRAME_RESEND_MAX_TIMEOUT_MS = 1000,

	/* How many retry timeouts a missing frame gets before we speak up. */
	FRAME_RESEND_TIMEOUTS_TO_WAIT = 2,
};

/**
	* Should we ask this player to send the blocking frame again?
	*
	* @param stalledMS						how long the logic has been unable to execute this frame
	* @param sinceLastRequestMS		how long since we last asked for it; pass stalledMS when we have
	*															not asked at all, which is what "no request yet" means here
	* @param retryTimeoutMS				the connection's own measured retry timeout
	*/
Bool shouldRequestFrameResend( UnsignedInt stalledMS, UnsignedInt sinceLastRequestMS,
															 UnsignedInt retryTimeoutMS );

/** The wait the rule above is built on, exposed so a caller can log what it is waiting for. */
UnsignedInt frameResendWaitMS( UnsignedInt retryTimeoutMS );

#endif // _FRAME_RESEND_POLICY_H_
