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

// RandomMapGenerator.cpp
// Writes a .map image straight into memory: the same CkMp chunk stream
// WorldBuilder saves (see WHeightMapEdit::saveToFile), minus everything a
// skirmish map does not need.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <math.h>

#include "Common/RandomMapGenerator.h"
#include "Lib/Trig.h"
#include "Common/DataChunk.h"
#include "Common/Dict.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"

// Chunk versions the writers of these chunks use. They live in the .cpp files
// that write them (SidesList.cpp, Scripts.cpp), not in MapReaderWriterInfo.h.
static const Int K_SIDES_DATA_VERSION_3 = 3;
static const Int K_SCRIPTS_DATA_VERSION_1 = 1;
static const Int K_SCRIPT_LIST_DATA_VERSION_1 = 1;

//-----------------------------------------------------------------------------
// The map we build
//-----------------------------------------------------------------------------

// Terrain outside the playable area. The camera looks across it at the map
// edges, so it is scenery, not play space.
#define RMG_BORDER_CELLS		20

// One texture class, the four tiles of a 2x2 grass sheet. Named the way
// Terrain.ini names it, since the reader looks the texture up by that name.
#define RMG_TEXTURE_NAME		"GrassType1"
#define RMG_TEXTURE_TILES		4
#define RMG_TEXTURE_WIDTH		2

// Height field, in height map bytes (a byte is MAP_HEIGHT_SCALE world units).
#define RMG_BASE_HEIGHT			40.0f
#define RMG_AMPLITUDE			22.0f
#define RMG_OCTAVES				4
#define RMG_FEATURES_PER_MAP	4.0f

// A start position gets a flat disc to build on, easing back into the terrain.
#define RMG_FLAT_RADIUS			14.0f
#define RMG_BLEND_RADIUS		30.0f

// Where the start positions sit, as a fraction of the playable size.
#define RMG_START_RING			0.34f
// Supply docks per player, and how far from the start they sit, in cells.
#define RMG_SUPPLY_PER_PLAYER	2
#define RMG_SUPPLY_DISTANCE		20.0f

//-----------------------------------------------------------------------------
// Chunk writer
//-----------------------------------------------------------------------------

/** Writes the CkMp stream DataChunkOutput writes, but into a buffer instead of
	through a temp file in the user data directory. Chunk names and dictionary
	keys share one table of contents, exactly as the reader expects. */
class MapChunkWriter
{
public:
	void openChunk( const char *name, DataChunkVersionType version );
	void closeChunk( void );

	void writeInt( Int v );
	void writeReal( Real v );
	void writeByte( Byte v );
	void writeBytes( const void *data, Int len );
	void writeAsciiString( const char *s );

	void beginDict( Int pairCount );
	void dictBool( const char *key, Bool v );
	void dictInt( const char *key, Int v );
	void dictAsciiString( const char *key, const char *v );

	/// Table of contents followed by the chunk stream.
	void finish( std::vector<char>& out );

private:
	UnsignedInt idFor( const char *name );
	void writeKeyAndType( const char *key, Dict::DataType type );

	std::vector<char> m_body;
	std::vector<AsciiString> m_names;	///< index i holds the name of id i+1
	std::vector<Int> m_openChunks;		///< offsets of the size fields still to patch
};

UnsignedInt MapChunkWriter::idFor( const char *name )
{
	for( UnsignedInt i = 0; i < m_names.size(); i++ )
	{
		if( m_names[i].compare( name ) == 0 )
			return i + 1;
	}

	m_names.push_back( AsciiString( name ) );
	return m_names.size();
}

void MapChunkWriter::writeBytes( const void *data, Int len )
{
	const char *p = (const char *)data;
	m_body.insert( m_body.end(), p, p + len );
}

void MapChunkWriter::writeInt( Int v )		{ writeBytes( &v, sizeof(Int) ); }
void MapChunkWriter::writeReal( Real v )	{ writeBytes( &v, sizeof(Real) ); }
void MapChunkWriter::writeByte( Byte v )	{ writeBytes( &v, sizeof(Byte) ); }

