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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/UserPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/Display.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/Version.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init(void) {}
	virtual void reset(void);
	virtual void update(void) {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = NULL;

void DeepCRCSanityCheck::reset(void)
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X\n", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = NULL;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = NULL;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, AsciiString name, SUBSYSTEM* sys, Xfer *pXfer,  const char* path1 = NULL, 
									 const char* path2 = NULL, const char* dirpath = NULL)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, dirpath, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern CComModule _Module;

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

Int GameEngine::getFramesPerSecondLimit( void )
{
	return m_maxFPS;
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine( void )
{
	// Set the time slice size to 1 ms.
	timeBeginPeriod(1);

	// initialize to non garbage values
	m_maxFPS = 0;
	m_quitting = FALSE;
	m_isActive = FALSE;

	_Module.Init(NULL, ApplicationHInstance);
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	delete TheMapCache;
	TheMapCache = NULL;

//	delete TheShell;
//	TheShell = NULL;

	TheGameResultsQueue->endThreads();

	// Tear the game world down while every subsystem can still see every other one.
	// shutdownAll deletes in reverse registration order, which kills ThePlayerList and
	// TheRadar before TheTeamFactory and TheGameLogic - and Player::~Player NULLs the
	// owning player of each team prototype on its way out, so TeamFactory's own teardown
	// then walked dead pointers and faulted twice on every exit.  Each fault cost a full
	// dbghelp symbolization of an 80MB pdb: that was the several-second stall on quit.
	// resetAll() is the same call made between games, and PlayerList::reset() clears the
	// teams before the players, so afterwards shutdownAll has an empty world to free.
	TheSubsystemList->resetAll();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = NULL;

	delete TheNetwork;
	TheNetwork = NULL;

	delete TheCommandList;
	TheCommandList = NULL;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = NULL;

	delete TheFileSystem;
	TheFileSystem = NULL;

	if (TheGameLODManager)
		delete TheGameLODManager;

	Drawable::killStaticImages();

	_Module.Term();

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif

	// Restore the previous time slice for Windows.
	timeEndPeriod(1);
}

void GameEngine::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("GameEngine::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)\n", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;
}

/** -----------------------------------------------------------------------------------------------
 * -autoskirmish <n>: build a skirmish slot list from the command line and launch it without going
 * through the menus.  This is reallyDoStart() in SkirmishGameOptionsMenu.cpp minus the GUI: the
 * slots only have to say who is occupied and who is an AI, because GameLogic::startNewGame resolves
 * a -1 faction, colour and start position itself (populateRandomSideAndColor,
 * populateRandomStartPosition).  Meant for unattended runs - eight AI players fighting at whatever
 * frame rate the machine gives, with the local slot watching.
 */
static void startAutoSkirmish( void )
{
	AsciiString mapName = TheGlobalData->m_mapName;
	if (mapName.isEmpty())
	{
		DEBUG_LOG(("-autoskirmish: no -map was given\n"));
		return;
	}

	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md == NULL)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not in the map cache\n", mapName.str()));
		return;
	}
	if (!md->m_isMultiplayer)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not a multiplayer map\n", mapName.str()));
		return;
	}

	Int numPlayers = TheGlobalData->m_autoSkirmishPlayers;
	if (numPlayers > md->m_numPlayers)
	{
		DEBUG_LOG(("-autoskirmish: '%s' holds %d players, not %d\n", mapName.str(), md->m_numPlayers, numPlayers));
		numPlayers = md->m_numPlayers;
	}

	if (TheSkirmishGameInfo == NULL)
	{
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	}
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->enterGame();

	/* Watching costs no seat.  GameLogic::startNewGame always adds a "ReplayObserver" side after
		 the slots, and PlayerList::newGame makes the first human side the local player - so a slot
		 list with nothing but AI in it leaves that observer holding the camera, exactly the way a
		 replay does.  Spending a slot on the observer instead would cost a bot, because MAX_SLOTS is
		 8 and that is also the most start positions a map has. */
	const Bool observing = TheGlobalData->m_autoSkirmishObserver;
	UnicodeString localName;
	localName.translate( AsciiString( "Player" ) );
	for( Int i = 0; i < numPlayers; i++ )
	{
		GameSlot slot;
		if (i == 0 && !observing)
		{
			slot.setState( SLOT_PLAYER, localName );
			slot.setName( localName );
			slot.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		}
		else
		{
			slot.setState( (SlotState)TheGlobalData->m_autoSkirmishAIState );
			slot.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		}
		slot.setColor( -1 );			// -1 is "random" to populateRandomSideAndColor
		slot.setStartPos( -1 );		// and to populateRandomStartPosition
		slot.setTeamNumber( -1 );	// -1 is "no team", so everybody fights everybody
		TheSkirmishGameInfo->setSlot( i, slot );
	}
	TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot(0)->getIP() );
	TheSkirmishGameInfo->setMap( mapName );

	/* -seed makes the whole run repeatable: the seed drives the factions, the colours, the start
		 positions and every logic random draw after them, so the same command line replays the same
		 match. */
	const Int seed = (TheGlobalData->m_fixedSeed >= 0) ? TheGlobalData->m_fixedSeed : GetTickCount();
	TheSkirmishGameInfo->setSeed( seed );
	TheSkirmishGameInfo->startGame( 0 );

	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	/* The menu passes the game speed slider's position here, so pass the same thing and clamp it
		 the same way reallyDoStart() does: MSG_NEW_GAME rejects anything outside 1..1000 by falling
		 back to m_framesPerSecondLimit *unclamped*, which is how a stray -fps would still get through.
		 -noFPSLimit deliberately does not appear here - it uncaps the renderer, not the simulation. */
	Int maxFPS = TheGlobalData->m_framesPerSecondLimit;
	if (maxFPS < 15)
		maxFPS = DEFAULT_MAX_FPS;
	if (maxFPS > 1000)
		maxFPS = 1000;

	InitRandom( seed );
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( maxFPS );

	DEBUG_LOG(("-autoskirmish: %d slots on '%s', seed %d, up to %d fps, %s\n",
		numPlayers, mapName.str(), seed, maxFPS,
		observing ? "every slot AI, watching from the free camera" : "slot 0 is the local player"));
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init( void ) {} /// @todo: I changed this to take argc & argv so we can parse those after the GDF is loaded.  We need to rethink this immediately as it is a nasty hack
void GameEngine::init( int argc, char *argv[] )
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================\n"));
	#if defined _DEBUG
			const char *buildType = "Debug";
	#elif defined _INTERNAL
			const char *buildType = "Internal";
	#else
			const char *buildType = "Release";
	#endif
			DEBUG_LOG(("Generals version %s (%s)\n", TheVersion->getAsciiVersion().str(), buildType));
			DEBUG_LOG(("Build date: %s\n", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s\n", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Built by: %s\n", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("================================================================================\n"));
		}
