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

// FILE: FPUControl.cpp ///////////////////////////////////////////////////////////////////////////
// Desc:   Pinning the FPU so two machines compute the same numbers.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameLogic/FPUControl.h"
#include <float.h>

/* This lives in its own translation unit rather than in GameLogic.cpp, where EA had it, so that a
	 test can call it without linking the whole of the game logic behind it. */

void setFPMode( void )
{
	/* Every float the simulation computes has to come out the same on both machines, and the x87
		 control word decides that: precision and rounding mode.  Nothing in the process guarantees
		 it stays put - Direct3D sets it when it creates a device without D3DCREATE_FPU_PRESERVE,
		 audio and video drivers have historically set it on their own threads, and any DLL loaded
		 into the process may set it once and never restore it.  So it is not set once at startup;
		 GameLogic::update re-asserts it at the top of every logic frame, and so does anything that
		 hands control to the renderer inside a logic loop (the load screens, INI parsing).

		 _fpreset() first, because it puts the whole word - exception masks included - into a known
		 state rather than only the two fields written below. */
	_fpreset();

	/* Rounding to nearest, and 24-bit (single) precision.  The precision could be anything as long
		 as it is the same everywhere; 24 bits is what Direct3D leaves behind anyway, so asking for
		 it costs nothing and removes one way for the two to disagree.

		 EA read the current word with _statusfp(), which returns the *status* word - the sticky
		 exception flags - not the control word.  It happened to be harmless, because the mask below
		 keeps everything except the precision and rounding fields and no status flag lands in
		 either.  _controlfp(0, 0) is what they meant: it reads the control word without writing. */
	UnsignedInt curVal = _controlfp( 0, 0 );
	UnsignedInt newVal = curVal;
	newVal = (newVal & ~_MCW_RC) | (_RC_NEAR & _MCW_RC);
	newVal = (newVal & ~_MCW_PC) | (_PC_24   & _MCW_PC);

	_controlfp( newVal, _MCW_PC | _MCW_RC );
}

UnsignedInt getFPMode( void )
{
	return _controlfp( 0, 0 ) & (_MCW_PC | _MCW_RC);
}

UnsignedInt expectedFPMode( void )
{
	return (_PC_24 & _MCW_PC) | (_RC_NEAR & _MCW_RC);
}
