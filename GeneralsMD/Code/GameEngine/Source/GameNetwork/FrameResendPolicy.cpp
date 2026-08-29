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

// FILE: FrameResendPolicy.cpp ////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/FrameResendPolicy.h"

UnsignedInt frameResendWaitMS( UnsignedInt retryTimeoutMS )
{
	if( retryTimeoutMS < FRAME_RESEND_MIN_TIMEOUT_MS )
		retryTimeoutMS = FRAME_RESEND_MIN_TIMEOUT_MS;

	if( retryTimeoutMS > FRAME_RESEND_MAX_TIMEOUT_MS )
		retryTimeoutMS = FRAME_RESEND_MAX_TIMEOUT_MS;

	return retryTimeoutMS * FRAME_RESEND_TIMEOUTS_TO_WAIT;
}

Bool shouldRequestFrameResend( UnsignedInt stalledMS, UnsignedInt sinceLastRequestMS,
															 UnsignedInt retryTimeoutMS )
{
	UnsignedInt wait = frameResendWaitMS( retryTimeoutMS );

	/* The packet has not had time to arrive yet.  Everything in the run-ahead window passes through
		 this state on its way to being executed. */
	if( stalledMS < wait )
		return FALSE;

	/* We have already asked, recently.  The answer may well be on the wire. */
	if( sinceLastRequestMS < wait )
		return FALSE;

	return TRUE;
}