#endif

	#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers.\n"));
		InitPrecisionTimer();
	#endif
	#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////
		
		m_maxFPS = DEFAULT_MAX_FPS;

		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;
		
		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		//Kris: Patch 1.01 - November 17, 2003
		//I was unable to resolve the RTPatch method of deleting a shipped file. English, Chinese, and Korean
		//SKU's shipped with two INIZH.big files. One properly in the Run directory and the other in Run\INI\Data.
		//We need to toast the latter in order for the game to patch properly.
		DeleteFile( "Data\\INI\\INIZH.big" );

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCommandList  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), NULL);


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), NULL); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", MSGNEW("GameEngineSubsystem") GlobalData(), &xferCRC, "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(_DEBUG) || defined(_INTERNAL)
		// If we're in Debug or Internal, load the Debug info as well.
		ini.load( AsciiString( "Data\\INI\\GameDataDebug.ini" ), INI_LOAD_OVERWRITE, NULL );
	#endif
		
		// special-case: parse command-line parameters after loading global data
		parseCommandLine(argc, argv);

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();
		
		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.load( AsciiString( "Data\\INI\\Default\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Default\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After water INI's = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, NULL, NULL, NULL, NULL);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameText = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, NULL); // must be before the game text
		initSubsystem(TheCDManager,"TheCDManager", CreateCDManager(), NULL);
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCDManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", createAudioManager(), NULL);
		if (!TheAudio->isMusicAlreadyLoaded())
			setQuitting(TRUE);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), NULL);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), NULL);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), NULL);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), NULL);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), NULL);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, NULL, "Data\\INI\\Rank.ini");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, NULL, "Data\\INI\\Weapon.ini");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, NULL, "Data\\INI\\Locomotor.ini");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini");
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, NULL, "Data\\INI\\DamageFX.ini");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, NULL, "Data\\INI\\Armor.ini");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheBuildAssistant = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object.ini", NULL, "Data\\INI\\Object");

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheThingFactory = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameClient = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////

	
		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData.ini", "Data\\INI\\AIData.ini");
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(), NULL);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), NULL);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), NULL);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), NULL);
		initSubsystem(TheRadar,"TheRadar", createRadar(), NULL);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), NULL);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheVictoryConditions = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap.ini", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), NULL, fname.str(), "Data\\INI\\CommandMap.ini");

