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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/Version.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameNetwork/GameInfo.h" // for the SlotState -autoskirmish hands the AI slots

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif



Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int DX8Wrapper_PreserveFPU;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
typedef Int (*FuncPtr)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

static void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString actualpath;

	if ((path.find('\\') == NULL) && (path.find('/') == NULL))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	path.nextToken(&token, "\\/");
	while (!token.endsWithNoCase(".map") && (token.getLength() > 0))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
	}
	// remove the .map from the end.
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();

	// "maps\foo.map" is the short form and grows the directory the map lives in; a path that
	// already names that directory ("maps\foo\foo.map") is left alone instead of growing a third.
	AsciiString dir = token;
	dir.concat('\\');
	if (!actualpath.endsWithNoCase(dir.str()))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
	}
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build\n"));
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowed = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_videoOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		DX8Wrapper_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraFade = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableMilitaryCaption = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noDraw = TRUE;
	}
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
	return 1;
}

#endif // _DEBUG || _INTERNAL

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_audioOn = false;
		TheWritableGlobalData->m_speechOn = false;
		TheWritableGlobalData->m_soundsOn = false;
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowed = false;
	}
	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useShadowVolumes = false;
		TheWritableGlobalData->m_useShadowDecals = false;
	}
	return 1;
}

Int parseMapName(char *args[], int num)
{
	// num is what is *left* on the command line, so the original "== 2" quietly ignored the map
	// whenever another option followed it, and returning 1 left the file name to be re-parsed.
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
		return 2;
	}
	return 1;
}

Int parseXRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* The five synthetic link conditions below used to be debug-build-only, and this fork only ever
	 ships Release, so the switch they drive did not exist in any build anyone could run.  They are
	 the cheapest way to reproduce a lossy or slow link without a second machine on a bad line. */
//=============================================================================
//=============================================================================
Int parseLatencyAverage(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAverage = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyAmplitude(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAmplitude = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyPeriod(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyPeriod = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyNoise(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyNoise = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parsePacketLoss(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_packetLoss = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoDynamicLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableDynamicLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableStaticLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;
	}
	return 1;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useCameraConstraints = FALSE;
	}
	return 1;
}

Int parseWireframe(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_wireframe = TRUE;
	}
	return 1;
}

Int parseShowCollision(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showCollisionExtents = TRUE;
	}
	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showClientPhysics = FALSE;
	}
	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showTerrainNormals = TRUE;
	}
	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_stateMachineDebug = TRUE;
	}
	return 1;
}

Int parseJabber(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_jabberOn = TRUE;
	}
	return 1;
}

Int parseMunkee(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_munkeeOn = TRUE;
	}
	return 1;
}
#endif // defined(_DEBUG) || defined(_INTERNAL)

Int parseScriptDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_scriptDebug = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseParticleEdit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleEdit = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
		TheWritableGlobalData->m_windowed = TRUE;
	}
	return 1;
}


Int parseBuildMapCache(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_buildMapCache = true;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
Int parsePreload( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_preloadAssets = TRUE;
	return 1;
}
#endif


#if defined(_DEBUG) || defined(_INTERNAL) 
Int parseDisplayDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_displayDebug = TRUE;
	}
	return 1;
}

Int parseFile(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}


Int parsePreloadEverything( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_preloadAssets = TRUE;
		TheWritableGlobalData->m_preloadEverything = TRUE;
	}
	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		FILE *logfile=fopen("PreloadedAssets.txt","w");
		if (logfile)	//clear the file
			fclose(logfile);
		TheWritableGlobalData->m_preloadReport = TRUE;
	}
	return 1;
}

/// begin stuff for VTUNE
Int parseVTune ( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_vTune = TRUE;
	return 1;
}
/// end stuff for VTUNE

#endif // defined(_DEBUG) || defined(_INTERNAL)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFX = FALSE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoShroud(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shroudOn = FALSE;
	}
	return 1;
}
#endif

Int parseForceBenchmark(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_forceBenchmark = TRUE;
	}
	return 1;
}

Int parseNoMoveCamera(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoCinematic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
		TheWritableGlobalData->m_disableMilitaryCaption = true;
		TheWritableGlobalData->m_disableCameraFade = true;
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheDebugIgnoreSyncErrors = true;
	}
	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

// Turn terrain collision on for every particle system at once.  The shipped
// ParticleSystem.ini lives inside INIZH.big and a loose copy would have to replace the whole
// file, so this is how the effect gets seen before any INI is written.
// ponytail: a preview switch - the real answer is GroundCollision per system in the INI
Int parseParticleBounce(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleGroundBounce = TRUE;
	}
	return 1;
}

