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

// FILE: CRCSnapshotRing.cpp //////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameLogic/CRCSnapshotRing.h"

//-------------------------------------------------------------------------------------------------
CRCSnapshotRing::CRCSnapshotRing( void )
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::clear( void )
{
	for( Int i = 0; i < CRC_SNAPSHOT_RING_SIZE; ++i )
	{
		m_snapshots[i].m_frame = 0;
		m_snapshots[i].m_totalCRC = 0;
		m_snapshots[i].m_randomSeed = 0;
		m_snapshots[i].m_valid = FALSE;
		m_snapshots[i].m_objects.clear();
	}
	m_writeSlot = -1;
	m_numSnapshots = 0;
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::beginSnapshot( UnsignedInt frame )
{
	m_writeSlot = (m_writeSlot + 1) % CRC_SNAPSHOT_RING_SIZE;

	CRCSnapshot &snap = m_snapshots[ m_writeSlot ];

	/* The oldest slot is being reused; if it held a complete snapshot it no longer counts as one
		 until this snapshot closes. */
	if( snap.m_valid )
		--m_numSnapshots;

	snap.m_frame = frame;
	snap.m_totalCRC = 0;
	snap.m_randomSeed = 0;
	snap.m_valid = FALSE;
	// keep the vector's capacity - the object count barely changes from one CRC frame to the next
	snap.m_objects.clear();
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::addObject( UnsignedInt id, UnsignedInt runningCRC,
																 Real x, Real y, Real z, Real health )
{
	if( m_writeSlot < 0 )
		return;

	CRCObjectEntry entry;
	entry.m_id = id;
	entry.m_runningCRC = runningCRC;
	entry.m_x = x;
	entry.m_y = y;
	entry.m_z = z;
	entry.m_health = health;

	m_snapshots[ m_writeSlot ].m_objects.push_back( entry );
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::endSnapshot( UnsignedInt totalCRC, UnsignedInt randomSeed )
{
	if( m_writeSlot < 0 )
		return;

	CRCSnapshot &snap = m_snapshots[ m_writeSlot ];
	snap.m_totalCRC = totalCRC;
	snap.m_randomSeed = randomSeed;
	snap.m_valid = TRUE;
	++m_numSnapshots;
}

//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::getNewestSlot( void ) const
{
	if( m_writeSlot < 0 )
		return -1;

	// only the slot being filled can be incomplete, so at worst we step back over that one
	for( Int k = 0; k < CRC_SNAPSHOT_RING_SIZE; ++k )
	{
		Int slot = m_writeSlot - k;
		while( slot < 0 )
			slot += CRC_SNAPSHOT_RING_SIZE;

		if( m_snapshots[ slot ].m_valid )
			return slot;
	}

	return -1;
}

//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::getNthNewestSlot( Int n ) const
{
	Int newest = getNewestSlot();
	if( newest < 0 || n < 0 || n >= m_numSnapshots )
		return -1;

	Int slot = newest - n;
	while( slot < 0 )
		slot += CRC_SNAPSHOT_RING_SIZE;

	return m_snapshots[ slot ].m_valid ? slot : -1;
}

//-------------------------------------------------------------------------------------------------
/** Our own CRC for the mismatching frame is one of the values that were just compared, so the slot
	* carrying it names the frame the mismatch happened on - which is several frames behind the frame
	* it was detected on. */
//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::findSlotByCRC( UnsignedInt crc ) const
{
	for( Int n = 0; n < m_numSnapshots; ++n )
	{
		Int slot = getNthNewestSlot( n );
		if( slot >= 0 && m_snapshots[ slot ].m_totalCRC == crc )
			return slot;
	}

	return -1;
}

//-------------------------------------------------------------------------------------------------
const CRCSnapshot *CRCSnapshotRing::getSlot( Int slot ) const
{
	if( slot < 0 || slot >= CRC_SNAPSHOT_RING_SIZE || !m_snapshots[ slot ].m_valid )
		return NULL;

	return &m_snapshots[ slot ];
}