#if defined(_DEBUG) || defined(_INTERNAL)
		ini.load("Data\\INI\\CommandMapDebug.ini", INI_LOAD_MULTIFILE, NULL);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.load("Data\\INI\\CommandMapDemo.ini", INI_LOAD_MULTIFILE, NULL);
#endif


		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), NULL);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), NULL);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, NULL, NULL, NULL );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, NULL, NULL, NULL );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), NULL, NULL, NULL, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X\n", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);
			
		// We're not in a network game yet, so set the network singleton to NULL.
		TheNetwork = NULL;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio 
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// for fingerprinting, we need to ensure the presence of these files


#if !defined(_INTERNAL) && !defined(_DEBUG)
		AsciiString dirName;
    dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsbzh.sec");

    if (dirName.compareNoCase("genseczh.big") != 0)
		{
			DEBUG_LOG(("generalsbzh.sec was not found in genseczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
		
		dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsazh.sec");
		const char *noPath = dirName.reverseFind('\\');
		if (noPath) {
			dirName = noPath + 1;
		}

		if (dirName.compareNoCase("musiczh.big") != 0)
		{
			DEBUG_LOG(("generalsazh.sec was not found in musiczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
#endif


		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(NULL, true, true);
			m_quitting = TRUE;
		}
		
		// load the initial shell screen
		//TheShell->push( AsciiString("Menus/MainMenu.wnd") );
		
		// This allows us to run a map/reply from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
			else if (fname.endsWithNoCase(".rep"))
			{
				TheRecorder->playbackFile(fname);
			}
		}
		else if (TheGlobalData->m_autoSkirmishPlayers > 0)
		{
			startAutoSkirmish();
		}

		// 
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if(!TheGlobalData->m_playIntro)
			TheWritableGlobalData->m_afterIntro = TRUE;

		//initDisabledMasks();
		
	}
	catch (ErrorCode ec)
	{
		/* Every ErrorCode but ERROR_INVALID_D3D used to leave here without a word, and init then ran
			 on to its tail with half its subsystems missing - the first frame faulted on a NULL
			 TheGameLogic and the crash log blamed GameEngine::update() for a failure that happened
			 during startup.  Say which code it was and where it left the engine. */
		DEBUG_LOG(("GameEngine::init - ErrorCode %d escaped initialization (TheGameLogic=%p, TheGameClient=%p)\n",
			(Int)ec, TheGameLogic, TheGameClient));
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if(!TheGlobalData->m_playIntro)
		TheWritableGlobalData->m_afterIntro = TRUE;

	initKindOfMasks();
	initDisabledMasks();
	initDamageTypeFlags();

	TheSubsystemList->resetAll();
	HideControlBar();
}  // end init

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data 
	*/
void GameEngine::reset( void )
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	TheSubsystemList->resetAll();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting NULL TheNetwork!"));
		if (TheNetwork)
			delete TheNetwork;
		TheNetwork = NULL;
	}
	if(background)
	{
		background->destroyWindows();
		background->deleteInstance();
		background = NULL;
	}
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Wall-clock pacing for the logic tick: update() may be called at any render rate, but a logic
 * frame is only "due" every 1000/logicFps milliseconds.
 *
 * A render pass longer than one logic frame owes the simulation more than one tick, and the debt
 * has to be payable or game speed silently becomes render speed - at 20fps a 30Hz match runs a
 * third slow. So the accumulator carries up to LOGIC_CATCHUP_MAX_FRAMES frames of debt and the
 * caller drains it in a loop, skipping the client half while it catches up.
 *
 * The cap is what keeps that from spiralling: when the logic frame itself is what blew the budget,
 * paying the debt would just queue more of the same work. Past the cap the surplus is dropped and
 * the match honestly runs slow - which is what the HUD's 'hz' readout reports.
 *
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
Bool GameEngine_isLogicFrameDue( Real& accumMs, Real elapsedMs, Int logicFps )
{
	if (logicFps <= 0)
		return TRUE;
	const Real msPerLogicFrame = 1000.0f / logicFps;
	const Real maxAccumMs = msPerLogicFrame * LOGIC_CATCHUP_MAX_FRAMES;
	if (elapsedMs > maxAccumMs)
		elapsedMs = maxAccumMs;
	accumMs += elapsedMs;
	if (accumMs > maxAccumMs)
		accumMs = maxAccumMs;
	if (accumMs < msPerLogicFrame)
		return FALSE;
	accumMs -= msPerLogicFrame;
	return TRUE;
}

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 * The client runs as fast as possible; TheGameLogic is limited to a fixed wall-clock
 * framerate (m_maxFPS, the game-speed setting), so game speed does not scale with the
 * render framerate.
 */
#ifdef DEBUG_LOGGING
//
// Frame rate watchdog.  A slow logic frame and a slow render both show up to the player as the same
// stutter, and the logic-side slow-frame log cannot tell them apart because it never sees the client
// half of the loop.  So time both halves of every pass and, once a second, say what the rate was and
// where the time went - but only when the rate actually dropped, so a smooth match logs nothing.
//
static Real engineElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq < 1 )
		return 0.0f;
	return (Real)((double)(to - from) * 1000.0 / (double)freq);
}
#endif