void MapChunkWriter::writeAsciiString( const char *s )
{
	UnsignedShort len = (UnsignedShort)strlen( s );
	writeBytes( &len, sizeof(UnsignedShort) );
	writeBytes( s, len );
}

void MapChunkWriter::openChunk( const char *name, DataChunkVersionType version )
{
	UnsignedInt id = idFor( name );
	writeBytes( &id, sizeof(UnsignedInt) );
	writeBytes( &version, sizeof(DataChunkVersionType) );

	m_openChunks.push_back( m_body.size() );
	Int placeholder = 0;
	writeBytes( &placeholder, sizeof(Int) );
}

void MapChunkWriter::closeChunk( void )
{
	Int sizeFieldPos = m_openChunks.back();
	m_openChunks.pop_back();

	Int size = m_body.size() - sizeFieldPos - sizeof(Int);
	memcpy( &m_body[sizeFieldPos], &size, sizeof(Int) );
}

void MapChunkWriter::beginDict( Int pairCount )
{
	UnsignedShort len = (UnsignedShort)pairCount;
	writeBytes( &len, sizeof(UnsignedShort) );
}

void MapChunkWriter::writeKeyAndType( const char *key, Dict::DataType type )
{
	Int keyAndType = idFor( key );
	keyAndType <<= 8;
	keyAndType |= (type & 0xff);
	writeInt( keyAndType );
}

void MapChunkWriter::dictBool( const char *key, Bool v )
{
	writeKeyAndType( key, Dict::DICT_BOOL );
	writeByte( v ? 1 : 0 );
}

void MapChunkWriter::dictInt( const char *key, Int v )
{
	writeKeyAndType( key, Dict::DICT_INT );
	writeInt( v );
}

void MapChunkWriter::dictAsciiString( const char *key, const char *v )
{
	writeKeyAndType( key, Dict::DICT_ASCIISTRING );
	writeAsciiString( v );
}

void MapChunkWriter::finish( std::vector<char>& out )
{
	out.clear();

	const char tag[4] = { 'C', 'k', 'M', 'p' };
	out.insert( out.end(), tag, tag + 4 );

	Int listLength = m_names.size();
	const char *p = (const char *)&listLength;
	out.insert( out.end(), p, p + sizeof(Int) );

	for( Int i = 0; i < listLength; i++ )
	{
		unsigned char len = (unsigned char)m_names[i].getLength();
		out.push_back( (char)len );
		out.insert( out.end(), m_names[i].str(), m_names[i].str() + len );

		UnsignedInt id = i + 1;
		p = (const char *)&id;
		out.insert( out.end(), p, p + sizeof(UnsignedInt) );
	}

	out.insert( out.end(), m_body.begin(), m_body.end() );
}

//-----------------------------------------------------------------------------
// Perlin noise
//-----------------------------------------------------------------------------

/** Permutation seeded by an integer generator only - no rand(), no clock - so
	the field is identical on every machine that asks for the same seed. */
static void seedPermutation( Int seed, UnsignedByte perm[512] )
{
	UnsignedInt state = (UnsignedInt)seed * 1664525U + 1013904223U;
	if( state == 0 )
		state = 0x9E3779B9U;

	Int i;
	for( i = 0; i < 256; i++ )
		perm[i] = (UnsignedByte)i;

	for( i = 255; i > 0; i-- )
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		Int j = (Int)(state % (UnsignedInt)(i + 1));

		UnsignedByte tmp = perm[i];
		perm[i] = perm[j];
		perm[j] = tmp;
	}

	for( i = 0; i < 256; i++ )
		perm[256 + i] = perm[i];
}

static Real fadeCurve( Real t )
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static Real lerpReal( Real a, Real b, Real t )
{
	return a + t * (b - a);
}

static Real gradient( Int hash, Real x, Real y )
{
	switch( hash & 7 )
	{
		case 0:		return  x;
		case 1:		return  x + y;
		case 2:		return  y;
		case 3:		return -x + y;
		case 4:		return -x;
		case 5:		return -x - y;
		case 6:		return -y;
		default:	return  x - y;
	}
}

