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

// Trig.h
// fast trig functions
// Author: Sondra Iverson, March 1998
// Converted to Generals by Matthew D. Campbell, February 2002

#ifndef _TRIG_H_
#define _TRIG_H_

/* The simulation's trigonometry.  These are integer table lookups, not calls
	 into the C runtime, and that is the whole point: the runtime's sin, cos and
	 atan2 are not specified bit for bit, they dispatch on the host CPU, and they
	 ship with the machine instead of with the game.  Two players running the same
	 replay on two different machines can get two different answers from them, and
	 a lockstep simulation that disagrees about an angle diverges within seconds.

	 Everything below computes the same bits everywhere.  Anything in GameLogic or
	 Common that needs an angle must call these and never sinf/cosf/atan2f - the
	 simulation_uses_no_runtime_trig test in test_gameengine holds the line.

	 Accuracy is around 1e-7 radians, the size of a float's own epsilon.  See
	 Tools/gentrigtables.py for the tables and the error budget. */

Real Sin(Real);
Real Cos(Real);
Real Tan(Real);
Real ACos(Real);
Real ASin(Real x);

/// Angle of the vector (x, y), in radians, in (-PI, PI].  Argument order is atan2's.
Real ATan2(Real y, Real x);

/// Arc tangent, in radians, in (-PI/2, PI/2).
Real ATan(Real x);

/* Deliberately absent: a square root.  sqrtf is required by IEEE 754 to be
	 correctly rounded, so it already returns identical bits on every machine and
	 replacing it would only cost accuracy.  The same goes for +, -, *, /, fabsf,
	 floorf and ceilf.  It is the transcendentals above, and only those, that the
	 standard leaves to the implementation. */

#endif // _TRIG_H_