void GameEngine::update( void )
{
	USE_PERF_TIMER(GameEngine_update)
	{
#ifdef DEBUG_LOGGING
		static Int fpsFrames = 0;
		static Real fpsClientTotal = 0.0f, fpsClientMax = 0.0f;
		static Real fpsLogicTotal = 0.0f, fpsLogicMax = 0.0f;
		static Int fpsLogicTicks = 0, fpsCatchupPasses = 0;
		static DWORD fpsWindowStart = timeGetTime();
		Int64 tClientStart, tClientEnd, tLogicStart, tLogicEnd;
		Real clientMS = 0.0f, logicMS = 0.0f;
		Int logicTicks = 0;
		QueryPerformanceCounter( (LARGE_INTEGER *)&tClientStart );
#endif

		{
			
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->UPDATE();

			/// @todo Move audio init, update, etc, into GameClient update
			
			TheAudio->UPDATE();
			TheGameClient->UPDATE();
			TheMessageStream->propagateMessages();

			if (TheNetwork != NULL)
			{
				TheNetwork->UPDATE();
			}
			 
			TheCDManager->UPDATE();
		}
#ifdef DEBUG_LOGGING
		QueryPerformanceCounter( (LARGE_INTEGER *)&tClientEnd );
		clientMS = engineElapsedMS( tClientStart, tClientEnd );
#endif


		// Fast-forward modes run a logic frame on every call; otherwise logic ticks at
		// m_maxFPS Hz of wall clock, however fast the client/renderer above is running.
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		Bool fastMode = TheGlobalData->m_TiVOFastMode;
#else
		Bool fastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif
		fastMode = fastMode || TheTacticalView->getTimeMultiplier() > 1 || TheScriptEngine->isTimeFast();

		static DWORD prevLogicTime = timeGetTime();
		static Real logicAccumMs = 0.0f;
		DWORD now = timeGetTime();
		Real elapsedMs = (Real)(now - prevLogicTime);
		prevLogicTime = now;

		Bool logicFrameDue;
		Bool mayCatchUp = FALSE;
		if (fastMode)
		{
			logicAccumMs = 0.0f;
			logicFrameDue = TRUE;
		}
		else if (TheNetwork != NULL && TheNetwork->isPacingLogicFrames())
		{
			// A network game already has a clock: Network::timeForNewFrame() paces the tick against
			// the negotiated frame rate and only then publishes the frame's commands.  Gating a
			// second time here against m_maxFPS beats two independent clocks against each other -
			// a frame needs both to say yes, so the effective rate settles *below* either one and
			// drifts, which is a systematic multiplayer-only slowdown.  Let the network own it and
			// keep the accumulator clean for when the game drops back to single player.
			logicAccumMs = 0.0f;
			logicFrameDue = TRUE;
		}
		else
		{
			logicFrameDue = GameEngine_isLogicFrameDue(logicAccumMs, elapsedMs, m_maxFPS);
			// Only the wall-clock-paced path has a debt to pay back.  Fast mode and the network
			// clock above both mean exactly one logic frame per call, by their own definition.
			mayCatchUp = (m_maxFPS > 0);
		}

		const Bool logicMayRun =
				((TheNetwork == NULL && !TheGameLogic->isGamePaused()) || (TheNetwork && TheNetwork->isFrameDataReady()));

		if (!logicMayRun)
		{
			// A paused game, or one waiting on the network, is not falling behind - it is stopped.
			// Drop the debt so resuming does not open with a catch-up burst.
			logicAccumMs = 0.0f;
		}
		else if (logicFrameDue)
		{
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tLogicStart );
#endif
			// Pay off the wall clock's debt.  Every pass after the first is a logic frame this call
			// already owes, and it runs without a client pass in front of it: a render frame is what
			// gets dropped so that game speed stays put when the frame rate does not.  Both the loop
			// count and the pacer's own accumulator cap stop at LOGIC_CATCHUP_MAX_FRAMES, so a logic
			// frame that is itself over budget cannot pull the loop into a spiral.
			Int logicTicksThisPass = 0;
			do
			{
				TheGameLogic->UPDATE();
				++logicTicksThisPass;
			}
			while (mayCatchUp && logicTicksThisPass < LOGIC_CATCHUP_MAX_FRAMES &&
						 GameEngine_isLogicFrameDue(logicAccumMs, 0.0f, m_maxFPS));
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tLogicEnd );
			logicMS = engineElapsedMS( tLogicStart, tLogicEnd );
			logicTicks = logicTicksThisPass;
#endif
		}