Int parseNoShaders(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_chipSetType = 1;	//force to a voodoo card which uses least amount of features.
	}
	return 1;
}

#if (defined(_DEBUG) || defined(_INTERNAL))
Int parseNoLogo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}
#endif

Int parseNoSizzle( char *args[], int )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_animateWindows = FALSE;
	}
	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseQuickStart( char *args[], int num )
{
#if (defined(_DEBUG) || defined(_INTERNAL))
  parseNoLogo( args, num );
#else
	//Kris: Patch 1.01 -- Allow release builds to skip the sizzle video, but still force the EA logo to show up.
	//This is for legal reasons.
	parseNoSizzle( args, num );
#endif
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_constantDebugUpdate = TRUE;
	}
	return 1;
}

#if (defined(_DEBUG) || defined(_INTERNAL))
Int parseExtraLogging( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_extraLogging = TRUE;
	}
	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allAdvice = TRUE;
	}
	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_showTeamDot = TRUE;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSelectAll( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allowUnselectableSelection = TRUE;
	}
	return 1;
}

Int parseRunAhead( char *args[], Int num )
{
	if (num > 2)
	{
		MIN_RUNAHEAD = atoi(args[1]);
		MAX_FRAMES_AHEAD = atoi(args[2]);
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1)*2;
	}
	return 3;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseAutoSkirmish(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int players = atoi(args[1]);
		if (players < 2)
			players = 2;
		if (players > MAX_SLOTS)
			players = MAX_SLOTS;
		TheWritableGlobalData->m_autoSkirmishPlayers = players;
	}
	return 2;
}

Int parseAIDifficulty(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString difficulty = args[1];
		if (difficulty.compareNoCase("easy") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_EASY_AI;
		else if (difficulty.compareNoCase("medium") == 0 || difficulty.compareNoCase("med") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_MED_AI;
		else
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_BRUTAL_AI;
	}
	return 2;
}

Int parseObserver(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_autoSkirmishObserver = TRUE;
	}
	return 1;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_incrementalAGPBuf = TRUE;
	}
	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_loadScreenDemo = TRUE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveAllStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseLocalMOTD(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseCameraDebug(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_debugCamera = TRUE;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseBenchmark(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_dumpStatsAtInterval = TRUE;
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
	}
	return 2;
}
#endif
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreAsserts(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

/* -fps <n> is the game speed itself: the rate the logic tick is paced at, the command-line twin of
	 the skirmish menu's game speed slider.  MSG_NEW_GAME only honours 1..1000. */

Int parseFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

/* -noFPSLimit uncaps the *renderer*, not the simulation.  It used to raise
	 m_framesPerSecondLimit to 30000 because the old loop ran one logic frame per pass, so the only
	 way to render freely was to let the logic run freely too - and the game then played at whatever
	 speed the machine managed.  GameEngine::update() now paces the logic tick against wall clock
	 and GameEngine::execute() never sleeps for a frame budget, so rendering is already uncapped
	 unconditionally and m_framesPerSecondLimit means game speed and nothing else.  Raising it here
	 would just run the match in fast-forward: superweapon and build timers count real seconds off a
	 clock ticking a thousand times too fast.  Use -fps to ask for that on purpose. */
Int parseNoFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFpsLimit = false;
	}
	return 1;
}

/* -headless is the unattended-run switch: no frame is ever drawn, the logic tick runs flat out
	 instead of being paced against wall clock, audio is silenced, and the process quits by itself
	 the moment the match is decided.  A window and a D3D device are still created - W3D reaches into
	 drawables and the asset manager throughout, so a true null display is a much larger change than
	 skipping the one draw call is worth.  Pair it with -autoskirmish and -observer for a soak run.

	 Audio goes off because at several hundred logic frames a second the game hands the mixer a few
	 thousand events a second that nobody will hear; -headless is for a machine, not a listener. */
Int parseHeadless(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_headless = TRUE;
		TheWritableGlobalData->m_audioOn = FALSE;
		TheWritableGlobalData->m_musicOn = FALSE;
		TheWritableGlobalData->m_soundsOn = FALSE;
		TheWritableGlobalData->m_speechOn = FALSE;
		TheWritableGlobalData->m_videoOn = FALSE;
	}
	return 1;
}

/* -maxframes <n> bounds a headless run in logic frames rather than in seconds, so the cutoff is
	 the same on every machine and in every replay.  A stalemate between eight brutal AIs is a real
	 outcome and it does not end on its own. */
Int parseMaxGameFrames(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_maxGameFrames = atoi(args[1]);
	}
	return 2;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_dumpAssetUsage = true;
	}
	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;
	}
	return 1;
}