/// Classic 2D Perlin noise, in [-1,1].
static Real perlin2( const UnsignedByte perm[512], Real x, Real y )
{
	Int xi = (Int)floorf( x );
	Int yi = (Int)floorf( y );
	Real xf = x - (Real)xi;
	Real yf = y - (Real)yi;

	Int gx = xi & 255;
	Int gy = yi & 255;

	Real u = fadeCurve( xf );
	Real v = fadeCurve( yf );

	Int aa = perm[perm[gx] + gy];
	Int ab = perm[perm[gx] + gy + 1];
	Int ba = perm[perm[gx + 1] + gy];
	Int bb = perm[perm[gx + 1] + gy + 1];

	Real x1 = lerpReal( gradient( aa, xf, yf ), gradient( ba, xf - 1.0f, yf ), u );
	Real x2 = lerpReal( gradient( ab, xf, yf - 1.0f ), gradient( bb, xf - 1.0f, yf - 1.0f ), u );

	return lerpReal( x1, x2, v );
}

/// Sum of octaves, normalized back into [-1,1].
static Real fractalNoise( const UnsignedByte perm[512], Real x, Real y )
{
	Real total = 0.0f;
	Real amplitude = 1.0f;
	Real frequency = 1.0f;
	Real range = 0.0f;

	for( Int octave = 0; octave < RMG_OCTAVES; octave++ )
	{
		total += amplitude * perlin2( perm, x * frequency, y * frequency );
		range += amplitude;
		amplitude *= 0.5f;
		frequency *= 2.0f;
	}

	return total / range;
}

//-----------------------------------------------------------------------------
// Map pieces
//-----------------------------------------------------------------------------

/** The tile index WorldBuilder computes for a cell: four quadrants packed into
	each source tile, the source tile picked by tiling the class across the map. */
static Short tileIndexForCell( Int x, Int y, Int firstTile, Int width )
{
	Int ndx = firstTile + ((x / 2) % width) + width * ((y / 2) % width);
	ndx = (ndx << 2) + 2 * (y & 1) + (x & 1);
	return (Short)ndx;
}

struct RMGStart
{
	Real m_cellX;		///< in playable cells
	Real m_cellY;
	Real m_angle;		///< direction from the middle of the map outwards
};

static void computeStarts( const RandomMapSettings& settings, RMGStart *starts )
{
	Real playable = (Real)settings.m_playableCells;
	Real center = playable * 0.5f;
	Real ring = playable * RMG_START_RING;

	// One of 1024 rotations, so different seeds do not all put player one in
	// the same corner.
	UnsignedInt state = (UnsignedInt)settings.m_seed * 2654435761U + 12345U;
	state ^= state >> 15;
	Real baseAngle = (Real)(state % 1024U) * (2.0f * PI / 1024.0f);

	for( Int i = 0; i < settings.m_numPlayers; i++ )
	{
		Real angle = baseAngle + (2.0f * PI * (Real)i) / (Real)settings.m_numPlayers;
		starts[i].m_cellX = center + ring * Cos( angle );
		starts[i].m_cellY = center + ring * Sin( angle );
		starts[i].m_angle = angle;
	}
}

/** Height map bytes for the whole map, border included. Playable cell (0,0) is
	map cell (border,border), which is world (0,0). */