#ifdef DEBUG_LOGGING
		fpsFrames++;
		fpsClientTotal += clientMS;
		fpsLogicTotal += logicMS;
		fpsLogicTicks += logicTicks;
		if( logicTicks > 1 ) fpsCatchupPasses++;
		if( clientMS > fpsClientMax ) fpsClientMax = clientMS;
		if( logicMS > fpsLogicMax ) fpsLogicMax = logicMS;
		{
			const DWORD nowMS = timeGetTime();
			const DWORD windowMS = nowMS - fpsWindowStart;
			if( windowMS >= 1000 )
			{
				const Real fps = (Real)fpsFrames * 1000.0f / (Real)windowMS;
				if( fps < 50.0f && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
				{
					// 'hz' is the rate the simulation actually achieved and 'catchup' how many render
					// frames were dropped to hold it there; hz short of m_maxFPS with catchup passes
					// present is the logic side being genuinely over budget, not the pacer.
					DEBUG_LOG(("FPS %.0f over %dms (%d frames, %d hz, %d catchup, logic frame %d) | client avg %.1f max %.1f | logic avg %.1f max %.1f | unaccounted %.1fms\n",
										 fps, (Int)windowMS, fpsFrames,
										 (Int)((Real)fpsLogicTicks * 1000.0f / (Real)windowMS + 0.5f), fpsCatchupPasses,
										 TheGameLogic->getFrame(),
										 fpsClientTotal / fpsFrames, fpsClientMax,
										 fpsLogicTotal / fpsFrames, fpsLogicMax,
										 (Real)windowMS - fpsClientTotal - fpsLogicTotal));
				}
				fpsFrames = 0;
				fpsLogicTicks = fpsCatchupPasses = 0;
				fpsClientTotal = fpsLogicTotal = 0.0f;
				fpsClientMax = fpsLogicMax = 0.0f;
				fpsWindowStart = nowMS;
			}
		}
#endif

	}	// end perfGather

}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits. 
 */
void GameEngine::execute( void )
{
	DWORD prevLoopTime = timeGetTime();
#if defined(_DEBUG) || defined(_INTERNAL)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	while( !m_quitting )
	{

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(_DEBUG) || defined(_INTERNAL)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif
			
			{
				try 
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
				catch (...)
				{
					// try to save info off
					try 
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}	// catch
			}	// perf

			{
				// Rendering runs uncapped everywhere now, menus included. Game speed stays
				// constant because the logic tick is paced by wall clock inside update(), and the
				// menus no longer need a capped loop either: AnimateWindowManager::update paces
				// its own stepping off the wall clock, so the window animations keep the cadence
				// they were tuned for however fast the loop runs.
				prevLoopTime = timeGetTime();

		#if defined(_DEBUG) || defined(_INTERNAL)
				// I'm disabling this in internal because many people need alt-tab capability.  If you happen to be
				// doing performance tuning, please just change this on your local system. -MDC
				if (TheTacticalView->getTimeMultiplier()<=1 && !TheScriptEngine->isTimeFast())
					::Sleep(1); // give everyone else a tiny time slice.
		#endif
			}

		}	// perfgather for execute_loop

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}

}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream( void )
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem( void )
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession( void )
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there 
	// are, then we will re-run the compression tool on them.
	
	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.removeLastChar();	// a
		filenameDDS.removeLastChar();	// g
		filenameDDS.removeLastChar();	// t
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh || 
					(infoTGA.timestampHigh == infoDDS.timestampHigh && 
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
extern const Bool TheSystemIsUnicode = (((void*) (::MessageBox)) == ((void*) (::MessageBoxW)));
