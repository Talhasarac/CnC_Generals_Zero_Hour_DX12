/*
 * Link-time stand-ins for the pieces of the game that do not live in
 * gameengine.lib.
 *
 * Two kinds of thing end up here:
 *
 *  - The well-known Dict keys and the MapObject list.  Both are *defined* in
 *    GameEngineDevice's WorldHeightMap.cpp (it is the one file that defines
 *    INSTANTIATE_WELL_KNOWN_KEYS), which is DX8 code and not ported yet, so the
 *    keys are instantiated here instead - same macro, same one definition.
 *
 *  - Device/exe callbacks the engine calls out to: the W3D shader manager, the
 *    CD manager, the Win32 message boxes, WinMain.  None of them are reachable
 *    from the tests, so they are stubs; when GameEngineDevice lands in Phase 4
 *    the real definitions take over and this file shrinks.
 */

// must come before anything else that might pull the header in transitively
#define INSTANTIATE_WELL_KNOWN_KEYS
#include "Common/WellKnownKeys.h"

#include "Common/MapObject.h"
#include "Common/OSDisplay.h"
#include "Common/CDManager.h"
#include "Common/GameLOD.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shadow.h"
#include "GameLogic/TerrainLogic.h"

#include <windows.h>

//////////////////////////////////////////////////////////////////////////////
// MapObject - the map file's object list, owned by WorldHeightMap.cpp
//////////////////////////////////////////////////////////////////////////////

MapObject *MapObject::TheMapObjectListPtr = NULL;
Dict MapObject::TheWorldDict;

MapObject::MapObject( Coord3D loc, AsciiString name, Real angle, Int flags,
											const Dict *props, const ThingTemplate *thingTemplate ) :
	m_location(loc), m_objectName(name), m_thingTemplate(thingTemplate), m_angle(angle),
	m_nextMapObject(NULL), m_flags(flags), m_color(0), m_renderObj(NULL),
	m_shadowObj(NULL), m_runtimeFlags(0)
{
	if( props )
		m_properties = *props;
	for( Int i = 0; i < BRIDGE_MAX_TOWERS; i++ )
		m_bridgeTowers[ i ] = NULL;
}

MapObject::~MapObject() {}

void MapObject::setName( AsciiString name ) { m_objectName = name; }
void MapObject::setThingTemplate( const ThingTemplate *thing ) { m_thingTemplate = thing; }
const ThingTemplate *MapObject::getThingTemplate( void ) const { return m_thingTemplate; }

WaypointID MapObject::getWaypointID( void )
{
	Bool exists;
	return (WaypointID)m_properties.getInt( TheKey_waypointID, &exists );
}

AsciiString MapObject::getWaypointName( void )
{
	Bool exists;
	return m_properties.getAsciiString( TheKey_waypointName, &exists );
}

//////////////////////////////////////////////////////////////////////////////
// Device layer
//////////////////////////////////////////////////////////////////////////////

HWND ApplicationHWnd = NULL;
ProjectedShadowManager *TheProjectedShadowManager = NULL;

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

CDManagerInterface *CreateCDManager( void ) { return NULL; }

OSDisplayButtonType OSDisplayWarningBox( AsciiString, AsciiString, UnsignedInt, UnsignedInt )
{
	return OSDBT_ERROR;
}

WindowMsgHandledType MOTDSystem( GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData )
{
	return MSG_IGNORED;
}

Bool testMinimumRequirements( ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq,
															Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex,
															Real *memBenchIndex )
{
	if( videoChipType )		*videoChipType = DC_UNKNOWN;
	if( cpuType )					*cpuType = XX;
	if( cpuFreq )					*cpuFreq = 0;
	if( numRAM )					*numRAM = 0;
	if( intBenchIndex )		*intBenchIndex = 0.0f;
	if( floatBenchIndex )	*floatBenchIndex = 0.0f;
	if( memBenchIndex )		*memBenchIndex = 0.0f;
	return FALSE;
}

void ReloadAllTextures( void ) {}
void oversizeTheTerrain( Int ) {}
void doSkyBoxSet( Bool ) {}

int getQR2HostingStatus( void ) { return 0; }

/* StackDump takes WinMain's address to work out where the exe's own code
   starts; the test is a console app and never gets here. */
int WINAPI WinMain( HINSTANCE, HINSTANCE, LPSTR, int ) { return 0; }
