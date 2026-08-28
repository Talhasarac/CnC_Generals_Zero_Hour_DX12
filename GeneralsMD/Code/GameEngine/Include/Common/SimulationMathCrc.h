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

// FILE: SimulationMathCrc.h //////////////////////////////////////////////////////////////////////
// Desc:   A fingerprint of the floating point math the simulation is built on.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SIMULATIONMATHCRC_H__
#define __SIMULATIONMATHCRC_H__

#include "Lib/BaseType.h"

/**
	* A mismatch says two machines disagreed about the world; it does not say why.  One of the
	* answers is that they never agreed about arithmetic in the first place - a different C runtime,
	* a different FPU control word left behind by a driver, a different implementation of sin().  The
	* game's own CRC cannot tell that apart from a logic bug, because both come out as "the numbers
	* differ".
	*
	* Two numbers come out of this, and the pair is the diagnosis.  calculate() runs a fixed sequence
	* of the C runtime's transcendentals and matrix operations over constants and CRCs the result: it
	* depends on nothing but the machine's math.  calculateSimulationTrig() does the same over the
	* trigonometry the simulation actually runs on, which is DetTrig's integer tables and therefore
	* the same everywhere.  Two players comparing the pair learn immediately which kind of divergence
	* they have - the trig number differing means the builds differ, the trig number agreeing and the
	* game CRC differing means the game states diverged, and the runtime number is free to differ
	* either way now that the simulation no longer calls into it.
	*/
class SimulationMathCrc
{
public:

	/** Compute the fingerprint.  Sets the simulation's own FPU mode for the duration and puts the
		* caller's mode back afterwards, so this is safe to call from anywhere, including mid-match. */
	static UnsignedInt calculate( void );

	/** The same idea over the trigonometry the simulation actually runs on - DetTrig's integer
		* tables rather than the C runtime.  The number above is allowed to differ between two
		* machines and now means nothing worse than "different Windows"; this one is not.  It is the
		* same integer arithmetic everywhere, so two dumps that disagree here are reporting a real
		* defect: a miscompiled table, a stale build, a modified binary.  Held to a literal by
		* test_gameengine's simulation_trig_fingerprint_is_pinned. */
	static UnsignedInt calculateSimulationTrig( void );

};

#endif // __SIMULATIONMATHCRC_H__
