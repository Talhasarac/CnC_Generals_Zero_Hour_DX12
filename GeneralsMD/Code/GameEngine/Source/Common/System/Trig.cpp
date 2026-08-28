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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Trig.cpp
// fast trig functions
// Author: Michael S. Booth, March 1994
// Converted to Generals by Matthew D. Campbell, February 2002

/* EA wrote an integer fixed point trig table here and then switched it off with
	 "#define DEFAULT_TRIG", which routed every call straight back to sinf, cosf,
	 tanf and acosf.  Their own comment above the switch says what it was for:
	 "use a precalculated lookup table for speed and repeatability across
	 different machines".  The repeatability is the part that matters, and the
	 shipped game does not have it.

	 The table now lives one layer down, in WWMath/dettrig.cpp, because the game
	 is not the only thing that needs it: Matrix3D::Rotate_Z and
	 Matrix3D::Get_Z_Rotation are what an object's facing actually round trips
	 through every frame, and those are WWMath's.  One table, one set of answers,
	 used by both sides.  See dettrig.h for why any of this is necessary and
	 Tools/gentrigtables.py for the error budget.

	 What is left here is the game's spelling of the same seven functions. */

#include "PreRTS.h"

#include "Lib/BaseType.h"
#include "Lib/Trig.h"

#include "dettrig.h"

Real Sin( Real x )						{ return DetTrig::Sin( x ); }
Real Cos( Real x )						{ return DetTrig::Cos( x ); }
Real Tan( Real x )						{ return DetTrig::Tan( x ); }
Real ATan( Real x )						{ return DetTrig::ATan( x ); }
Real ATan2( Real y, Real x )	{ return DetTrig::ATan2( y, x ); }
Real ACos( Real x )						{ return DetTrig::ACos( x ); }
Real ASin( Real x )						{ return DetTrig::ASin( x ); }
