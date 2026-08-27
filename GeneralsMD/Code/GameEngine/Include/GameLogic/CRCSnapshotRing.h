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

// FILE: CRCSnapshotRing.h ////////////////////////////////////////////////////////////////////////
// Evidence for a mismatch, kept before anyone knows there will be one.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CRC_SNAPSHOT_RING_H_
#define _CRC_SNAPSHOT_RING_H_

#include "Lib/BaseType.h"
#include <vector>

/* A mismatch is reported as one number: "the world CRC differs".  Worse, it is reported the
	 run-ahead's worth of frames after the frame it was computed for, by which time the world has
	 moved on and the divergent object is unrecoverable.  So keep the evidence while it is free to
	 keep: as the world CRC is computed, write down the running CRC after each object, into a ring
	 of the last few CRC frames.  Nothing reads the ring unless a mismatch is found, and then the
	 first object whose running CRC differs between two players' dumps is the object that diverged. */

#define CRC_SNAPSHOT_RING_SIZE 16

struct CRCObjectEntry
{
	UnsignedInt m_id;								///< the ObjectID, as a plain int so this file needs no engine types
	UnsignedInt m_runningCRC;				///< world CRC after this object was folded in
	Real m_x, m_y, m_z;							///< where it was, to read the dump without cross-referencing
	Real m_health;
};

struct CRCSnapshot
{
	UnsignedInt m_frame;
	UnsignedInt m_totalCRC;
	UnsignedInt m_randomSeed;
	Bool m_valid;										///< FALSE until endSnapshot() closes it
	std::vector<CRCObjectEntry> m_objects;
};

//-------------------------------------------------------------------------------------------------
/** A fixed ring of per-object CRC snapshots.  Deliberately free of engine singletons so it can be
	* tested on its own. */
//-------------------------------------------------------------------------------------------------
class CRCSnapshotRing
{
public:
	CRCSnapshotRing( void );

	void clear( void );																					///< forget everything (new game)

	void beginSnapshot( UnsignedInt frame );										///< start filling the next slot
	void addObject( UnsignedInt id, UnsignedInt runningCRC,
									Real x, Real y, Real z, Real health );				///< record one object's running CRC
	void endSnapshot( UnsignedInt totalCRC, UnsignedInt randomSeed );	///< close the slot being filled

	Int getNewestSlot( void ) const;														///< -1 when no snapshot is complete
	Int getNthNewestSlot( Int n ) const;												///< n==0 is the newest; -1 when out of range
	Int findSlotByCRC( UnsignedInt crc ) const;									///< newest first; -1 when no slot has that CRC
	const CRCSnapshot *getSlot( Int slot ) const;								///< NULL for an out of range or unfilled slot

	Int getNumSnapshots( void ) const { return m_numSnapshots; }

private:
	CRCSnapshot m_snapshots[ CRC_SNAPSHOT_RING_SIZE ];
	Int m_writeSlot;																						///< slot beginSnapshot() last opened, -1 before the first
	Int m_numSnapshots;																					///< how many slots hold real data
};

#endif // _CRC_SNAPSHOT_RING_H_