static void buildHeights( const RandomMapSettings& settings, const RMGStart *starts,
													Int width, Int height, std::vector<UnsignedByte>& data )
{
	UnsignedByte perm[512];
	seedPermutation( settings.m_seed, perm );

	Real scale = RMG_FEATURES_PER_MAP / (Real)settings.m_playableCells;

	data.resize( width * height );

	// The flat disc under each start takes the height the noise gives at that
	// spot, so the discs sit in the landscape instead of on top of it.
	Real startHeight[RandomMapGenerator::MAX_PLAYERS];
	Int i;
	for( i = 0; i < settings.m_numPlayers; i++ )
	{
		Real n = fractalNoise( perm, starts[i].m_cellX * scale, starts[i].m_cellY * scale );
		startHeight[i] = RMG_BASE_HEIGHT + RMG_AMPLITUDE * n;
	}

	for( Int y = 0; y < height; y++ )
	{
		for( Int x = 0; x < width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);

			Real h = RMG_BASE_HEIGHT + RMG_AMPLITUDE * fractalNoise( perm, px * scale, py * scale );

			for( i = 0; i < settings.m_numPlayers; i++ )
			{
				Real dx = px - starts[i].m_cellX;
				Real dy = py - starts[i].m_cellY;
				Real dist = sqrtf( dx * dx + dy * dy );
				if( dist >= RMG_BLEND_RADIUS )
					continue;

				Real t = 0.0f;
				if( dist > RMG_FLAT_RADIUS )
					t = (dist - RMG_FLAT_RADIUS) / (RMG_BLEND_RADIUS - RMG_FLAT_RADIUS);

				h = lerpReal( startHeight[i], h, fadeCurve( t ) );
			}

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			data[y * width + x] = (UnsignedByte)(h + 0.5f);
		}
	}
}

/// One waypoint object. The reader calls anything with a waypointID a waypoint.
static void writeWaypoint( MapChunkWriter& w, const char *name, Int waypointID, Real x, Real y )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( x );
		w.writeReal( y );
		w.writeReal( 0.0f );
		w.writeReal( 0.0f );
		w.writeInt( 0 );
		w.writeAsciiString( "*Waypoints/Waypoint" );

		w.beginDict( 12 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "originalOwner", "team" );
		w.dictAsciiString( "uniqueID", name );
		w.dictAsciiString( "objectLayer", "Start Points" );
		w.dictInt( "waypointID", waypointID );
		w.dictBool( "objectDestructible", TRUE );
		w.dictBool( "objectSellable", TRUE );
		w.dictBool( "objectRepairable", TRUE );
		w.dictAsciiString( "waypointName", name );
	w.closeChunk();
}

static void writeSupplyDock( MapChunkWriter& w, const char *uniqueID, Real x, Real y, Real angle )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( x );
		w.writeReal( y );
		w.writeReal( 0.0f );
		w.writeReal( angle );
		w.writeInt( 0 );
		w.writeAsciiString( "SupplyDock" );

		w.beginDict( 11 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectIndestructible", FALSE );
		w.dictBool( "objectUnsellable", FALSE );
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "objectName", "" );
		w.dictAsciiString( "originalOwner", "team" );
		w.dictAsciiString( "uniqueID", uniqueID );
		w.dictAsciiString( "objectLayer", "" );
		w.dictBool( "objectSelectable", TRUE );
	w.closeChunk();
}

/** The sides every skirmish map carries: the civilians that own map scenery,
	and one side per playable faction for the skirmish scripts to attach to. */
static const char *theSkirmishSides[][2] =
{
	{ "PlyrCivilian",						"FactionCivilian" },
	{ "SkirmishAmerica",					"FactionAmerica" },
	{ "SkirmishChina",						"FactionChina" },
	{ "SkirmishGLA",						"FactionGLA" },
	{ "SkirmishAmericaAirForceGeneral",		"FactionAmericaAirForceGeneral" },
	{ "SkirmishAmericaLaserGeneral",		"FactionAmericaLaserGeneral" },
	{ "SkirmishAmericaSuperWeaponGeneral",	"FactionAmericaSuperWeaponGeneral" },
	{ "SkirmishChinaTankGeneral",			"FactionChinaTankGeneral" },
	{ "SkirmishChinaNukeGeneral",			"FactionChinaNukeGeneral" },
	{ "SkirmishChinaInfantryGeneral",		"FactionChinaInfantryGeneral" },
	{ "SkirmishGLADemolitionGeneral",		"FactionGLADemolitionGeneral" },
	{ "SkirmishGLAToxinGeneral",			"FactionGLAToxinGeneral" },
	{ "SkirmishGLAStealthGeneral",			"FactionGLAStealthGeneral" },
};
static const Int theNumSkirmishSides = sizeof(theSkirmishSides) / sizeof(theSkirmishSides[0]);