Int parseMod(char *args[], Int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString modPath = args[1];
		if (strchr(modPath.str(), ':') || modPath.startsWith("/") || modPath.startsWith("\\"))
		{
			// full path passed in.  Don't append base path.
		}
		else
		{
			modPath.format("%s%s", TheGlobalData->getPath_UserData().str(), args[1]);
		}
		DEBUG_LOG(("Looking for mod '%s'\n", modPath.str()));

		if (!TheLocalFileSystem->doesFileExist(modPath.str()))
		{
			DEBUG_LOG(("Mod does not exist.\n"));
			return 2; // no such file/dir.
		}

		// now check for dir-ness
		struct _stat statBuf;
		if (_stat(modPath.str(), &statBuf) != 0)
		{
			DEBUG_LOG(("Could not _stat() mod.\n"));
			return 2; // could not stat the file/dir.
		}

		if (statBuf.st_mode & _S_IFDIR)
		{
			if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
				modPath.concat('\\');
			DEBUG_LOG(("Mod dir is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modDir = modPath;
		}
		else
		{
			DEBUG_LOG(("Mod file is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modBIG = modPath;
		}

		return 2;
	}
	return 1;
}

static CommandLineParam params[] =
{
	{ "-noshellmap", parseNoShellMap },
	{ "-win", parseWin },
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullscreen", parseNoWin },
	{ "-fullVersion", parseFullVersion },
	{	"-particleEdit", parseParticleEdit },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-mod", parseMod },
	{ "-noshaders", parseNoShaders },
	{ "-particlebounce", parseParticleBounce },
	{ "-quickstart", parseQuickStart },

	{ "-packetloss", parsePacketLoss },
	{ "-latAvg", parseLatencyAverage },
	{ "-latAmp", parseLatencyAmplitude },
	{ "-latPeriod", parseLatencyPeriod },
	{ "-latNoise", parseLatencyNoise },

#if (defined(_DEBUG) || defined(_INTERNAL))
	{ "-noaudio", parseNoAudio },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats }, 
#endif
  { "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },
	{ "-KeepCRCSaves", parseKeepCRCSave },
	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },
	{ "-ClientDeepCRC", parseClientDeepCRC },
	{ "-VerifyClientCRC", parseVerifyClientCRC },
	{ "-LogObjectCRCs", parseLogObjectCRCs },
	{ "-saveAllStats", parseSaveAllStats },
	{ "-NetCRCInterval", parseNetCRCInterval },
	{ "-ReplayCRCInterval", parseReplayCRCInterval },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-useWaveEditor", parseUseWaveEditor },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },
	{ "-file", parseFile },
  
//	{ "-preload", parsePreload },
	
  { "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-ignoreAsserts", parseIgnoreAsserts },
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
	{ "-logToCon", parseLogToConsole },
	{ "-vTune", parseVTune },
	{ "-selectTheUnselectable", parseSelectAll },
	{ "-RunAhead", parseRunAhead },
	{ "-noshroud", parseNoShroud },
	{ "-forceBenchmark", parseForceBenchmark },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-nologo", parseNoLogo },
	{ "-shellmap", parseShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },

#endif

	/* Outside the _DEBUG/_INTERNAL block on purpose: an unattended Release run is driven entirely
		 from the command line.  -autoskirmish needs a map to play, -seed makes the run repeatable,
		 -noFPSLimit lets the renderer run free, and -fps is the one knob that changes how fast the
		 match itself is simulated - the command-line twin of the skirmish menu's game speed slider. */
	{ "-map", parseMapName },
	{ "-seed", parseSeed },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-fps", parseFPSLimit },
	{ "-autoskirmish", parseAutoSkirmish },
	{ "-aidiff", parseAIDifficulty },
	{ "-observer", parseObserver },
	{ "-headless", parseHeadless },
	{ "-maxframes", parseMaxGameFrames },

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
  { "-preload", parsePreload },
#endif


};

// parseCommandLine ===========================================================
/** Parse command-line parameters. */
//=============================================================================
void parseCommandLine(int argc, char *argv[])
{
	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accomodate multi-arg parameters, e.g. "-p 1234".
	int arg=1, param;
	Bool found;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	while (arg<argc)
	{
		// Look at arg #i
		found = false;
		for (param=0; !found && param<sizeof(params)/sizeof(params[0]); ++param)
		{
			int len = strlen(params[param].name);
			int len2 = strlen(argv[arg]);
			if (len2 != len)
				continue;
			if (!strnicmp(argv[arg], params[param].name, len))
			{
				arg += params[param].func(argv+arg, argc-arg);
				found = true;
			}
		}	// for
		if (!found)
		{
			arg++;
		}
	}

	TheArchiveFileSystem->loadMods();
}


