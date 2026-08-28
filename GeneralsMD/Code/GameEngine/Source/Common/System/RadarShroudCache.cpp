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

// FILE: RadarShroudCache.cpp /////////////////////////////////////////////////////////////////////
// Desc:   The radar's shroud layer, kept in main memory and uploaded once a frame.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/RadarShroudCache.h"

// ------------------------------------------------------------------------------------------------
RadarShroudCache::RadarShroudCache()
{
	m_alpha = NULL;
	m_width = 0;
	m_height = 0;
	m_dirty = FALSE;
	m_dirtyMinX = 0;
	m_dirtyMinY = 0;
	m_dirtyMaxX = -1;
	m_dirtyMaxY = -1;
}

// ------------------------------------------------------------------------------------------------
RadarShroudCache::~RadarShroudCache()
{
	delete [] m_alpha;
	m_alpha = NULL;
}

// ------------------------------------------------------------------------------------------------
void RadarShroudCache::setSize( Int width, Int height )
{
	delete [] m_alpha;
	m_alpha = NULL;
	m_width = 0;
	m_height = 0;
	m_dirty = FALSE;
	m_dirtyMinX = 0;
	m_dirtyMinY = 0;
	m_dirtyMaxX = -1;
	m_dirtyMaxY = -1;

	if( width <= 0 || height <= 0 )
		return;

	m_width = width;
	m_height = height;
	m_alpha = NEW UnsignedByte[ width * height ];

	// the texture we shadow was just created and holds nothing we know about, so everything owes
	// it a write
	clear( 0 );
}

// ------------------------------------------------------------------------------------------------
void RadarShroudCache::clear( UnsignedByte alpha )
{
	if( m_alpha == NULL )
		return;

	memset( m_alpha, alpha, m_width * m_height );

	m_dirty = TRUE;
	m_dirtyMinX = 0;
	m_dirtyMinY = 0;
	m_dirtyMaxX = m_width - 1;
	m_dirtyMaxY = m_height - 1;
}

// ------------------------------------------------------------------------------------------------
void RadarShroudCache::setAlpha( Int x, Int y, UnsignedByte alpha )
{
	if( m_alpha == NULL || x < 0 || y < 0 || x >= m_width || y >= m_height )
		return;

	UnsignedByte *cell = m_alpha + y * m_width + x;
	if( *cell == alpha )
		return;

	*cell = alpha;
	markDirty( x, y );
}

// ------------------------------------------------------------------------------------------------
UnsignedByte RadarShroudCache::getAlpha( Int x, Int y ) const
{
	if( m_alpha == NULL || x < 0 || y < 0 || x >= m_width || y >= m_height )
		return 0;

	return m_alpha[ y * m_width + x ];
}

// ------------------------------------------------------------------------------------------------
void RadarShroudCache::markDirty( Int x, Int y )
{
	if( m_dirty == FALSE )
	{
		m_dirty = TRUE;
		m_dirtyMinX = m_dirtyMaxX = x;
		m_dirtyMinY = m_dirtyMaxY = y;
		return;
	}

	if( x < m_dirtyMinX ) m_dirtyMinX = x;
	if( x > m_dirtyMaxX ) m_dirtyMaxX = x;
	if( y < m_dirtyMinY ) m_dirtyMinY = y;
	if( y > m_dirtyMaxY ) m_dirtyMaxY = y;
}

// ------------------------------------------------------------------------------------------------
void RadarShroudCache::flushTo( void *bits, Int pitch, Int bytesPerPixel )
{
	if( m_alpha == NULL || bits == NULL || m_dirty == FALSE )
		return;

	/* The colour is GameMakeColor( 0, 0, 0, alpha ) - black, and the alpha in the top byte - and
		 it is written the way the surface wants it.  Two-byte surfaces get the 4-bit alpha in the
		 top nibble; the DrawPixel this replaces masked the colour with 0xFFFF instead, which threw
		 the alpha away and left the radar shroud invisible.  No machine we know of picks that
		 format for the shroud (A8R8G8B8 comes first and every Direct3D device supports it), so
		 this is a latent fix, not a visible change. */
	for( Int y = m_dirtyMinY; y <= m_dirtyMaxY; y++ )
	{
		const UnsignedByte *src = m_alpha + y * m_width + m_dirtyMinX;
		UnsignedByte *row = (UnsignedByte *)bits + y * pitch + m_dirtyMinX * bytesPerPixel;

		for( Int x = m_dirtyMinX; x <= m_dirtyMaxX; x++, src++, row += bytesPerPixel )
		{
			switch( bytesPerPixel )
			{
				case 1:
					*row = *src;
					break;
				case 2:
					*(UnsignedShort *)row = (UnsignedShort)((*src >> 4) << 12);
					break;
				case 4:
					*(UnsignedInt *)row = ((UnsignedInt)*src) << 24;
					break;
			}
		}
	}

	m_dirty = FALSE;
	m_dirtyMinX = 0;
	m_dirtyMinY = 0;
	m_dirtyMaxX = -1;
	m_dirtyMaxY = -1;
}