static void writeSides( MapChunkWriter& w )
{
	Int numSides = theNumSkirmishSides + 1;		// plus neutral
	Int i;

	w.openChunk( "SidesList", K_SIDES_DATA_VERSION_3 );
		w.writeInt( numSides );

		// Neutral, the side that owns the map itself.
		w.beginDict( 6 );
		w.dictAsciiString( "playerName", "" );
		w.dictBool( "playerIsHuman", FALSE );
		w.dictAsciiString( "playerDisplayName", "Neutral" );
		w.dictAsciiString( "playerFaction", "" );
		w.dictAsciiString( "playerAllies", "" );
		w.dictAsciiString( "playerEnemies", "" );
		w.writeInt( 0 );	// empty build list

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			w.beginDict( 6 );
			w.dictAsciiString( "playerName", theSkirmishSides[i][0] );
			w.dictBool( "playerIsHuman", FALSE );
			w.dictAsciiString( "playerDisplayName", theSkirmishSides[i][0] );
			w.dictAsciiString( "playerFaction", theSkirmishSides[i][1] );
			w.dictAsciiString( "playerAllies", "" );
			w.dictAsciiString( "playerEnemies", "" );
			w.writeInt( 0 );	// empty build list
		}

		// One default team per side.
		w.writeInt( numSides );

		w.beginDict( 3 );
		w.dictAsciiString( "teamName", "team" );
		w.dictAsciiString( "teamOwner", "" );
		w.dictBool( "teamIsSingleton", TRUE );

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			AsciiString teamName;
			teamName.format( "team%s", theSkirmishSides[i][0] );

			w.beginDict( 3 );
			w.dictAsciiString( "teamName", teamName.str() );
			w.dictAsciiString( "teamOwner", theSkirmishSides[i][0] );
			w.dictBool( "teamIsSingleton", TRUE );
		}

		// No scripts: skirmish and multiplayer games fall back to the shipped
		// SkirmishScripts.scb / MultiplayerScripts.scb.
		w.openChunk( "PlayerScriptsList", K_SCRIPTS_DATA_VERSION_1 );
			for( i = 0; i < numSides; i++ )
			{
				w.openChunk( "ScriptList", K_SCRIPT_LIST_DATA_VERSION_1 );
				w.closeChunk();
			}
		w.closeChunk();
	w.closeChunk();
}

//-----------------------------------------------------------------------------
// RandomMapGenerator
//-----------------------------------------------------------------------------

