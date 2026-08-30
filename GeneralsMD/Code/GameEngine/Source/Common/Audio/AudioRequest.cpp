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


#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/AudioRequest.h"


AudioRequest::~AudioRequest() 
{
	// A play request carries an event it allocated.  Pausing the game throws every pending play
	// request away, and killing a sound before it starts throws one away, and neither freed the
	// event - so every pause leaked one allocation per queued sound.
	if (m_usePendingEvent)
	{
		delete m_pendingEvent;
		m_pendingEvent = NULL;
		m_usePendingEvent = FALSE;
	}
}

AudioEventRTS *AudioRequest::releasePendingEvent()
{
	if (m_usePendingEvent)
	{
		AudioEventRTS *event = m_pendingEvent;
		m_pendingEvent = NULL;
		m_usePendingEvent = FALSE;
		return event;
	}
	return NULL;
}
