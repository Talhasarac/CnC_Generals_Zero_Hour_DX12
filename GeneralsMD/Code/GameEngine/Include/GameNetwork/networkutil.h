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


#pragma once
#ifndef __NETWORKUTIL_H
#define __NETWORKUTIL_H

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/NetCommandMsg.h"

UnsignedInt ResolveIP(AsciiString host);
UnsignedShort GenerateNextCommandID();
Bool DoesCommandRequireACommandID(NetCommandType type);
Bool CommandRequiresAck(NetCommandMsg *msg);
Bool CommandRequiresDirectSend(NetCommandMsg *msg);
Bool IsCommandSynchronized(NetCommandType type);
AsciiString GetAsciiNetCommandType(NetCommandType type);

// Command ids are sixteen bits wide and wrap.  Ordering them with < or > puts every command
// issued after a wrap in front of every command issued before it, so the answer comes from the
// distance between them instead: newVal is newer when it sits within half the id space ahead of
// oldVal.  Equal ids are not newer.
inline Bool IsCommandIdNewer( UnsignedShort newVal, UnsignedShort oldVal )
{
	const UnsignedShort diff = newVal - oldVal;
	return diff != 0 && diff < 0x8000;
}

//
// The command list is sorted by command type, then player id, then sort number.  The sort number
// is the command id for everything except the three ack types, which sort by the id of the command
// they acknowledge - an ack's own id is not a key at all: it stays at the constructor's zero when
// the ack is made to be sent, and NetPacket::getCommandList sets it to the packet's running command
// counter when one is read off the wire, which acks do not advance.  Either way it is the same
// number for every ack in a run.  Every comparison in the list has to use the same key, so all of
// them ask these.
//
inline Bool IsCommandFromSamePlayerGroup( NetCommandMsg *first, NetCommandMsg *second )
{
	return first->getNetCommandType() == second->getNetCommandType() &&
				 first->getPlayerID() == second->getPlayerID();
}

inline Bool IsCommandNewerInSamePlayerGroup( NetCommandMsg *newCommand, NetCommandMsg *oldCommand )
{
	return IsCommandFromSamePlayerGroup( newCommand, oldCommand ) &&
				 IsCommandIdNewer( (UnsignedShort)newCommand->getSortNumber(), (UnsignedShort)oldCommand->getSortNumber() );
}

inline Bool IsCommandNewer( NetCommandMsg *newCommand, NetCommandMsg *oldCommand )
{
	if( newCommand->getNetCommandType() != oldCommand->getNetCommandType() )
		return newCommand->getNetCommandType() > oldCommand->getNetCommandType();

	if( newCommand->getPlayerID() != oldCommand->getPlayerID() )
		return newCommand->getPlayerID() > oldCommand->getPlayerID();

	return IsCommandIdNewer( (UnsignedShort)newCommand->getSortNumber(), (UnsignedShort)oldCommand->getSortNumber() );
}

#ifdef DEBUG_LOGGING
extern "C" {
void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line);
};
#define LOGBUFFER(buf, len) dumpBufferToLog(buf, len, __FILE__, __LINE__)
#else
#define LOGBUFFER(buf, len) {}
#endif // DEBUG_LOGGING

#endif
