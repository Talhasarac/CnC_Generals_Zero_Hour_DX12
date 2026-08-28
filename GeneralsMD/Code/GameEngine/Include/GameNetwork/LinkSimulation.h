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

// FILE: LinkSimulation.h ///////////////////////////////////////////////////////////////////////
//
// The two decisions the synthetic link simulator makes, as pure functions: whether an arriving
// packet is thrown away, and how long one that is kept is held back before the game sees it.
// The transport calls both from Transport::doRecv.
//
// They live here rather than inline in the transport for the same reason StallJudgement and
// KeepAliveSchedule do: a decision that can be called without a socket can be tested without one,
// and every number in it can be pinned.
//
// Neither of these may ever be made to depend on anything the simulation reads.  The rolls handed
// in come from the client random stream, which is per-machine on purpose, and holding a packet
// back only delays the frame the lockstep protocol was already waiting for - it never changes what
// that frame contains.
//
///////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _LINK_SIMULATION_H_
#define _LINK_SIMULATION_H_

#include <math.h>
#include "Lib/BaseType.h"

/**
 * TRUE when an incoming packet should be thrown away.
 *
 * `roll` is a GameClientRandomValue(1, 100).  EA rolled (0, 100) - 101 outcomes - against the same
 * `>=`, so every percentage was one point too lossy and a setting of 0 still threw away one packet
 * in a hundred.  Over [1, 100] the count of losing rolls is the percentage itself.
 */
inline Bool linkSimPacketIsLost( Int lossPercent, Int roll )
{
	return lossPercent >= roll;
}

/**
 * The time at which a packet received at `now` should be handed to the game.
 *
 * The delay is a constant average, plus a sine wave of `amplitude` over `period` milliseconds, plus
 * the jitter already rolled by the caller.  EA's own line was `sin(now * m_latencyPeriod)`, which
 * makes the "period" a radians-per-millisecond multiplier: at any usable setting the argument moves
 * by whole radians every millisecond, so the term is not a slow modulation at all but a second,
 * bigger noise source - and `now * period` overflows an UnsignedInt after a minute of uptime.  Read
 * as the field documents itself instead: a period in milliseconds, and 0 for no modulation.
 *
 * A delay that comes out negative means "hand it over at once"; nothing may be delivered before it
 * arrived.
 */
inline UnsignedInt linkSimDeliveryTime( UnsignedInt now, Int average, Int amplitude, Int period, Int noiseRoll )
{
	Int delay = average + noiseRoll;

	if (period != 0)
	{
		const double twoPi = 6.283185307179586476925286766559;
		const UnsignedInt ms = (UnsignedInt)(period < 0 ? -period : period);
		double phase = twoPi * (double)(now % ms) / (double)ms;
		delay += (Int)(amplitude * sin( phase ));
	}

	if (delay < 0)
		delay = 0;

	return now + (UnsignedInt)delay;
}

#endif // _LINK_SIMULATION_H_