void RandomMapGenerator::generate( const RandomMapSettings& settings, std::vector<char>& mapBytes )
{
	RandomMapSettings s = settings;
	if( s.m_playableCells < MIN_CELLS ) s.m_playableCells = MIN_CELLS;
	if( s.m_playableCells > MAX_CELLS ) s.m_playableCells = MAX_CELLS;
	if( s.m_numPlayers < MIN_PLAYERS ) s.m_numPlayers = MIN_PLAYERS;
	if( s.m_numPlayers > MAX_PLAYERS ) s.m_numPlayers = MAX_PLAYERS;

	Int playable = s.m_playableCells;
	Int width = playable + 2 * RMG_BORDER_CELLS;
	Int height = width;
	Int dataSize = width * height;

	RMGStart starts[MAX_PLAYERS];
	computeStarts( s, starts );

	std::vector<UnsignedByte> heights;
	buildHeights( s, starts, width, height, heights );

	MapChunkWriter w;

	/***************HEIGHT MAP DATA ***************/
	w.openChunk( "HeightMapData", K_HEIGHT_MAP_VERSION_4 );
		w.writeInt( width );
		w.writeInt( height );
		w.writeInt( RMG_BORDER_CELLS );
		w.writeInt( 1 );					// one boundary
		w.writeInt( playable );
		w.writeInt( playable );
		w.writeInt( dataSize );
		w.writeBytes( &heights[0], dataSize );
	w.closeChunk();

	/***************BLEND TILE DATA ***************/
	// Version 6 on purpose: from version 7 on the reader expects the cliff and
	// passability bits in the file, below it works them out from the heights.
	{
		std::vector<Short> tiles( dataSize );
		std::vector<Short> zeroes( dataSize, 0 );

		for( Int y = 0; y < height; y++ )
			for( Int x = 0; x < width; x++ )
				tiles[y * width + x] = tileIndexForCell( x, y, 0, RMG_TEXTURE_WIDTH );

		w.openChunk( "BlendTileData", K_BLEND_TILE_VERSION_6 );
			w.writeInt( dataSize );
			w.writeBytes( &tiles[0], dataSize * sizeof(Short) );
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// blend tiles
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// extra blend tiles
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// cliff info

			w.writeInt( RMG_TEXTURE_TILES );	// bitmap tiles
			w.writeInt( 1 );					// blended tiles: entry 0 is the default
			w.writeInt( 1 );					// cliff infos:   entry 0 is the default

			w.writeInt( 1 );					// one texture class
			w.writeInt( 0 );					// first tile
			w.writeInt( RMG_TEXTURE_TILES );
			w.writeInt( RMG_TEXTURE_WIDTH );
			w.writeInt( 0 );					// legacy field
			w.writeAsciiString( RMG_TEXTURE_NAME );

			w.writeInt( 0 );					// no edge tiles
			w.writeInt( 0 );					// no edge texture classes
		w.closeChunk();
	}

	/***************WORLD DATA ***************/
	// Must come before the sides chunk.
	w.openChunk( "WorldInfo", K_WORLDDICT_VERSION_1 );
		w.beginDict( 2 );
		w.dictInt( "weather", 0 );
		w.dictInt( "compression", 0 );
	w.closeChunk();

	/***************PLAYER DATA ***************/
	// Must come before the object list.
	writeSides( w );

	/***************OBJECTS DATA ***************/
	w.openChunk( "ObjectsList", K_OBJECTS_VERSION_3 );
	{
		Int waypointID = 1;
		for( Int i = 0; i < s.m_numPlayers; i++ )
		{
			AsciiString name;
			name.format( "Player_%d_Start", i + 1 );
			writeWaypoint( w, name.str(), waypointID++,
										 starts[i].m_cellX * MAP_XY_FACTOR, starts[i].m_cellY * MAP_XY_FACTOR );

			for( Int k = 0; k < RMG_SUPPLY_PER_PLAYER; k++ )
			{
				// Off to either side of the line from the middle to the start.
				Real side = (k == 0) ? (PI * 0.5f) : (-PI * 0.5f);
				Real angle = starts[i].m_angle + side;
				Real x = (starts[i].m_cellX + RMG_SUPPLY_DISTANCE * Cos( angle )) * MAP_XY_FACTOR;
				Real y = (starts[i].m_cellY + RMG_SUPPLY_DISTANCE * Sin( angle )) * MAP_XY_FACTOR;

				AsciiString uniqueID;
				uniqueID.format( "SupplyDock %d", i * RMG_SUPPLY_PER_PLAYER + k + 1 );
				writeSupplyDock( w, uniqueID.str(), x, y, starts[i].m_angle + PI );
			}
		}
	}
	w.closeChunk();

	w.finish( mapBytes );
}

//-----------------------------------------------------------------------------
// MemoryChunkInputStream
//-----------------------------------------------------------------------------

MemoryChunkInputStream::MemoryChunkInputStream( const char *data, Int size ) :
	m_data(data), m_size(size), m_pos(0)
{
}

Int MemoryChunkInputStream::read( void *pData, Int numBytes )
{
	if( numBytes > m_size - m_pos )
		numBytes = m_size - m_pos;

	if( pData )
		memcpy( pData, m_data + m_pos, numBytes );

	m_pos += numBytes;
	return numBytes;
}

UnsignedInt MemoryChunkInputStream::tell( void )
{
	return m_pos;
}

Bool MemoryChunkInputStream::absoluteSeek( UnsignedInt pos )
{
	if( (Int)pos > m_size )
		pos = m_size;

	m_pos = pos;
	return TRUE;
}

Bool MemoryChunkInputStream::eof( void )
{
	return m_pos >= m_size;
}
