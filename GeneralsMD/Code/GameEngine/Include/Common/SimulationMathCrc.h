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
	* This runs a fixed sequence of the transcendental and matrix operations the simulation actually
	* uses, over constants, and CRCs the result.  It depends on nothing but the machine's math, so two
	* players comparing this one number learn immediately which kind of divergence they have: the same
	* value means the arithmetic agrees and the difference is in the game state, a different value
	* means the game states were never going to agree.
	*/
class SimulationMathCrc
{
public:

	/** Compute the fingerprint.  Sets the simulation's own FPU mode for the duration and puts the
		* caller's mode back afterwards, so this is safe to call from anywhere, including mid-match. */
	static UnsignedInt calculate( void );

};

#endif // __SIMULATIONMATHCRC_H__
