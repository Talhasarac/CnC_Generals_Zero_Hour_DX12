/*
 * GameEngine boot coverage: the Phase 3 checkpoint.
 *
 * Linking gameengine.lib proves nothing on its own, so this brings up the
 * pieces the engine boots first - the memory manager, the name key generator,
 * the file system - and then drives a real INI file through INI::load() and
 * checks the values that came out the other side.
 *
 * TheLocalFileSystem normally lives in GameEngineDevice (Win32LocalFile), which
 * is not ported yet, so the test supplies its own out of LocalFile, which is
 * plain CRT _open/_read and does live in gameengine.
 */
/* crc.h pulls winsock2.h, so it has to come before anything that drags in
   windows.h (and with it winsock.h) - otherwise ws2def.h redefines sockaddr. */
#include "Common/crc.h"

#include "test_harness.h"

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/GameMemory.h"
#include "Common/NameKeyGenerator.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/LocalFile.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/STLTypedefs.h"
#include "Common/StackDump.h"
#include "GameNetwork/Connection.h"
#include "GameLogic/CRCSnapshotRing.h"
#include "GameNetwork/GameDataMatch.h"
#include "GameLogic/FPUControl.h"
#include "GameNetwork/StallJudgement.h"
#include "GameNetwork/KeepAliveSchedule.h"
#include "GameNetwork/CushionMetrics.h"
#include "GameNetwork/LinkSimulation.h"
#include "Common/Energy.h"
#include "Common/RandomValue.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameClient/ClientRandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/SimulationMathCrc.h"
#include "GameLogic/FPUControl.h"
#include <float.h>
#include "Common/AudioRandomValue.h"
#include "GameNetwork/CrcAgreement.h"
#include "Common/RadarShroudCache.h"
#include "GameClient/Gadget.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/GameInfo.h"
#include <float.h>
#include "GameClient/Water.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/IncomingDamage.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "Common/TunnelTracker.h"
#include "Common/StateMachine.h"
#include "Common/XferCRC.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/Player.h"
#include "GameLogic/Module/DefaultProductionExitUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Module/SupplyCenterProductionExitUpdate.h"
#include "WWMath/matrix3d.h"
#include "GameClient/Drawable.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Object.h"
#include "GameClient/CommandXlat.h"
#include "Common/ActionManager.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

//////////////////////////////////////////////////////////////////////////////
// Boot scaffolding
//////////////////////////////////////////////////////////////////////////////

/* LocalFile carries the abstract-base flavour of the pool glue, so it cannot be
   instantiated directly - the device layer subclasses it (Win32LocalFile) purely
   to get a pool.  Same trick here; the base does all the work. */
class TestLocalFile : public LocalFile
{
	/* The pool name has to be one MemoryInit.cpp's size table knows, or
	   createMemoryPool throws ERROR_OUT_OF_MEMORY on the -1 sizes; borrow the
	   device class' entry, since that is the class this stands in for. */
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TestLocalFile, "Win32LocalFile")
public:
	TestLocalFile() : LocalFile() {}
};

TestLocalFile::~TestLocalFile() {}

/* Just enough LocalFileSystem to let INI::load() find a file on disk. */
class TestLocalFileSystem : public LocalFileSystem
{
public:
	virtual void init( void ) {}
	virtual void reset( void ) {}
	virtual void update( void ) {}

	virtual File *openFile( const Char *filename, Int access = 0 )
	{
		TestLocalFile *file = newInstance( TestLocalFile );
		if( file->open( filename, access ) == FALSE )
		{
			file->close();
			return NULL;
		}
		file->deleteOnClose();
		return file;
	}

	virtual Bool doesFileExist( const Char *filename ) const
	{
		FILE *fp = fopen( filename, "rb" );
		if( fp == NULL )
			return FALSE;
		fclose( fp );
		return TRUE;
	}

	virtual void getFileListInDirectory( const AsciiString&, const AsciiString&,
																			 const AsciiString&, FilenameList&, Bool ) const {}
	virtual Bool getFileInfo( const AsciiString&, FileInfo* ) const { return FALSE; }
	virtual Bool createDirectory( AsciiString ) { return FALSE; }
};

/* The subsystems below are globals the engine owns; bring them up once and
   leave them up for the whole run, exactly as GameEngine::init() would. */
static Bool bootOnce( void )
{
	static Bool booted = FALSE;
	if( booted )
		return TRUE;

	initMemoryManager();

	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();

	TheLocalFileSystem = NEW TestLocalFileSystem;
	TheFileSystem = NEW FileSystem;

	booted = TRUE;
	return TRUE;
}

static const char *TEST_INI = "test_gameengine_tmp.ini";

/* INI reports every failure by throwing - either an INIException carrying a
   message or a bare enum code - so run the load behind something that turns
   that back into a check failure with the message attached. */
static Bool loadIni( const char *name )
{
	INI ini;
	try
	{
		ini.load( AsciiString( name ), INI_LOAD_OVERWRITE, NULL );
		return TRUE;
	}
	catch( INIException &e )
	{
		printf( "  INI threw: %s\n", e.mFailureMessage ? e.mFailureMessage : "(no message)" );
	}
	catch( ... )
	{
		/* the enum error codes (INI_UNKNOWN_TOKEN et al) come through here */
		printf( "  INI threw an error code\n" );
	}
	return FALSE;
}

static void writeFile( const char *name, const char *text )
{
	FILE *fp = fopen( name, "wb" );
	CHECK( fp != NULL );
	fwrite( text, 1, strlen( text ), fp );
	fclose( fp );
}

//////////////////////////////////////////////////////////////////////////////
// Memory manager and the string types it hands out
//////////////////////////////////////////////////////////////////////////////

TEST(boot_memory_manager)
{
	CHECK( bootOnce() );

	AsciiString a( "Command & Conquer" );
	CHECK_EQ( a.getLength(), 17 );
	a.concat( ": Generals" );
	CHECK_STR( a.str(), "Command & Conquer: Generals" );

	/* AsciiString is refcounted and copy-on-write; a mutation through one
	   handle must not reach the other. */
	AsciiString b = a;
	b.set( "Zero Hour" );
	CHECK_STR( a.str(), "Command & Conquer: Generals" );
	CHECK_STR( b.str(), "Zero Hour" );

	UnicodeString u;
	u.translate( a );
	CHECK( wcscmp( u.str(), L"Command & Conquer: Generals" ) == 0 );
}

/* winnt.h defines BitTest as the _bittest intrinsic, which takes a LONG*, so if
   windows.h ever gets to redefine the game's macro then every bit-flag test in
   the engine either stops compiling or silently changes meaning.  BaseType.h
   pulls windef.h in first and then takes the name back; this fails to compile
   if that ever regresses, and checks the semantics while it is here. */
TEST(bittest_macro_is_the_games_not_the_intrinsics)
{
	UnsignedInt flags = 0x0A;		// bits 1 and 3

	CHECK( BitTest( flags, 0x02 ) );
	CHECK( BitTest( flags, 0x08 ) );
	CHECK( BitTest( flags, 0x04 ) == 0 );
	/* the intrinsic takes a bit *index*, the macro takes a mask - 0x0A & 0x0A */
	CHECK( BitTest( flags, 0x0A ) );
}

TEST(name_key_generator_round_trips)
{
	CHECK( bootOnce() );

	NameKeyType k1 = TheNameKeyGenerator->nameToKey( "TankGeneralUSA" );
	NameKeyType k2 = TheNameKeyGenerator->nameToKey( "TankGeneralUSA" );
	NameKeyType k3 = TheNameKeyGenerator->nameToKey( "TankGeneralChina" );

	CHECK_EQ( (Int)k1, (Int)k2 );
	CHECK_NE( (Int)k1, (Int)k3 );
	CHECK_STR( TheNameKeyGenerator->keyToName( k1 ).str(), "TankGeneralUSA" );

	/* nameToKey is case sensitive, nameToLowercaseKey is not. */
	CHECK_NE( (Int)k1, (Int)TheNameKeyGenerator->nameToKey( "tankgeneralusa" ) );
	CHECK_EQ( (Int)TheNameKeyGenerator->nameToLowercaseKey( "TankGeneralUSA" ),
						(Int)TheNameKeyGenerator->nameToLowercaseKey( "tankgeneralusa" ) );
}

/* The port aliased hash_map onto unordered_map, which meant supplying
   rts::hash<AsciiString>.  STLport's default hashed the pointer, so equal
   strings in different buffers have to land in the same bucket now. */
TEST(asciistring_hash_is_by_content)
{
	CHECK( bootOnce() );

	char buf[ 32 ];
	strcpy( buf, "AmericaTankCrusader" );

	AsciiString fromLiteral( "AmericaTankCrusader" );
	AsciiString fromBuffer( buf );
	CHECK( fromLiteral.str() != fromBuffer.str() );	// genuinely different storage

	rts::hash<AsciiString> hasher;
	CHECK_EQ( (Int)hasher( fromLiteral ), (Int)hasher( fromBuffer ) );
	CHECK_NE( (Int)hasher( fromLiteral ), (Int)hasher( AsciiString( "AmericaTankPaladin" ) ) );

	std::hash_map< AsciiString, Int, rts::hash<AsciiString>, rts::equal_to<AsciiString> > m;
	m[ fromLiteral ] = 7;
	CHECK_EQ( (Int)m.size(), 1 );
	CHECK_EQ( m[ fromBuffer ], 7 );	// same key, not a second entry
	CHECK_EQ( (Int)m.size(), 1 );
}

//////////////////////////////////////////////////////////////////////////////
// The INI parser
//////////////////////////////////////////////////////////////////////////////

/* A WaterSet block exercises four of the field parsers (AsciiString, Real, Int,
   RGBAColorInt) and needs no subsystem beyond the ones booted above. */
TEST(ini_loads_a_waterset_block)
{
	CHECK( bootOnce() );

	writeFile( TEST_INI,
		"; a comment line\r\n"
		"WaterSet NIGHT\r\n"
		"  SkyTexture = TSNightSky.tga\r\n"
		"  WaterTexture = TWWater01.tga\r\n"
		"  WaterRepeatCount = 5\r\n"
		"  SkyTexelsPerUnit = 0.25\r\n"
		"  UScrollPerMS = 0.5\r\n"
		"  VScrollPerMS = -0.5\r\n"
		"  DiffuseColor = R:12 G:34 B:56 A:78\r\n"
		"End\r\n" );

	WaterSetting &night = WaterSettings[ TIME_OF_DAY_NIGHT ];
	night.m_waterRepeatCount = -1;

	CHECK( loadIni( TEST_INI ) );

	CHECK_STR( night.m_skyTextureFile.str(), "TSNightSky.tga" );
	CHECK_STR( night.m_waterTextureFile.str(), "TWWater01.tga" );
	CHECK_EQ( night.m_waterRepeatCount, 5 );
	CHECK_NEAR( night.m_skyTexelsPerUnit, 0.25f, 0.0001f );
	CHECK_NEAR( night.m_uScrollPerMs, 0.5f, 0.0001f );
	CHECK_NEAR( night.m_vScrollPerMs, -0.5f, 0.0001f );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.red, 12 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.green, 34 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.blue, 56 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.alpha, 78 );

	remove( TEST_INI );
}

/* Blocks are keyed by name, so a second load of the same block has to land on
   the same record - that is how the game's override files work. */
TEST(ini_second_load_overwrites_the_same_record)
{
	CHECK( bootOnce() );

	writeFile( TEST_INI,
		"WaterSet MORNING\r\n"
		"  WaterRepeatCount = 3\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_MORNING ].m_waterRepeatCount, 3 );

	writeFile( TEST_INI,
		"WaterSet MORNING\r\n"
		"  WaterRepeatCount = 9\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_MORNING ].m_waterRepeatCount, 9 );

	remove( TEST_INI );
}

/* A missing file throws rather than returning; the loader has no other way to
   report it. */
TEST(ini_missing_file_throws)
{
	CHECK( bootOnce() );

	Bool threw = FALSE;
	INI ini;
	try
	{
		ini.load( AsciiString( "no_such_file_anywhere.ini" ), INI_LOAD_OVERWRITE, NULL );
	}
	catch( ... )
	{
		threw = TRUE;
	}
	CHECK( threw );
}

/* An unknown block name aborts the whole file - INI::load throws
   INI_UNKNOWN_TOKEN and the blocks after it never get read.  Worth pinning:
   nothing in the loader skips unknown blocks, so an INI written for a newer
   build breaks an older one outright. */
TEST(ini_unknown_block_aborts_the_file)
{
	CHECK( bootOnce() );

	WaterSettings[ TIME_OF_DAY_EVENING ].m_waterRepeatCount = -1;

	writeFile( TEST_INI,
		"ThisBlockTypeDoesNotExist SomeName\r\n"
		"  Whatever = 1\r\n"
		"End\r\n"
		"WaterSet EVENING\r\n"
		"  WaterRepeatCount = 4\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) == FALSE );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_EVENING ].m_waterRepeatCount, -1 );

	remove( TEST_INI );
}

/* ReleaseCrashInfo.txt's stack trace is the only stack this port gets - there is
   no debugger on the porting machine - and it came back empty on the first real
   crash, which is worth catching here rather than in the middle of a launch. */
static char s_stackText[ 4096 ];

static void collectStackLine( const char *line )
{
	strncat( s_stackText, line, sizeof( s_stackText ) - strlen( s_stackText ) - 1 );
}

TEST(stackdump_walks_the_callers)
{
	void *frames[ 12 ];
	memset( frames, 0, sizeof( frames ) );
	::FillStackAddresses( frames, 12, 0 );
	CHECK( frames[ 0 ] != NULL );

	s_stackText[ 0 ] = 0;
	::StackDumpFromAddresses( frames, 12, collectStackLine );
	/* Needs the PDB next to the exe; without symbols this is where it shows. */
	if( strstr( s_stackText, "stackdump_walks_the_callers" ) == NULL )
		printf( "%s\n", s_stackText );
	CHECK( strstr( s_stackText, "stackdump_walks_the_callers" ) != NULL );
}

/* Two __asm blocks in headers everything includes wrote to registers that belong
   to the caller.  fast_float_trunc's "xor ebx,ebx" is what killed every run at the
   main menu: W3DTreeBuffer::doLighting keeps its saved ESP in EBX, so the epilogue's
   "mov esp,ebx" set ESP to zero and the following pop faulted.  The witnesses below
   put a sentinel in each register, run the block, and read the register back. */
static unsigned truncEbxWitness( float f )
{
	unsigned ebxOut;
	volatile float t;
	__asm mov ebx, 0x0BADF00D
	t = fast_float_trunc( f );
	__asm mov ebxOut, ebx
	(void)t;
	return ebxOut;
}

TEST(fast_float_trunc_leaves_ebx_alone)
{
	CHECK_EQ( truncEbxWitness( 3.75f ), 0x0BADF00D );
	CHECK_NEAR( fast_float_trunc( 3.75f ), 3.0f, 0.0001f );
	CHECK_NEAR( fast_float_trunc( -3.75f ), -3.0f, 0.0001f );
}

/* REAL_TO_INT and its siblings used to be fast_float2long_round(fast_float_trunc(x)) - an inline
	 assembly mantissa mask followed by an x87 fld/fistp - and are now a plain cast, which on any
	 SSE2 target is the single cvttss2si instruction that does both.  The claim is that nothing
	 changed except the speed, so the witness runs the two side by side.  The assembly helpers are
	 still in BaseType.h (FAST_REAL_TRUNC and the floor/ceil macros use them), so this is the real
	 old expression, not a re-implementation of it. */

static Int oldRealToInt( Real f )
{
	return (Int)(fast_float2long_round(fast_float_trunc(f)));
}

TEST(real_to_int_agrees_with_the_assembly_it_replaced)
{
	/* The values that decide it: either side of zero, either side of one, exact halves, exact
		 integers, and the ends of the range a 32 bit signed conversion is defined over.  Anything
		 whose magnitude reaches 2^31 is out of range for both spellings and both give INT_MIN, so
		 the sweep stops below it. */
	static const Real kInteresting[] =
	{
		0.0f, -0.0f, 0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f,
		1.0f, -1.0f, 1.5f, -1.5f, 1.9999999f, -1.9999999f,
		2.5f, -2.5f, 3.75f, -3.75f, 30.0f, -30.0f,
		127.5f, -127.5f, 128.0f, -128.0f, 255.9f, -255.9f, 256.0f,
		32767.9f, -32768.0f, 65535.9f, 65536.0f,
		1.0e6f, -1.0e6f, 16777216.0f, -16777216.0f, 16777217.0f,
		1.0e9f, -1.0e9f, 2147483520.0f, -2147483520.0f,
		1.1754944e-38f, -1.1754944e-38f			// smallest normal float, either sign
	};

	for( Int i = 0; i < (Int)(sizeof(kInteresting)/sizeof(kInteresting[0])); ++i )
	{
		Real f = kInteresting[i];
		CHECK_EQ( REAL_TO_INT( f ), oldRealToInt( f ) );

		// the narrowing spellings truncate to Int first, exactly as the long-returning original did
		CHECK_EQ( (Int)REAL_TO_SHORT( f ), (Int)(Short)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDSHORT( f ), (Int)(UnsignedShort)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_BYTE( f ), (Int)(Byte)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDBYTE( f ), (Int)(UnsignedByte)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_CHAR( f ), (Int)(Char)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDINT( f ), (Int)(UnsignedInt)oldRealToInt( f ) );
	}

	/* And a sweep, because a hand-picked list is a hand-picked list.  The generator is a plain LCG
		 over the float's bit pattern, rejected down to the range the conversion is defined over, so
		 the same 20000 values are tested on every machine that runs this. */
	UnsignedInt bits = 0x12345678;
	Int tested = 0;
	for( Int n = 0; n < 400000 && tested < 20000; ++n )
	{
		bits = bits * 1664525u + 1013904223u;

		Real f;
		memcpy( &f, &bits, sizeof(f) );

		// NaNs, infinities and everything at or past 2^31 are out of range for both spellings
		if( !(f == f) || f >= 2147483520.0f || f <= -2147483520.0f )
			continue;

		++tested;
		CHECK_EQ( REAL_TO_INT( f ), oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDBYTE( f ), (Int)(UnsignedByte)oldRealToInt( f ) );
	}

	CHECK_EQ( tested, 20000 );
}

TEST(real_to_int_does_not_care_what_rounding_mode_it_is_called_in)
{
	/* The old spelling's second half was an fld/fistp, and fistp rounds by the FPU's current mode -
		 its own comment says the mode "tends to be left in unpredictable modes by various system bits
		 of code".  It was only safe because the mantissa mask ran first.  A cast has no mode at all,
		 which is one less way for two machines to disagree about the same simulation. */
	const UnsignedInt callersMode = _controlfp( 0, 0 );

	_controlfp( _RC_CHOP, _MCW_RC );
	const Int chop = REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f );

	_controlfp( _RC_UP, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	_controlfp( _RC_DOWN, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	_controlfp( _RC_NEAR, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	// truncation toward zero, in every one of them
	CHECK_EQ( chop, -3 * 1000 + 3 );

	_controlfp( callersMode, _MCW_PC | _MCW_RC );
}

/* The length is a parameter and not a strlen() call on purpose: anything the compiler
   emits between the two blocks below is free to use these registers itself, so the
   witness only stays honest while the call is the only thing in between. */
static void crcRegisterWitness( const char *text, Int len, unsigned *ebxOut, unsigned *esiOut, unsigned *ediOut )
{
	CRC crc;
	__asm
	{
		mov ebx, 0x0BADF00D
		mov esi, 0x0BADBEEF
		mov edi, 0x0BADCAFE
	}
	crc.computeCRC( text, len );
	__asm
	{
		mov eax, ebxOut
		mov dword ptr [eax], ebx
		mov eax, esiOut
		mov dword ptr [eax], esi
		mov eax, ediOut
		mov dword ptr [eax], edi
	}
}

TEST(crc_computecrc_leaves_the_callee_saved_registers_alone)
{
	const char *text = "the quick brown fox";
	unsigned ebxOut = 0, esiOut = 0, ediOut = 0;
	crcRegisterWitness( text, (Int)strlen( text ), &ebxOut, &esiOut, &ediOut );
	CHECK_EQ( ebxOut, 0x0BADF00D );
	CHECK_EQ( esiOut, 0x0BADBEEF );
	CHECK_EQ( ediOut, 0x0BADCAFE );

	/* ...and it still computes what the C++ version in the header's comment does. */
	UnsignedInt expected = 0;
	for( const UnsignedByte *p = (const UnsignedByte *)text; *p; ++p )
	{
		UnsignedInt hibit = ( expected & 0x80000000 ) ? 1 : 0;
		expected <<= 1;
		expected += *p;
		expected += hibit;
	}

	CRC crc;
	crc.computeCRC( text, (Int)strlen( text ) );
	CHECK_EQ( crc.get(), expected );
}

/* Skirmish's Play button (and campaign start) gate on IsFirstCDPresent, which
   is TheFileSystem->areMusicFilesOnCD().  A no-CD layout (Steam) has no disc
   to find but ships the security big next to the exe; the check accepts that
   as the same proof of media instead of prompting for a CD forever. */
TEST(are_music_files_on_cd_accepts_the_local_no_cd_layout)
{
	CHECK( bootOnce() );

	/* No local big and the test's CD manager is the NULL stub: no media. */
	CHECK_EQ( TheFileSystem->areMusicFilesOnCD(), FALSE );

	writeFile( "genseczh.big", "contents are irrelevant, presence is the proof" );
	CHECK_EQ( TheFileSystem->areMusicFilesOnCD(), TRUE );
	remove( "genseczh.big" );
}

/* GameEngine.cpp: rendering is uncapped and the logic tick is paced by wall
   clock instead of by render frames, so game speed must not scale with fps.
   This is the pacing decision function; the statics live in update(). */
extern Bool GameEngine_isLogicFrameDue( Real& accumMs, Real elapsedMs, Int logicFps );

TEST(logic_tick_is_wall_clock_paced_not_render_paced)
{
	/* A 300fps renderer (3.33ms/frame) at 30fps logic: 900 render frames span
	   3 seconds, which must yield 90 logic frames, not 900. */
	Real accum = 0.0f;
	Int due = 0;
	for( Int i = 0; i < 900; ++i )
		if( GameEngine_isLogicFrameDue( accum, 1000.0f / 300.0f, 30 ) )
			++due;
	CHECK( due >= 89 && due <= 91 );

	/* A renderer slower than the logic rate runs exactly one logic frame per
	   render frame (elapsed time is clamped) - never a catch-up burst. */
	accum = 0.0f;
	for( Int i = 0; i < 50; ++i )
		CHECK( GameEngine_isLogicFrameDue( accum, 100.0f, 30 ) );

	/* A non-positive fps never throttles (the -noFPSLimit style dev mode). */
	CHECK( GameEngine_isLogicFrameDue( accum, 0.0f, 0 ) );
}

/* AnimateWindowManager.cpp: the same argument one layer up. The menu animations
   count fixed steps and were authored against a capped frame rate, so with the
   renderer uncapped they have to be paced off the wall clock too or a whole
   menu transition plays out in a handful of milliseconds. Unlike the logic
   pacer this one owns the clock reading, so it takes 'now' rather than an
   elapsed time - a caller can hand it timeGetTime() and nothing else.

   Declared here rather than by including AnimateWindowManager.h: that header
   does not parse outside the engine's own PreRTS.h include order. */
extern Bool GameClient_isUiAnimStepDue( UnsignedInt &lastMs, Real &accumMs,
																				UnsignedInt nowMs, Real stepsPerSec );

TEST(ui_anim_steps_at_a_fixed_rate_however_fast_the_renderer_is)
{
	/* must match UI_ANIM_STEPS_PER_SEC in GameClient/UiAnimClock.h */
	const Real UI_ANIM_STEPS_PER_SEC = 30.0f;

	/* 3 seconds of a 300fps renderer at 30 steps/sec is 90 steps, not 900. */
	UnsignedInt last = 0;
	Real accum = 0.0f;
	Int due = 0;
	UnsignedInt now = 100000;		/* an arbitrary non-zero clock origin */
	for( Int i = 0; i < 900; ++i )
	{
		now += 3;		/* timeGetTime has 1ms resolution, so ~333fps */
		if( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) )
			++due;
	}
	CHECK( due >= 79 && due <= 81 );		/* 2700ms at 33.3ms/step */

	/* The very first call must not fire: with lastMs seeded to now there is no
	   elapsed time yet, so a freshly started animation holds its first frame
	   for a full step instead of jumping two frames on the frame it starts. */
	last = 0;
	accum = 0.0f;
	CHECK_EQ( GameClient_isUiAnimStepDue( last, accum, 500000, UI_ANIM_STEPS_PER_SEC ), FALSE );

	/* A renderer slower than the step rate steps once per call and never banks a
	   burst - a level load must not fast-forward the transition it returns to. */
	last = 0;
	accum = 0.0f;
	now = 200000;
	GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC );	/* seed */
	for( Int i = 0; i < 20; ++i )
	{
		now += 5000;		/* five seconds of stall = 150 steps, if it banked them */
		CHECK( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) );
	}

	/* And the rate really is the rate: half the step period never fires twice. */
	last = 0;
	accum = 0.0f;
	now = 300000;
	GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC );
	due = 0;
	for( Int i = 0; i < 60; ++i )
	{
		now += 16;		/* ~60fps against a 33.3ms step */
		if( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) )
			++due;
	}
	CHECK( due >= 28 && due <= 30 );		/* 960ms of it */
}

/* PhysicsUpdate.cpp: the forward speed a locomotor steers on is the projection
   of the velocity onto the facing - a plain dot product. It used to be
   sqrt((vx*dx)^2 + (vy*dy)^2), which is exact on the axes but reads only
   sqrt(cos^4 + sin^4) = 0.707 of the true speed at 45 degrees. Since the
   locomotors close speedDelta = goalSpeed - actualSpeed by accelerating, and
   nothing else caps velocity, an under-read made diagonal units settle at
   goalSpeed/0.707 - up to 1.41x their max speed.

   The witness is heading independence: drive at a known speed along the
   facing and the reported forward speed must be that speed at every heading. */
TEST(physics_forward_speed_is_the_projection_not_a_per_axis_norm)
{
	const Real speed = 40.0f;

	for( Int deg = 0; deg <= 360; deg += 5 )
	{
		Real a = (Real)(deg * PI / 180.0);
		Coord3D dir; dir.x = (Real)cos(a); dir.y = (Real)sin(a); dir.z = 0.0f;
		Coord3D vel; vel.x = dir.x * speed; vel.y = dir.y * speed; vel.z = 0.0f;

		/* moving along the facing: full speed, whatever the heading. The old
		   code returned 0.707*speed here at 45/135/225/315 degrees. */
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( vel, dir ), speed, 0.01f );

		/* moving backwards along the facing: the same speed, negated. */
		Coord3D back; back.x = -vel.x; back.y = -vel.y; back.z = 0.0f;
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( back, dir ), -speed, 0.01f );

		/* moving straight across the facing contributes nothing. The old code
		   returned a positive magnitude here for every off-axis heading. */
		Coord3D side; side.x = -dir.y * speed; side.y = dir.x * speed; side.z = 0.0f;
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( side, dir ), 0.0f, 0.01f );
	}

	/* the 3d form is the same dot product, z included. */
	Coord3D up; up.x = 0.0f; up.y = 0.0f; up.z = 1.0f;
	Coord3D climb; climb.x = 0.0f; climb.y = 0.0f; climb.z = 12.0f;
	CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( climb, up ), 12.0f, 0.01f );
}

/* CommandXlat.cpp: ctrl is the force fire modifier, but while the attack move cursor is up it
   means "one shared pace" for the group instead.  The click dispatch used to read the raw ctrl
   state, so A then ctrl+click fired at the ground rather than issuing the attack move. */
extern Bool CommandXlat_isForceAttackTargeting( Bool ctrlHeld, Bool attackMoveArmed );

TEST(ctrl_is_force_fire_only_while_the_attack_move_cursor_is_down)
{
	/* plain ctrl+click: force fire, as in retail. */
	CHECK(  CommandXlat_isForceAttackTargeting( true,  false ) );

	/* attack move armed: ctrl is the group speed modifier, so nothing force fires. */
	CHECK( !CommandXlat_isForceAttackTargeting( true,  true ) );

	/* no ctrl at all, either way. */
	CHECK( !CommandXlat_isForceAttackTargeting( false, false ) );
	CHECK( !CommandXlat_isForceAttackTargeting( false, true ) );
}

/* AssaultTransportAIUpdate.cpp: the troop crawler deploys its passengers at a target and used to
   leave them walking behind it for the rest of the attack move once that target died. */
extern Bool AssaultTransport_shouldRetrieveMembers( Bool membersOutside, Bool membersFighting, Bool areaClear );
extern Bool AssaultTransport_waitingForBoarding( Bool membersOutside, UnsignedInt framesRemaining );

TEST(troop_crawler_picks_its_troops_back_up_only_when_the_fight_is_over)
{
	/* deployed, nobody shooting, nothing hostile in range: climb back in. */
	CHECK( AssaultTransport_shouldRetrieveMembers( true, false, true ) );

	/* a member still has a victim - leave them to it. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( true, true, true ) );

	/* nobody has a target yet, but something hostile is still in range: wait for them to engage. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( true, false, false ) );

	/* everybody is already aboard: nothing to order. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( false, false, true ) );
}

TEST(troop_crawler_waits_for_boarding_but_not_forever)
{
	/* troops outside and time on the clock: hold position while they board. */
	CHECK( AssaultTransport_waitingForBoarding( true, 300 ) );
	CHECK( AssaultTransport_waitingForBoarding( true, 1 ) );

	/* the wait ran out - roll on rather than stalling on a member that cannot get back. */
	CHECK( !AssaultTransport_waitingForBoarding( true, 0 ) );

	/* all aboard: continue the attack move immediately. */
	CHECK( !AssaultTransport_waitingForBoarding( false, 300 ) );
}

/* AIStates.cpp: an attack move leashes a human player's ground unit so a target
   that backs away cannot walk it off the order.  Aircraft must be exempt: they
   acquire out to weapon range, which is far beyond the leash, so they broke it on
   the way in every time and disengaged before ever being in range to fire - which
   also meant they never spent the ammunition that sends them home to reload. */
extern Bool AIAttackMove_leashBroken( Bool isHumanPlayer, Bool isAirborne, Real dx, Real dy, Int leashCells );

TEST(attack_move_leashes_ground_units_but_never_aircraft)
{
	/* PATHFIND_CELL_SIZE_F is 10 and ATTACK_MOVE_LEASH_CELLS is 12, so the leash
	   is 120 world units for a human player's ground unit. */
	const Real cell = 10.0f;
	const Int LEASH = 12;		/* ATTACK_MOVE_LEASH_CELLS, which is protected on the state */

	/* a ground unit: inside the leash it keeps fighting, past it the fight is off. */
	CHECK( !AIAttackMove_leashBroken( true, false, 0.0f,       0.0f,       LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, false, 11.0f*cell, 0.0f,       LEASH ) );
	CHECK(  AIAttackMove_leashBroken( true, false, 13.0f*cell, 0.0f,       LEASH ) );
	CHECK(  AIAttackMove_leashBroken( true, false, 0.0f,       13.0f*cell, LEASH ) );
	/* measured as a radius, not per axis */
	CHECK(  AIAttackMove_leashBroken( true, false, 10.0f*cell, 10.0f*cell, LEASH ) );

	/* an aircraft is never leashed, however far the fight has taken it - this is
	   the case that used to fire on the way in to every single target. */
	CHECK( !AIAttackMove_leashBroken( true, true, 13.0f*cell,  0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, true, 100.0f*cell, 0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, true, 500.0f*cell, 500.0f*cell, LEASH ) );

	/* the computer and scripts keep retail chase behaviour: never leashed here,
	   airborne or not. */
	CHECK( !AIAttackMove_leashBroken( false, false, 500.0f*cell, 0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( false, true,  500.0f*cell, 0.0f, LEASH ) );
}

/* AIStates.cpp: telling a firing pass from a failed approach when the attack move
   disengages.  A fight the unit spent rounds on was real however short; only one it
   never fired in is charged the re-acquire delay.  The ammunition count is the state
   that was left uninitialized and unsaved - see the zero cases below. */
extern Bool AIAttackMove_engageWasADud( Bool victimStillAlive, Int ammoAtEngage, Int ammoNow,
																				UnsignedInt engageStartFrame, UnsignedInt now, Int dudFrames );

TEST(attack_move_charges_the_reacquire_delay_only_for_a_fight_that_never_happened)
{
	const Int DUD = 15;		/* ATTACK_MOVE_DUD_ENGAGE_FRAMES, protected on the state */

	/* stood next to it for a moment, fired nothing, it is still alive: a dud. */
	CHECK( AIAttackMove_engageWasADud( true, 8, 8, 1000, 1000, DUD ) );
	CHECK( AIAttackMove_engageWasADud( true, 8, 8, 1000, 1014, DUD ) );

	/* the same non-fight, but long enough that it was a real attempt, not a bounce. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 1015, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 9999, DUD ) );

	/* it died. whatever we did worked, so go straight back to scanning. */
	CHECK( !AIAttackMove_engageWasADud( false, 8, 8, 1000, 1000, DUD ) );

	/* one round spent inside two frames is the aircraft firing pass: a real fight,
	   and charging it a re-acquire delay is exactly backwards - the load is what
	   sends the aircraft home, so it should be spent as fast as it can be. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 7, 1000, 1002, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 0, 1000, 1001, DUD ) );

	/* an ammunition count of zero at engage time can never read as "we fired": the
	   count now is a sum of remaining rounds and is never negative.  That is what a
	   save written before the count existed, and the constructor, both load as - so
	   the answer there is the conservative "dud", not whatever was in the block. */
	CHECK( AIAttackMove_engageWasADud( true, 0, 0, 1000, 1000, DUD ) );
	CHECK( AIAttackMove_engageWasADud( true, 0, 12, 1000, 1000, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 0, 12, 1000, 1015, DUD ) );

	/* a unit with no ammunition at all on either side of a fight it could not start. */
	CHECK( AIAttackMove_engageWasADud( true, 0, 0, 0, 0, DUD ) );

	/* both frames are unsigned: an engage stamped after 'now' must not wrap the
	   subtraction into a small number and read as a fresh dud. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 999, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 0xffffffff, 0, DUD ) );
}

/* AIStates.cpp: on attack move, whoever is shooting us wins the target selection
   over whatever the scan would otherwise have picked. */
extern Bool AIAttackMove_shouldRetaliate( UnsignedInt lastDamageFrame, UnsignedInt now, Int windowFrames,
																					Bool alreadyFightingTheAttacker, Bool canAttackTheAttacker );

TEST(attack_move_turns_on_whoever_is_shooting_it)
{
	const Int WINDOW = 30;		/* ATTACK_MOVE_RETALIATE_FRAMES, protected on the state */

	/* hit this second, can shoot back, busy with somebody else: turn on the shooter. */
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, false, true ) );
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1029, WINDOW, false, true ) );
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1030, WINDOW, false, true ) );

	/* an old grudge is not a fight. */
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1031, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 9999, WINDOW, false, true ) );

	/* already fighting them, or cannot touch them: leave the current target alone,
	   otherwise the fight restarts every scan and nothing is ever shot. */
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, true,  true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, false, false ) );

	/* the two timestamps that mean "never hit". Both are unsigned, and 0xffffffff
	   plus the window wraps to a small number - read naively, a unit that has never
	   been damaged would retaliate against whatever object id happened to be there. */
	CHECK( !AIAttackMove_shouldRetaliate( 0, 10, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0, 0, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0xffffffff, 10, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0xffffffff, 0xfffffff0, WINDOW, false, true ) );
}

//////////////////////////////////////////////////////////////////////////////
// Particle ground collision
//
// particleGroundBounce is the whole of the terrain collision added to
// Particle::update.  It touches no subsystem, so it can be driven directly.
//////////////////////////////////////////////////////////////////////////////

static Coord3D partCoord(Real x, Real y, Real z) { Coord3D c; c.set(x, y, z); return c; }

TEST(particle_above_the_ground_is_left_alone)
{
	Coord3D pos = partCoord(0.0f, 0.0f, 10.0f);
	Coord3D vel = partCoord(1.0f, 0.0f, -2.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(!particleGroundBounce(&pos, &vel, 5.0f, &up, 0.5f, 0.5f));
	CHECK_NEAR(pos.z, 10.0f, 1e-5f);
	CHECK_NEAR(vel.z, -2.0f, 1e-5f);
}

TEST(particle_bounces_off_flat_ground_and_keeps_the_restitution)
{
	Coord3D pos = partCoord(0.0f, 0.0f, 4.0f);
	Coord3D vel = partCoord(3.0f, 0.0f, -10.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 5.0f, &up, 0.5f, 0.8f));
	CHECK_NEAR(pos.z, 5.0f, 1e-5f);
	CHECK_NEAR(vel.z, 5.0f, 1e-5f);
	CHECK_NEAR(vel.x, 2.4f, 1e-5f);
	CHECK_NEAR(vel.y, 0.0f, 1e-5f);
}

TEST(particle_with_zero_bounce_stops_dead_and_slides)
{
	Coord3D pos = partCoord(0.0f, 0.0f, -1.0f);
	Coord3D vel = partCoord(4.0f, 0.0f, -6.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &up, 0.0f, 0.5f));
	CHECK_NEAR(vel.z, 0.0f, 1e-5f);
	CHECK_NEAR(vel.x, 2.0f, 1e-5f);
}

TEST(particle_on_a_slope_is_deflected_downhill_not_straight_up)
{
	// A 45 degree face whose normal leans towards +x, so downhill is +x.  Dropped straight
	// down onto it, the particle must carry on down the slope - not stop, and not be thrown
	// back up the way a flat Z flip would throw it.
	const Real k = 0.70710678f;
	Coord3D pos = partCoord(0.0f, 0.0f, -1.0f);
	Coord3D vel = partCoord(0.0f, 0.0f, -10.0f);
	const Coord3D slope = partCoord(k, 0.0f, k);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &slope, 0.0f, 1.0f));
	CHECK_NEAR(vel.x, 5.0f, 1e-4f);		// pushed downhill
	CHECK_NEAR(vel.z, -5.0f, 1e-4f);	// and still going down, at the slope's angle
}

TEST(particle_already_leaving_the_surface_is_only_lifted_clear)
{
	// the resting case: gravity has just dragged it a hair under, but it is moving away
	Coord3D pos = partCoord(0.0f, 0.0f, -0.01f);
	Coord3D vel = partCoord(1.0f, 0.0f, 2.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &up, 0.5f, 0.5f));
	CHECK_NEAR(pos.z, 0.0f, 1e-5f);
	CHECK_NEAR(vel.z, 2.0f, 1e-5f);
	CHECK_NEAR(vel.x, 1.0f, 1e-5f);
}

//////////////////////////////////////////////////////////////////////////////
// The ground blob under a particle system
//
// particleShadowBlob* is the whole of the decision: which systems earn a soft
// shadow on the terrain, where it goes, how big it is and how dark.  Pure
// arithmetic over the live particles, so it can be driven directly.
//////////////////////////////////////////////////////////////////////////////

static void blobFeed(ParticleShadowBlob *blob, Int count, Real size, Real alpha)
{
	for (Int i = 0; i < count; i++)
		particleShadowBlobAdd(blob, (Real)i, 0.0f, size, alpha);
}

TEST(blob_reset_leaves_nothing_to_resolve)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	CHECK_EQ(blob.m_count, 0);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_bullet_trail_because_its_particles_are_tiny)
{
	// plenty of particles, all of them a couple of units across - this is the case that
	// keeps trails, sparks and muzzle flashes from staining the ground
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 40, 3.0f, 1.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_puff_of_one_or_two_particles)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 2, 50.0f, 1.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_cloud_that_has_faded_to_nothing)
{
	// big particles, enough of them, but no alpha left: a decal here would be a black
	// smear under smoke that is no longer drawn
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 10, 60.0f, 0.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_centres_on_the_particles_and_covers_their_spread_plus_one_particle)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	particleShadowBlobAdd(&blob, 100.0f, 200.0f, 20.0f, 0.5f);
	particleShadowBlobAdd(&blob, 140.0f, 200.0f, 30.0f, 0.5f);
	particleShadowBlobAdd(&blob, 120.0f, 260.0f, 10.0f, 0.5f);

	CHECK(particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
	CHECK_NEAR(x, 120.0f, 1e-4f);					// midpoint of 100..140
	CHECK_NEAR(y, 230.0f, 1e-4f);					// midpoint of 200..260
	CHECK_NEAR(sx, 40.0f + 30.0f, 1e-4f);	// spread plus the biggest particle's width
	CHECK_NEAR(sy, 60.0f + 30.0f, 1e-4f);
	CHECK(op > 0);
}

TEST(blob_darkens_with_more_smoke_then_saturates_below_opaque)
{
	ParticleShadowBlob thin, thick, absurd;
	Real x, y, sx, sy;
	Int thinOp, thickOp, absurdOp;

	particleShadowBlobReset(&thin);
	blobFeed(&thin, 4, 40.0f, 0.25f);
	CHECK(particleShadowBlobResolve(&thin, &x, &y, &sx, &sy, &thinOp));

	particleShadowBlobReset(&thick);
	blobFeed(&thick, 8, 40.0f, 1.0f);		// past the saturation point
	CHECK(particleShadowBlobResolve(&thick, &x, &y, &sx, &sy, &thickOp));

	particleShadowBlobReset(&absurd);
	blobFeed(&absurd, 200, 40.0f, 1.0f);
	CHECK(particleShadowBlobResolve(&absurd, &x, &y, &sx, &sy, &absurdOp));

	CHECK(thinOp < thickOp);
	CHECK(thickOp <= absurdOp);
	CHECK(absurdOp < 255);		// smoke shades the ground, it never blacks it out
	CHECK_EQ(thickOp, absurdOp);	// and past saturation more smoke changes nothing
}

TEST(blob_never_smears_wider_than_the_cap)
{
	// a wind-blown system whose particles have drifted right across the map
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	particleShadowBlobAdd(&blob, 0.0f, 0.0f, 40.0f, 1.0f);
	particleShadowBlobAdd(&blob, 5000.0f, 4000.0f, 40.0f, 1.0f);
	particleShadowBlobAdd(&blob, 2500.0f, 2000.0f, 40.0f, 1.0f);

	CHECK(particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
	CHECK(sx <= 300.0f);
	CHECK(sy <= 300.0f);
}

// ---------------------------------------------------------------------------------------------
// The HUD income estimate (InGameUI.cpp).  It samples the score keeper's cumulative earnings
// into a ring of buckets and averages over every bucket it holds.
// ---------------------------------------------------------------------------------------------
extern Int computeIncomePerMinute( const Int *samples, UnsignedInt ringSize, UnsignedInt count, Int sampleSeconds );

TEST(income_one_bucket_is_not_a_rate_yet)
{
	const Int samples[4] = { 500, 0, 0, 0 };
	CHECK_EQ(computeIncomePerMinute(samples, 4, 1, 2), -1);
}

TEST(income_averages_over_the_buckets_held)
{
	// two 2s buckets, 100 earned over them -> 100 per 2s -> 3000 a minute
	const Int samples[4] = { 1000, 1050, 1100, 0 };
	CHECK_EQ(computeIncomePerMinute(samples, 4, 2, 2), 1500);
	CHECK_EQ(computeIncomePerMinute(samples, 4, 3, 2), 1500);
}

TEST(income_window_slides_once_the_ring_wraps)
{
	// ring of 4, so at most 3 buckets (6s) are spanned.  Earnings: 0,10,20,30 then a 300 jump.
	Int samples[4] = { 0, 10, 20, 30 };
	CHECK_EQ(computeIncomePerMinute(samples, 4, 4, 2), 300);		// (30-0) over 6s

	samples[4 % 4] = 330;																			// bucket 4 lands on index 0
	CHECK_EQ(computeIncomePerMinute(samples, 4, 5, 2), 3200);	// (330-10) over 6s, bucket 0 gone
}

TEST(income_is_zero_not_negative_when_nothing_comes_in)
{
	// cumulative earnings never fall, so a dry spell reads as a real zero
	const Int samples[4] = { 7000, 7000, 7000, 7000 };
	CHECK_EQ(computeIncomePerMinute(samples, 4, 4, 2), 0);
}

/*
 * BaseType.h's fast_float_ceil does not ceil an integer.  It adds 0.99999994 and
 * truncates, but 0.99999994 is only a sixteenth of a float ULP once the value is past
 * 1.0, so the sum rounds up to the next whole float before the truncate ever runs:
 * ceil(600) is 601, ceil(1080) is 1081, and so on for every positive whole number.
 * Fractions - what it is normally handed - come out right, which is why this has
 * survived.  Every REAL_TO_INT_CEIL in the codebase inherits it.
 *
 * Pinned rather than fixed: the sum is in a header every translation unit includes and
 * a correcting compare would cost the branch the whole routine exists to avoid, so the
 * blast radius of changing it is the entire game.  Callers that need an exact edge floor
 * and pin it instead - a screen height that ceils to one past the bottom of the screen is
 * where this turned up.
 */
TEST(realtointceil_DEFECT_overshoots_every_whole_number)
{
	CHECK_EQ(REAL_TO_INT_CEIL(1.0f), 2);
	CHECK_EQ(REAL_TO_INT_CEIL(600.0f), 601);
	CHECK_EQ(REAL_TO_INT_CEIL(768.0f), 769);
	CHECK_EQ(REAL_TO_INT_CEIL(1080.0f), 1081);
	CHECK_EQ(REAL_TO_INT_CEIL(1920.0f), 1921);

	// zero is the one whole number it gets right - 0.99999994 truncates back to it
	CHECK_EQ(REAL_TO_INT_CEIL(0.0f), 0);

	// genuine fractions round up correctly, which is what callers normally feed it
	CHECK_EQ(REAL_TO_INT_CEIL(600.5f), 601);
	CHECK_EQ(REAL_TO_INT_CEIL(1080.25f), 1081);

	// negatives skip the addition entirely and truncate, which really is ceil
	CHECK_EQ(REAL_TO_INT_CEIL(-600.0f), -600);
	CHECK_EQ(REAL_TO_INT_CEIL(-600.5f), -600);
}

//////////////////////////////////////////////////////////////////////////////
// The in-flight damage ledger
//////////////////////////////////////////////////////////////////////////////

/*
 * A delayed shot - a projectile in flight, or a hitscan weapon far enough away that its
 * damage is scheduled for a later frame - used to leave no trace anywhere between the
 * muzzle and the impact.  Every other unit went on reading full health off the victim and
 * kept firing, so a squad routinely spent a whole volley killing something the first two
 * shots had already killed.  IncomingDamageTracker books each such shot against its victim
 * and auto-targeting reads it back.
 *
 * These drive the ledger with hand-picked frame numbers, which is why the frame is a
 * parameter rather than a read of TheGameLogic - there is no game logic in this binary.
 */

static const ObjectID VICTIM_A = (ObjectID)101;
static const ObjectID VICTIM_B = (ObjectID)102;
static const ObjectID SHOOTER_1 = (ObjectID)201;
static const ObjectID SHOOTER_2 = (ObjectID)202;

TEST(incomingdamage_books_and_sums_per_victim)
{
	IncomingDamageTracker::reset();
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 25.0f, 100, 112);
	IncomingDamageTracker::bookShot(VICTIM_B, SHOOTER_1, 10.0f, 100, 110);

	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 65.0f, 0.001f);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_B), 10.0f, 0.001f);

	IncomingDamageTracker::reset();
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
}

TEST(incomingdamage_doomed_only_once_the_booking_covers_the_health)
{
	IncomingDamageTracker::reset();

	// nothing booked: never doomed, however little health is left
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 100.0f));
	CHECK(IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 40.0f));	// exactly lethal counts

	// a second shooter's shell tips it over: this is the overkill the ledger exists to stop
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 70.0f, 100, 112);
	CHECK(IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 100.0f));

	// and the victim next to it is unaffected
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_B, 1.0f));

	IncomingDamageTracker::reset();
}

TEST(incomingdamage_landing_releases_one_booking_from_that_shooter)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 101, 111);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 25.0f, 100, 112);

	// one landing releases one shot, not the shooter's whole account
	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 65.0f, 0.001f);

	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 25.0f, 0.001f);

	// a landing nobody booked is not an error, and takes nothing with it
	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	IncomingDamageTracker::shotLanded(VICTIM_B, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 25.0f, 0.001f);

	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_2);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::reset();
}

/*
 * The reservation has to lapse on its own, or a missile shot down by a point defence - or
 * lured away by countermeasures - would reserve its target forever and the squad would
 * stand there holding its fire.
 */
TEST(incomingdamage_booking_lapses_after_the_impact_it_predicted)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);

	// still honored while the shot is plausibly in the air
	IncomingDamageTracker::update(110);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 40.0f, 0.001f);

	// ...and released once the impact is well past and nothing landed
	IncomingDamageTracker::update(200);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::reset();
}

/*
 * An impact frame that has already gone by (or is this very frame) must not wrap the
 * unsigned subtraction into a four-billion-frame reservation.
 */
TEST(incomingdamage_impact_in_the_past_still_expires)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 500, 400);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 40.0f, 0.001f);

	IncomingDamageTracker::update(600);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::reset();
}

/* A shot that would do nothing is not worth reserving a target over. */
TEST(incomingdamage_ignores_harmless_shots)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 0.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, -5.0f, 100, 110);
	IncomingDamageTracker::bookShot(INVALID_ID, SHOOTER_1, 40.0f, 100, 110);

	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::reset();
}

//////////////////////////////////////////////////////////////////////////////
// Drag-to-aim building placement
//////////////////////////////////////////////////////////////////////////////

/*
 * The 45 degree snap used to pick its rounding by sign: floor(x + 0.5) above zero but
 * floor(x - 0.5) below it, which rounds *away* from zero.  REAL_TO_INT_FLOOR is a true
 * floor, so the second branch pushed every negative heading a whole step out - and
 * Coord2D::toAngle returns -PI..PI, so that is half the circle.  Dragging into it meant
 * fighting the snap: the building faced 45 degrees past where the mouse was pointing.
 */
TEST(placement_snap_takes_the_nearest_45_on_both_halves_of_the_circle)
{
	const Real step = PI / 4.0f;

	/* dead on a spoke stays put */
	for (Int i = -4; i <= 4; i++)
		CHECK_NEAR(InGameUI::snapAngleTo45(i * step), i * step, 0.0001f);

	/* just short of a spoke rounds up to it, just past rounds back down to it - same both signs */
	CHECK_NEAR(InGameUI::snapAngleTo45(step - 0.1f), step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(step + 0.1f), step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-step - 0.1f), -step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-step + 0.1f), -step, 0.0001f);

	/* the old sign branch failed exactly here: -0.2 rad is nearest to 0, not to -45 degrees */
	CHECK_NEAR(InGameUI::snapAngleTo45(-0.2f), 0.0f, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-1.0f), -step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-2.0f), -3.0f * step, 0.0001f);	/* -114.6 deg is nearer -135 than -90 */

	/* nothing is ever more than half a step of mouse travel from its snap */
	for (Int deg = -180; deg <= 180; deg += 3)
	{
		Real angle = deg * PI / 180.0f;
		Real snapped = InGameUI::snapAngleTo45(angle);

		CHECK(fabsf(snapped - angle) <= step / 2.0f + 0.0001f);

		/* and it really is a multiple of 45 degrees */
		CHECK_NEAR(snapped / step, (Real)REAL_TO_INT_FLOOR(snapped / step + 0.5f), 0.0001f);
	}
}

/*
 * GridBuildPlacement.  The grid is the pathfinder's, 10 world units a cell, and what has to
 * land on it is the footprint's *edges*, not its centre: an odd number of cells wide means
 * the centre sits in the middle of a cell, an even number means it sits on the line between
 * two.  Get that backwards and every second structure straddles a cell it only half fills,
 * which is the gap-you-cannot-walk-through this snap exists to remove.
 */
TEST(command_availability_rank_is_not_the_enum_order)
{
	/* the whole point of the helper: the enum is declared RESTRICTED, AVAILABLE, ACTIVE, HIDDEN,
	 * NOT_READY, CANT_AFFORD, so taking the numeric max over a multi-selection would rank
	 * "cannot afford" above "available" and "hidden" above both */
	CHECK((Int)COMMAND_HIDDEN > (Int)COMMAND_AVAILABLE);
	CHECK((Int)COMMAND_CANT_AFFORD > (Int)COMMAND_AVAILABLE);

	/* permissiveness, most to least */
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_ACTIVE) >
	      ControlBar::commandAvailabilityRank(COMMAND_AVAILABLE));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_AVAILABLE) >
	      ControlBar::commandAvailabilityRank(COMMAND_CANT_AFFORD));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_CANT_AFFORD) >
	      ControlBar::commandAvailabilityRank(COMMAND_NOT_READY));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_NOT_READY) >
	      ControlBar::commandAvailabilityRank(COMMAND_RESTRICTED));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_RESTRICTED) >
	      ControlBar::commandAvailabilityRank(COMMAND_HIDDEN));

	/* a group of four barracks where only the last can still buy the upgrade: the button is
	 * offered, because the best answer in the group wins */
	CommandAvailability group[] = { COMMAND_RESTRICTED, COMMAND_RESTRICTED,
	                                COMMAND_RESTRICTED, COMMAND_AVAILABLE };
	CommandAvailability best = group[0];
	for (Int i = 1; i < 4; i++)
		if (ControlBar::commandAvailabilityRank(group[i]) > ControlBar::commandAvailabilityRank(best))
			best = group[i];
	CHECK_EQ((Int)best, (Int)COMMAND_AVAILABLE);
}

TEST(placement_grid_snap_puts_footprint_edges_on_cell_lines)
{
	const Real cell = 10.0f;

	/* The pathfinder files a position under floor((v + 0.5) / 10), so its cell lines sit at
	 * k*10 - 0.5, not at k*10.  Everything below is measured against those. */
	CHECK_NEAR(InGameUI::placementGridLine(0), -0.5f, 0.0001f);
	CHECK_NEAR(InGameUI::placementGridLine(3), 29.5f, 0.0001f);

	/* 3 cells wide (extent 15) - centre on a cell centre, whatever it started as */
	CHECK_NEAR(InGameUI::snapPlacementAxis(0.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(4.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(9.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(11.0f, 15.0f), 14.5f, 0.0001f);

	/* 4 cells wide (extent 20) - centre on a cell line */
	CHECK_NEAR(InGameUI::snapPlacementAxis(4.0f, 20.0f), -0.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(6.0f, 20.0f), 9.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(123.0f, 20.0f), 119.5f, 0.0001f);

	/* a snap never moves anything more than half a cell */
	for (Int i = 0; i < 400; i++)
	{
		Real v = i * 0.7f;

		CHECK(fabsf(InGameUI::snapPlacementAxis(v, 15.0f) - v) <= cell / 2.0f + 0.0001f);
		CHECK(fabsf(InGameUI::snapPlacementAxis(v, 20.0f) - v) <= cell / 2.0f + 0.0001f);
	}

	/* and both edges of the footprint end up on the pathfinder's own cell lines, odd width and
	 * even alike - which is what makes the cells a structure blocks exactly the cells it covers */
	for (Int cells = 1; cells <= 8; cells++)
	{
		Real extent = cells * cell * 0.5f;
		Real centre = InGameUI::snapPlacementAxis(37.3f, extent);
		Real lo = (centre - extent - InGameUI::placementGridLine(0)) / cell;
		Real hi = (centre + extent - InGameUI::placementGridLine(0)) / cell;

		CHECK_NEAR(lo, (Real)REAL_TO_INT_FLOOR(lo + 0.5f), 0.0001f);
		CHECK_NEAR(hi, (Real)REAL_TO_INT_FLOOR(hi + 0.5f), 0.0001f);

		/* the cell the pathfinder puts each edge in is the first and last cell covered: no
		 * half-unit sliver of a neighbouring cell left over for the building next door */
		CHECK_EQ(REAL_TO_INT_FLOOR((centre - extent + 0.5f) / cell),
		         REAL_TO_INT_FLOOR((centre - extent + 0.5f + 0.1f) / cell));
		CHECK_EQ(REAL_TO_INT_FLOOR((centre + extent + 0.5f) / cell) - 1,
		         REAL_TO_INT_FLOOR((centre + extent + 0.5f - 0.1f) / cell));
	}

	/* a template with no footprint worth the name still lands on a whole cell, not on nothing */
	CHECK_NEAR(InGameUI::snapPlacementAxis(13.0f, 0.0f), 14.5f, 0.0001f);
}


/* The shipped AIData.ini values, so the numbers below are the ones a real game uses. */
static const Real AIDATA_TEAM_SECONDS   = 10.0f;
static const Int  AIDATA_POOR           = 2000;
static const Int  AIDATA_WEALTHY        = 7000;
static const Real AIDATA_TEAM_POOR_MOD  = 0.6f;
static const Real AIDATA_TEAM_RICH_MOD  = 2.0f;
static const Real SKIRMISH_RATE         = 10.0f/3.0f;

static Int teamDelay(Int money, Real rate)
{
	return AIPlayer::computeBuildDelay(AIDATA_TEAM_SECONDS, money, AIDATA_POOR, AIDATA_WEALTHY,
	                                   AIDATA_TEAM_POOR_MOD, AIDATA_TEAM_RICH_MOD, rate);
}

TEST(ai_build_delay_follows_the_players_bank_account)
{
	/* A campaign AI runs at the rate the data literally says: 10 seconds flat, faster when it
	 * is rich, slower when it is broke. */
	CHECK_EQ(teamDelay(4000, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(9000, 1.0f),  5 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(1000, 1.0f), (Int)(10.0f / 0.6f * LOGICFRAMES_PER_SECOND));

	/* The thresholds are exclusive on both sides - sitting exactly on Poor or exactly on
	 * Wealthy is neither. */
	CHECK_EQ(teamDelay(AIDATA_POOR, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(AIDATA_WEALTHY, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
}

TEST(skirmish_build_rate_keeps_the_old_pace_but_not_the_old_flat_clamp)
{
	/* The skirmish AI used to clamp both of its timers to a flat 3 seconds every frame they
	 * counted down.  With the shipped TeamSeconds of 10 that clamp fired for every player at
	 * every wealth, so TeamsWealthyRate and TeamsPoorRate did nothing at all.  As a rate the
	 * neutral case still lands on the same 3 seconds... */
	CHECK_EQ(teamDelay(4000, SKIRMISH_RATE), 3 * LOGICFRAMES_PER_SECOND);

	/* ...and the modifiers around it are alive again: rich presses harder, broke backs off.
	 * Under the clamp all three of these were 90. */
	CHECK_EQ(teamDelay(9000, SKIRMISH_RATE), (Int)(1.5f * LOGICFRAMES_PER_SECOND));
	CHECK_EQ(teamDelay(1000, SKIRMISH_RATE), (Int)(5.0f * LOGICFRAMES_PER_SECOND));
	CHECK(teamDelay(9000, SKIRMISH_RATE) < teamDelay(4000, SKIRMISH_RATE));
	CHECK(teamDelay(1000, SKIRMISH_RATE) > teamDelay(4000, SKIRMISH_RATE));

	/* The SET_BASE_CONSTRUCTION_SPEED script action writes the seconds this reads, and the
	 * clamp is what used to eat anything it asked for above 3 seconds. */
	const Int scripted = AIPlayer::computeBuildDelay(30.0f, 4000, AIDATA_POOR, AIDATA_WEALTHY,
	                                                 AIDATA_TEAM_POOR_MOD, AIDATA_TEAM_RICH_MOD,
	                                                 SKIRMISH_RATE);
	CHECK_EQ(scripted, 9 * LOGICFRAMES_PER_SECOND);
	CHECK(scripted > 3 * LOGICFRAMES_PER_SECOND);

	/* StructureSeconds ships as 0, so structures stay on the "as soon as the build delay lets
	 * you" path they are on today, whatever the rate is. */
	CHECK_EQ(AIPlayer::computeBuildDelay(0.0f, 9000, AIDATA_POOR, AIDATA_WEALTHY, 0.6f, 2.0f, SKIRMISH_RATE), 0);
}

TEST(skirmish_team_move_actions_are_the_throttled_ones)
{
	/* Every action listed here reaches AIGroup::friend_computeGroundPath, which runs a full-map A*
	 * synchronously.  Six of them landed on one logic frame in a 1v7 skirmish and cost 80ms of a
	 * 33ms budget, so the sequential-script step lets one through per frame.  Pin the set: adding a
	 * team-move action without adding it here quietly puts the pile-up back. */
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::MOVE_TEAM_TO));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::TEAM_FOLLOW_WAYPOINTS));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::TEAM_FOLLOW_WAYPOINTS_EXACT));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::CREATE_REINFORCEMENT_TEAM));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE));

	/* ...and nothing cheap is throttled, or the AI would crawl for no reason. */
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::DEBUG_MESSAGE_BOX));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::ENABLE_SCRIPT));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SET_FLAG));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::NO_OP));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(-1));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(0x7fffffff));
}

TEST(ai_players_do_not_all_check_in_on_the_same_frame)
{
	/* Every AIPlayer used to be built with the same timers on the same frame, and every one of
	 * its repeating checks re-arms itself from a constant, so seven bots ran their base building,
	 * team building and bridge repair together for the whole match and the cost of all seven
	 * landed on one logic frame.  The phase is what pulls them apart. */
	const Int cycle = 2 * LOGICFRAMES_PER_SECOND;
	Int seen[ MAX_PLAYER_COUNT ];
	Int i, j;
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
	{
		seen[i] = AIPlayer::computeUpdatePhase(i, cycle);
		/* A phase is a slot inside the cycle, never a cycle of its own - the check still runs
		 * exactly as often as it did, just not at the same moment as the neighbour's. */
		CHECK(seen[i] >= 0);
		CHECK(seen[i] < cycle);
	}
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
		for (j = i + 1; j < MAX_PLAYER_COUNT; j++)
			CHECK_NE(seen[i], seen[j]);

	/* It is a function of the player index and nothing else - no clock, no random - or the
	 * lockstep simulation would desync the moment two machines disagreed. */
	CHECK_EQ(AIPlayer::computeUpdatePhase(3, cycle), AIPlayer::computeUpdatePhase(3, cycle));

	/* A one-second cycle has fewer frames than MAX_PLAYER_COUNT has players, so the slots
	 * collide there; what must not happen is a phase outside the cycle. */
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
	{
		const Int shortPhase = AIPlayer::computeUpdatePhase(i, LOGICFRAMES_PER_SECOND);
		CHECK(shortPhase >= 0);
		CHECK(shortPhase < LOGICFRAMES_PER_SECOND);
	}

	/* Garbage in stays harmless: an unset index or a zero cycle means "no offset". */
	CHECK_EQ(AIPlayer::computeUpdatePhase(-1, cycle), 0);
	CHECK_EQ(AIPlayer::computeUpdatePhase(0, cycle), 0);
	CHECK_EQ(AIPlayer::computeUpdatePhase(5, 0), 0);
}

//////////////////////////////////////////////////////////////////////////////
// Pathfinder cell info pool
//////////////////////////////////////////////////////////////////////////////

/* The A* open and closed lists are drawn from one fixed pool of PathfindCellInfo
   records.  A search that walks away from a cell without handing its record back
   leaks one entry, every time it runs, for the rest of the match - and once the
   pool is dry every pathfind on the map fails and the units stop taking orders.
   The pool is private, so the only way to measure it is to drain it. */
enum { CELL_POOL_PROBE_CAP = 400000 };

static Int drainCellInfoPool( void )
{
	PathfindCell *cells = new PathfindCell[ CELL_POOL_PROBE_CAP ];
	Int count = 0;
	while( count < CELL_POOL_PROBE_CAP )
	{
		ICoord2D pos;
		pos.x = count & 0xff;
		pos.y = (count >> 8) & 0xff;
		if( !cells[ count ].allocateInfo( pos ) )
			break;
		count++;
	}
	for( Int i = 0; i < count; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	return count;
}

TEST(pathfind_pool_comes_back_whole_after_a_search_is_started)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();
	CHECK(baseline > 1000);
	CHECK(baseline < CELL_POOL_PROBE_CAP);

	{
		PathfindCell start, goal;
		ICoord2D sp, gp;
		sp.x = 10; sp.y = 10;
		gp.x = 20; gp.y = 30;
		CHECK(start.allocateInfo(sp));
		CHECK(goal.allocateInfo(gp));

		/* startPathfind used to mark the start cell as sitting on the open list.  It is not:
		 * the caller assigns it to m_openList by hand rather than linking it in.  All the flag
		 * did was make releaseInfo() refuse to hand the record back, so every search that gave
		 * up early - wrong zone, no path, pool empty - leaked its start cell. */
		start.startPathfind(&goal);
		CHECK(!start.getOpen());
		CHECK(!start.getClosed());

		start.releaseInfo();
		CHECK(!start.hasInfo());
		goal.releaseInfo();
		CHECK(!goal.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}

TEST(pathfind_cell_drops_its_parent_link_even_when_it_keeps_its_record)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();

	{
		PathfindCell keeper, parent;
		ICoord2D kp, pp;
		kp.x = 5; kp.y = 5;
		pp.x = 6; pp.y = 5;
		CHECK(keeper.allocateInfo(kp));
		CHECK(parent.allocateInfo(pp));
		keeper.setParentCell(&parent);
		CHECK(keeper.getParentCell() == &parent);

		/* An obstacle cell hangs on to its record - releaseInfo() returns early for it.  It
		 * used to return before clearing the parent link as well, so the cell went on pointing
		 * at a record the very next search hands out to some other cell, and walking the path
		 * backwards from there reads a stranger's data. */
		keeper.setType(PathfindCell::CELL_OBSTACLE);
		keeper.releaseInfo();
		CHECK(keeper.hasInfo());
		CHECK(keeper.getParentCell() == NULL);

		keeper.setType(PathfindCell::CELL_CLEAR);
		keeper.releaseInfo();
		CHECK(!keeper.hasInfo());
		parent.releaseInfo();
		CHECK(!parent.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}

TEST(pathfind_obstacle_state_no_longer_borrows_a_search_record)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();

	{
		PathfindCell cell;
		CHECK(!cell.hasInfo());

		/* Which object stands on a cell, whether it is a fence, whether you can see through it
		 * and whether an ally is in the way all used to be stored in a pooled search record, so
		 * a cell had to hold one open for as long as the wall stood on it - and releaseInfo()
		 * refuses to reclaim an obstacle cell, so every building and fence on the map was a
		 * permanent bite out of the pool the search draws from.  They are cell state now, and a
		 * cell answers for them with no record at all. */
		cell.setBlockedByAlly(TRUE);
		CHECK(cell.isBlockedByAlly());
		cell.setBlockedByAlly(FALSE);
		CHECK(!cell.isBlockedByAlly());
		CHECK(cell.getObstacleID() == INVALID_ID);
		CHECK(!cell.isObstacleFence());
		CHECK(!cell.isObstacleTransparent());
		CHECK(!cell.isObstaclePresent((ObjectID)17));

		CHECK(!cell.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}


/* The tunnel network keeps the contain list for every tunnel a player owns, and
 * TunnelContain::killAllContained has to take that whole list away from the tracker before it
 * kills anything: a Terrorist that explodes on death kills the tunnel, and the tunnel's own death
 * walks the same list the outer call is still standing in.  Taking the list away is only half of
 * it - the tracker's cached size has to follow, or the game reads a count that no longer matches
 * the list.  No Object is touched here, only the bookkeeping. */
TEST(tunneltracker_handing_the_contain_list_over_takes_the_count_with_it)
{
	CHECK(bootOnce());

	TunnelTracker *tracker = newInstance(TunnelTracker);
	CHECK_EQ((Int)tracker->getContainCount(), 0);

	ContainedItemsList seeded;
	seeded.push_back((Object *)0x100);
	seeded.push_back((Object *)0x200);
	seeded.push_back((Object *)0x300);

	tracker->swapContainedItemsList(seeded);
	CHECK(seeded.empty());
	CHECK_EQ((Int)tracker->getContainCount(), 3);
	CHECK_EQ((Int)tracker->getContainedItemsList()->size(), 3);

	/* what killAllContained does: take the list, leave the tracker empty and consistent */
	ContainedItemsList taken;
	tracker->swapContainedItemsList(taken);
	CHECK_EQ((Int)taken.size(), 3);
	CHECK_EQ((Int)tracker->getContainCount(), 0);
	CHECK(tracker->getContainedItemsList()->empty());

	taken.clear();
	tracker->deleteInstance();
}

/* StateMachine is reference counted so that a state update which destroys the
   machine's owner does not pull the machine out from under updateStateMachine.
   The owner's deleteInstance() is now just "drop my reference". */
static Bool s_witnessMachineDestroyed = FALSE;

class WitnessStateMachine : public StateMachine
{
	/* borrow the real machine's pool - same size, and MemoryInit.cpp knows the name */
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(WitnessStateMachine, "StateMachinePool")
public:
	WitnessStateMachine() : StateMachine(NULL, "witness") {}
};

WitnessStateMachine::~WitnessStateMachine() { s_witnessMachineDestroyed = TRUE; }

TEST(statemachine_outlives_the_owner_that_lets_go_of_it_mid_update)
{
	CHECK(bootOnce());

	s_witnessMachineDestroyed = FALSE;
	StateMachine *machine = newInstance(WitnessStateMachine);
	CHECK_EQ(machine->Num_Refs(), 1);

	/* what updateStateMachine holds while m_currentState->update() runs */
	machine->Add_Ref();
	CHECK_EQ(machine->Num_Refs(), 2);

	/* the state update kills the owning object, which deletes its machine */
	machine->deleteInstance();
	CHECK(!s_witnessMachineDestroyed);
	CHECK_EQ(machine->Num_Refs(), 1);

	/* update() returns, updateStateMachine drops its reference - now it goes */
	machine->Release_Ref();
	CHECK(s_witnessMachineDestroyed);

	/* and deleteInstance() still tolerates a NULL machine, the way the pool one did */
	machine = NULL;
	machine->deleteInstance();
}


/* ------------------------------------------------------------------------------------------------
 * The A* open list.
 *
 * `PathfindCell::putOnSortedOpenList` used to walk from the head on every insert, past every cell
 * of equal cost, which is O(open list) per expanded cell - 62 million walk steps over 108 slow
 * frames in a real skirmish.  It now keeps a tail pointer and enters from whichever end is nearer
 * the new cost.  The insertion *point* has to stay exactly where it was, or the search expands a
 * different set of cells and lockstep breaks, so these check the resulting order against a stable
 * sort of the same insertion sequence: ascending cost, ties in insertion order.
 * ---------------------------------------------------------------------------------------------- */
static PathfindCell *theOpenTestCells = NULL;

static Bool openListOrderMatches( PathfindCell *list, const std::vector<Int>& expected )
{
	UnsignedInt n = 0;
	UnsignedInt prevCost = 0;
	for( PathfindCell *c = list; c; c = c->getNextOpen() )
	{
		if( n >= expected.size() )
			return false;											// longer than it should be
		if( c->getTotalCost() < prevCost )
			return false;											// not sorted
		if( (Int)(c - theOpenTestCells) != expected[ n ] )
			return false;											// right costs, wrong tie order
		prevCost = c->getTotalCost();
		n++;
	}
	return n == expected.size();
}

TEST(pathfind_open_list_insert_keeps_ascending_cost_and_insertion_order_on_ties)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int count = 400;
	PathfindCell *cells = MSGNEW("PathfindCellInfo") PathfindCell[ count ];
	theOpenTestCells = cells;

	/* costs with a lot of ties (the grid quantises everything to multiples of ten) and no
		 monotonic order, so every branch of the new insert gets used */
	std::vector<Int> expected;
	PathfindCell *list = NULL;
	Int seed = 12345;
	Int i;
	for( i = 0; i < count; i++ )
	{
		seed = seed * 1103515245 + 12345;
		UnsignedInt cost = 10 * (UnsignedInt)(((seed >> 16) & 0x7fff) % 25);
		ICoord2D pos;
		pos.x = (UnsignedShort)(i % 64);
		pos.y = (UnsignedShort)(i / 64);
		CHECK(cells[ i ].allocateInfo( pos ));
		cells[ i ].setTotalCost( cost );

		/* the reference: insert after every cell of equal or lower cost */
		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, i );

		list = cells[ i ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* pull cells out - including the head and the tail, the two the fast paths depend on - and
		 put fresh ones back, which is exactly what an A* loop does */
	Int removed[ 5 ];
	removed[ 0 ] = expected.front();
	removed[ 1 ] = expected.back();
	removed[ 2 ] = expected[ expected.size() / 2 ];
	removed[ 3 ] = expected[ 1 ];
	removed[ 4 ] = expected[ expected.size() - 2 ];
	for( i = 0; i < 5; i++ )
	{
		list = cells[ removed[ i ] ].removeFromOpenList( list );
		expected.erase( std::find( expected.begin(), expected.end(), removed[ i ] ) );
	}
	CHECK(openListOrderMatches( list, expected ));

	for( i = 0; i < 5; i++ )
	{
		Int c = removed[ i ];
		UnsignedInt cost = 10 * (UnsignedInt)(i * 7 % 25);
		cells[ c ].setTotalCost( cost );
		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, c );
		list = cells[ c ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* emptying it one at a time from the tail end must not leave a stale tail behind */
	while( !expected.empty() )
	{
		Int c = expected.back();
		expected.pop_back();
		list = cells[ c ].removeFromOpenList( list );
	}
	CHECK(list == NULL);

	cells[ 0 ].setTotalCost( 100 );
	list = cells[ 0 ].putOnSortedOpenList( NULL );
	cells[ 1 ].setTotalCost( 50 );
	list = cells[ 1 ].putOnSortedOpenList( list );
	CHECK(list == &cells[ 1 ]);
	CHECK(list->getNextOpen() == &cells[ 0 ]);

	PathfindCell::releaseOpenList( list );
	for( i = 0; i < count; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	theOpenTestCells = NULL;
	PathfindCellInfo::releaseCellInfos();
}


/* The open list is not walked any more, it is indexed by cost: a bucket per possible m_totalCost,
	 with a three level bit index over the occupied ones.  That index is the part that can be wrong
	 in ways the previous test would not notice, because its costs all live inside one 32 bit word of
	 the bottom level.  This one spreads them across the whole 16 bit key space, so finding the
	 nearest occupied bucket below a new cost has to climb the summaries and come back down, and it
	 empties buckets from both ends so the clear paths run too.

	 It also reproduces the one thing the game does that the index cannot see coming: raising a
	 cell's cost while it is still linked into the list, and only then taking it off.  That is
	 findAttackPath's decrease-key, and it is why a cell remembers which bucket it went into. */
TEST(pathfind_open_list_bucket_index_finds_the_right_neighbour_across_the_whole_cost_range)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int count = 300;
	const Int spares = 8;												// held back, to be inserted at costs that were vacated
	PathfindCell *cells = MSGNEW("PathfindCellInfo") PathfindCell[ count + spares ];
	theOpenTestCells = cells;

	std::vector<Int> expected;
	PathfindCell *list = NULL;
	Int seed = 987654321;
	Int i;

	/* costs spread over the whole UnsignedShort range, including both ends, with enough repeats to
		 keep the tie order under test as well */
	for( i = 0; i < count; i++ )
	{
		seed = seed * 1103515245 + 12345;
		UnsignedInt cost;
		switch( i % 10 )
		{
			case 0:  cost = 0; break;											// the very bottom bucket
			case 1:  cost = 65535; break;									// the very top one
			case 2:  cost = 31; break;										// last bit of word 0
			case 3:  cost = 32; break;										// first bit of word 1
			case 4:  cost = 1023; break;									// last bucket under summary word 0
			case 5:  cost = 1024; break;									// first bucket over it
			case 6:  cost = 32767; break;									// last bucket under the top word 0
			case 7:  cost = 32768; break;									// first bucket in top word 1
			default: cost = (UnsignedInt)((seed >> 8) & 0xFFFF); break;
		}
		ICoord2D pos;
		pos.x = (UnsignedShort)(i % 64);
		pos.y = (UnsignedShort)(i / 64);
		CHECK(cells[ i ].allocateInfo( pos ));
		cells[ i ].setTotalCost( cost );

		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, i );

		list = cells[ i ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* the game's decrease-key, in the game's order: change the cost first, take the cell off
		 second.  If the removal is filed under the new cost instead of the bucket the cell is
		 actually in, the old bucket is left pointing at a cell that is no longer on the list and
		 the next insert at that cost links itself into nothing. */
	Int rekeyed[ 6 ];
	rekeyed[ 0 ] = expected.front();
	rekeyed[ 1 ] = expected.back();
	rekeyed[ 2 ] = expected[ expected.size() / 3 ];
	rekeyed[ 3 ] = expected[ expected.size() / 2 ];
	rekeyed[ 4 ] = expected[ 1 ];
	rekeyed[ 5 ] = expected[ expected.size() - 2 ];
	for( i = 0; i < 6; i++ )
	{
		Int c = rekeyed[ i ];
		UnsignedInt was = cells[ c ].getTotalCost();
		UnsignedInt cost = (UnsignedInt)(i * 9973) & 0xFFFF;
		cells[ c ].setTotalCost( cost );										// cost changes while still linked
		list = cells[ c ].removeFromOpenList( list );
		expected.erase( std::find( expected.begin(), expected.end(), c ) );

		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, c );
		list = cells[ c ].putOnSortedOpenList( list );
		CHECK(openListOrderMatches( list, expected ));

		/* and now a fresh cell at the cost the rekeyed one used to have.  This is what makes the
			 stale bucket visible: if the removal was filed under the new cost, the old cost still
			 points at the rekeyed cell, which has since moved somewhere else entirely, and this
			 insert lands next to it instead of where its own cost belongs. */
		Int spare = count + i;
		ICoord2D spos;
		spos.x = (UnsignedShort)(spare % 64);
		spos.y = (UnsignedShort)(spare / 64);
		CHECK(cells[ spare ].allocateInfo( spos ));
		cells[ spare ].setTotalCost( was );
		it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= was )
			++it;
		expected.insert( it, spare );
		list = cells[ spare ].putOnSortedOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}

	/* empty it from the cheap end - an A* pop loop - and check after every pop, because a bucket
		 left marked occupied after its last cell leaves is exactly the failure that would send the
		 next insert to a freed cell */
	while( expected.size() > 100 )
	{
		Int c = expected.front();
		expected.erase( expected.begin() );
		list = cells[ c ].removeFromOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}

	/* and the rest from the dear end, which drains the top summary word */
	while( !expected.empty() )
	{
		Int c = expected.back();
		expected.pop_back();
		list = cells[ c ].removeFromOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}
	CHECK(list == NULL);

	/* an emptied index must be empty: two cells at costs that were both heavily used above, in
		 the wrong order, still come out sorted */
	cells[ 0 ].setTotalCost( 65535 );
	list = cells[ 0 ].putOnSortedOpenList( NULL );
	cells[ 1 ].setTotalCost( 0 );
	list = cells[ 1 ].putOnSortedOpenList( list );
	cells[ 2 ].setTotalCost( 32768 );
	list = cells[ 2 ].putOnSortedOpenList( list );
	CHECK(list == &cells[ 1 ]);
	CHECK(list->getNextOpen() == &cells[ 2 ]);
	CHECK(list->getNextOpen()->getNextOpen() == &cells[ 0 ]);
	CHECK(list->getNextOpen()->getNextOpen()->getNextOpen() == NULL);

	PathfindCell::releaseOpenList( list );
	for( i = 0; i < count + spares; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	theOpenTestCells = NULL;
	PathfindCellInfo::releaseCellInfos();
}


//-------------------------------------------------------------------------------------------------
// Zone equivalency sets (pathfindZoneFind / pathfindZoneUnion / pathfindZoneFlatten).
//
// The pathfinder merges zone ids with a union-find over the equivalency array instead of EA's
// relabel-the-whole-array loop.  The contract the rest of the pathfinder relies on is exact, not
// approximate: after flattening, array[i] must be the *smallest* id in i's set, which is what the
// relabelling version produced.  This drives a long random merge sequence through both the real
// implementation and a straight-line reference relabeller and requires the two arrays to match
// entry for entry.
//-------------------------------------------------------------------------------------------------

// EA's original merge: canonicalize both, keep the lower, relabel every entry.
static void referenceResolveZones( zoneStorageType *zones, Int srcZone, Int targetZone, Int numZones )
{
	srcZone = zones[srcZone];
	targetZone = zones[targetZone];
	zoneStorageType finalZone = (targetZone < srcZone) ? zones[targetZone] : zones[srcZone];
	for (Int i = 0; i < numZones; i++) {
		zoneStorageType ze = zones[i];
		if (ze == targetZone || ze == srcZone) {
			zones[i] = finalZone;
		}
	}
}

TEST(pathfind_zone_union_find_matches_the_relabelling_merge_it_replaced)
{
	enum { NUM_ZONES = 500, NUM_MERGES = 4000 };
	static zoneStorageType real[ NUM_ZONES ];
	static zoneStorageType ref[ NUM_ZONES ];
	Int i;
	for (i = 0; i < NUM_ZONES; i++) {
		real[ i ] = (zoneStorageType)i;
		ref[ i ] = (zoneStorageType)i;
	}

	// A fixed LCG, so a failure is reproducible.
	UnsignedInt seed = 12345;
	Int mismatches = 0;
	Int merges;
	for (merges = 0; merges < NUM_MERGES; merges++) {
		seed = seed * 1103515245 + 12345;
		Int a = 1 + (Int)((seed >> 16) % (NUM_ZONES - 1));
		seed = seed * 1103515245 + 12345;
		Int b = 1 + (Int)((seed >> 16) % (NUM_ZONES - 1));

		pathfindZoneUnion( real, a, b );
		referenceResolveZones( ref, a, b, NUM_ZONES );

		// Reading a set representative mid-sequence must agree too - the merge loops in
		// calculateZones compare representatives between merges to decide what to merge next.
		if (pathfindZoneFind( real, a ) != ref[ a ]) mismatches++;
		if (pathfindZoneFind( real, b ) != ref[ b ]) mismatches++;
	}
	CHECK_EQ( mismatches, 0 );

	pathfindZoneFlatten( real, NUM_ZONES );
	Int diffs = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ i ] != ref[ i ]) diffs++;
	}
	CHECK_EQ( diffs, 0 );

	// The whole array must be flat afterwards: array[array[i]] == array[i].
	Int notFlat = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ real[ i ] ] != real[ i ]) notFlat++;
	}
	CHECK_EQ( notFlat, 0 );

	// And every representative must be the minimum id of its set.
	Int notMinimum = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ i ] > (zoneStorageType)i) notMinimum++;
	}
	CHECK_EQ( notMinimum, 0 );
}

TEST(pathfind_zone_flatten_collapses_a_deep_chain_in_one_pass)
{
	// pathfindZoneFlatten is a single ascending pass, which is only correct because every link
	// points from a higher id to a lower one.  Build the deepest chain the union can produce -
	// 1 <- 2 <- 3 <- ... - by merging in an order that never gives path compression a chance.
	enum { NUM_ZONES = 64 };
	zoneStorageType zones[ NUM_ZONES ];
	Int i;
	for (i = 0; i < NUM_ZONES; i++) {
		zones[ i ] = (zoneStorageType)i;
	}
	for (i = NUM_ZONES - 1; i >= 2; i--) {
		zones[ i ] = (zoneStorageType)(i - 1);		// hand-built chain, no compression
	}
	CHECK_EQ( (Int)zones[ NUM_ZONES - 1 ], NUM_ZONES - 2 );

	pathfindZoneFlatten( zones, NUM_ZONES );
	Int notOne = 0;
	for (i = 1; i < NUM_ZONES; i++) {
		if (zones[ i ] != 1) notOne++;
	}
	CHECK_EQ( notOne, 0 );
	CHECK_EQ( (Int)zones[ 0 ], 0 );
}

/* The two bits of arithmetic behind the command bar's new numbers.  Declared here rather than by
   including ControlBar.h for them: they are free functions in the ControlBar sources, and the
   header carries the class, not these. */
extern Int ControlBar_secondsFromFrames( Real frames );
extern Int ControlBar_secondsFromFramesAt( Real frames, Int logicFps );
extern Int ControlBar_experiencePercent( Int currentExp, Int levelExp, Int nextLevelExp );

TEST(controlbar_seconds_round_up_and_never_reach_zero_early)
{
	/* nothing left is the only thing that reads as no number at all */
	CHECK_EQ( ControlBar_secondsFromFrames( 0.0f ), 0 );
	CHECK_EQ( ControlBar_secondsFromFrames( -5.0f ), 0 );

	/* a whole second is a whole second, at LOGICFRAMES_PER_SECOND to the second */
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND ), 1 );
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND * 6.0f ), 6 );
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND * 120.0f ), 120 );

	/* and anything in between rounds up: 1.1s must not print as 1s and then sit there */
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND + 1.0f ), 2 );

	/* the one that matters - a single frame of work left still says 1s, because a button with
	   work left on it reading 0s looks finished when it is not */
	CHECK_EQ( ControlBar_secondsFromFrames( 1.0f ), 1 );
}

TEST(controlbar_seconds_are_real_seconds_at_the_current_game_speed)
{
	/* The number on a build button is a promise about how long you will be waiting, and the logic
	   rate is a knob in this fork (the game speed keys move it between 5 and 200). The same 600
	   frames of work is 20 seconds at the nominal rate and 10 at double speed. */
	const Real frames = 600.0f;
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND ), 20 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND * 2 ), 10 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND / 2 ), 40 );

	/* A rate of zero is the engine before it has one, not a division to attempt: fall back to the
	   nominal rate. This is also the path the whole suite runs on - the test binary has no
	   GameEngine, so ControlBar_secondsFromFrames() above resolves the rate to 0. */
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, 0 ), 20 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, -5 ), 20 );

	/* and the round-up and the never-zero floor hold at any rate */
	CHECK_EQ( ControlBar_secondsFromFramesAt( 61.0f, 60 ), 2 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( 1.0f, 200 ), 1 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( 0.0f, 200 ), 0 );
}

TEST(controlbar_experience_percent_fills_the_rank_and_clamps)
{
	/* a fresh unit at the bottom of its rank */
	CHECK_EQ( ControlBar_experiencePercent( 0, 0, 100 ), 0 );
	CHECK_EQ( ControlBar_experiencePercent( 50, 0, 100 ), 50 );
	CHECK_EQ( ControlBar_experiencePercent( 99, 0, 100 ), 99 );

	/* the window is between two thresholds, not from zero: half way from 100 to 300 is 50% */
	CHECK_EQ( ControlBar_experiencePercent( 200, 100, 300 ), 50 );

	/* experience past the next threshold (the level has not been applied yet) pins the bar full
	   rather than overflowing it, and a sink that took points away pins it empty */
	CHECK_EQ( ControlBar_experiencePercent( 400, 100, 300 ), 100 );
	CHECK_EQ( ControlBar_experiencePercent( 50, 100, 300 ), 0 );

	/* no next rank to fill towards - top rank, or a template with no thresholds at all - is the
	   "draw no bar" answer, and must not be a division by zero */
	CHECK_EQ( ControlBar_experiencePercent( 500, 300, 300 ), -1 );
	CHECK_EQ( ControlBar_experiencePercent( 0, 0, 0 ), -1 );
	CHECK_EQ( ControlBar_experiencePercent( 10, 300, 100 ), -1 );
}

/* ---------------------------------------------------------------------------------------------
   Multiplayer pacing.

   FrameMetrics feeds ConnectionManager::updateRunAhead the answer to "how fast can this machine
   advance the simulation", and that number becomes the input delay everyone in the room lives
   with. It used to be sampled from TheDisplay->getAverageFPS(), which was the same number as the
   logic rate only while EA's renderer was locked to the logic tick. This fork uncapped the
   renderer, so the two came apart and the run-ahead started being sized off a GPU measurement.
   --------------------------------------------------------------------------------------------- */
extern Real FrameMetrics_logicFpsSample( UnsignedInt frame, UnsignedInt windowStartFrame, time_t windowMS );

TEST(network_fps_metric_measures_logic_frames_not_rendered_ones)
{
	/* The case that broke: a weak GPU drawing 12fps while the CPU still steps logic 30 times a
	   second must report 30, because 30 is what the run-ahead has to cover. Nothing in the sample
	   can see the render rate - the only inputs are logic frame numbers and a wall-clock window. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 1000 ), 30.0f, 0.001f );

	/* A machine that genuinely cannot keep up reports what it managed, so the run-ahead grows to
	   cover it. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1012, 1000, 1000 ), 12.0f, 0.001f );

	/* The window is whatever it turned out to be, not an assumed 1000ms: the sampler fires on the
	   first logic frame at or past a second, which on a stuttering machine can be well past it.
	   30 frames spread over 1500ms is 20fps, not 30. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 1500 ), 20.0f, 0.001f );
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000,  500 ), 60.0f, 0.001f );

	/* A window with no time in it is not a division to attempt. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 0 ), 0.0f, 0.001f );

	/* A window with no frames in it is a real answer - the simulation did not advance at all -
	   and must read zero rather than wrapping the unsigned subtraction. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1000, 1000, 1000 ), 0.0f, 0.001f );
}

/* ---------------------------------------------------------------------------------------------
   Adaptive retransmit. EA shipped a flat 2000ms retry, so on a fast link one lost command packet
   cost the whole room a two-second stall. Their own commented-out intent (average latency * 1.5)
   could not be switched on as written - the average is a rolling mean over an array that starts
   full of zeroes - so this is Jacobson/Karels instead, which carries a variance term and widens on
   a jittery link rather than hugging the mean.
   --------------------------------------------------------------------------------------------- */
extern void Connection_updateRetryTimeout( Real sampleMS, Real &srtt, Real &rttvar, time_t &retryMS );
extern time_t Connection_retryDelayFor( time_t baseRetryMS, Int numTimesSent );

TEST(connection_retry_timeout_follows_the_link_and_never_leaves_its_bounds)
{
	/* A steady 40ms LAN link: the first sample seeds srtt=40, rttvar=20, so the timeout starts at
	   40+80=120 and is lifted to the 150ms floor. Feeding the same number repeatedly drives the
	   deviation to zero, so it settles on the floor - which is the whole point: a lost packet on
	   this link is retried in 150ms, not 2000. */
	Real srtt = -1.0f, rttvar = 0.0f;
	time_t retry = CONNECTION_MAX_RETRY_TIME;
	Connection_updateRetryTimeout( 40.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );
	for( Int i = 0; i < 100; ++i )
		Connection_updateRetryTimeout( 40.0f, srtt, rttvar, retry );
	CHECK_NEAR( srtt, 40.0f, 0.5f );
	CHECK( rttvar < 1.0f );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );

	/* A steady 300ms link settles above the floor, at about the mean, because the deviation
	   collapses. It must not sit at EA's ceiling. */
	srtt = -1.0f; rttvar = 0.0f; retry = CONNECTION_MAX_RETRY_TIME;
	for( Int i = 0; i < 200; ++i )
		Connection_updateRetryTimeout( 300.0f, srtt, rttvar, retry );
	CHECK( retry > CONNECTION_MIN_RETRY_TIME );
	CHECK( retry < CONNECTION_MAX_RETRY_TIME );
	CHECK_NEAR( (Real)retry, 300.0f, 30.0f );

	/* The reason the variance term exists: a link whose mean is 100ms but which swings between
	   40 and 160 must be given more room than a rock-steady 100ms link, or every swing is read as
	   a loss and retransmitted. */
	Real steadySrtt = -1.0f, steadyVar = 0.0f;
	time_t steadyRetry = CONNECTION_MAX_RETRY_TIME;
	Real jumpySrtt = -1.0f, jumpyVar = 0.0f;
	time_t jumpyRetry = CONNECTION_MAX_RETRY_TIME;
	for( Int i = 0; i < 200; ++i )
	{
		Connection_updateRetryTimeout( 100.0f, steadySrtt, steadyVar, steadyRetry );
		Connection_updateRetryTimeout( (i & 1) ? 160.0f : 40.0f, jumpySrtt, jumpyVar, jumpyRetry );
	}
	CHECK( jumpyRetry > steadyRetry );

	/* Neither bound can be crossed: a satellite link is capped at EA's old constant so nobody is
	   served worse than the shipped behaviour, and a zero-latency loopback still waits the floor. */
	srtt = -1.0f; rttvar = 0.0f; retry = 0;
	for( Int i = 0; i < 50; ++i )
		Connection_updateRetryTimeout( 5000.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MAX_RETRY_TIME );

	srtt = -1.0f; rttvar = 0.0f; retry = 0;
	for( Int i = 0; i < 50; ++i )
		Connection_updateRetryTimeout( 0.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );
}

TEST(connection_retry_backs_off_when_a_command_keeps_going_unacked)
{
	/* A command that has gone out once waits the connection's plain timeout. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 1 ), 200 );

	/* Each further attempt doubles, so a link that is genuinely down is not hammered at the
	   floor rate for as long as the disconnect timer runs. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 2 ), 400 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 3 ), 800 );

	/* ...but the doubling stops there.  This is a lockstep game: the command being retried is one
	   the whole room is stopped on, so this delay is the length of the freeze, and it used to reach
	   1.2 and then 2 seconds after three and four losses of the same command.  Quartering the retry
	   rate is all the protection a dead link needs from here; deciding a link is dead belongs to the
	   disconnect manager. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 4 ), 800 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 5 ), 800 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 50 ), 800 );

	/* The freeze a command can cost on a link fast enough to sit on the retry floor, however many
	   times in a row it is lost. */
	CHECK_EQ( (Int)Connection_retryDelayFor( CONNECTION_MIN_RETRY_TIME, 50 ),
						CONNECTION_MIN_RETRY_TIME << CONNECTION_MAX_RETRY_BACKOFF_SHIFTS );
	CHECK( (Connection_retryDelayFor( CONNECTION_MIN_RETRY_TIME, 50 ) * 1000) <
				 (CONNECTION_MAX_RETRY_TIME * 1000) );

	/* A slow link still reaches the ceiling honestly - its own round trip put it there, not the
	   backoff - and the ceiling still holds. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 600, 3 ), 2000 );
	CHECK_EQ( (Int)Connection_retryDelayFor( CONNECTION_MAX_RETRY_TIME, 3 ), CONNECTION_MAX_RETRY_TIME );

	/* Monotonic, and never below the connection's own timeout. */
	for( Int sent = 0; sent < 20; ++sent )
	{
		time_t here = Connection_retryDelayFor( 200, sent );
		time_t next = Connection_retryDelayFor( 200, sent + 1 );
		CHECK( here >= 200 );
		CHECK( next >= here );
	}

	/* A command that has never been sent is not a retry, and must not read as a negative shift. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 0 ), 200 );
}

// ------------------------------------------------------------------------------------------------
// CRCSnapshotRing - the evidence a mismatch report is missing.  A mismatch is detected several
// frames after the frame it happened on, so the ring has to still hold that frame when asked, and
// it has to be able to name it from nothing but a CRC value.
// ------------------------------------------------------------------------------------------------

static void fillSnapshot( CRCSnapshotRing &ring, UnsignedInt frame, UnsignedInt totalCRC, Int numObjects )
{
	ring.beginSnapshot( frame );
	for( Int i = 0; i < numObjects; ++i )
		ring.addObject( 100 + i, 0xAB000000 + frame * 1000 + i, (Real)i, (Real)(i * 2), 0.0f, 50.0f + i );
	ring.endSnapshot( totalCRC, 0xF00D0000 + frame );
}

TEST(crcsnapshotring_names_the_diverging_frame_from_a_crc_value)
{
	CRCSnapshotRing ring;
	CHECK_EQ( ring.getNewestSlot(), -1 );
	CHECK_EQ( ring.getNumSnapshots(), 0 );

	fillSnapshot( ring, 30, 0x11111111, 3 );
	fillSnapshot( ring, 60, 0x22222222, 4 );
	fillSnapshot( ring, 90, 0x33333333, 5 );

	CHECK_EQ( ring.getNumSnapshots(), 3 );

	// the mismatch is reported on a later frame; all we have to go on is our own CRC for that frame
	Int slot = ring.findSlotByCRC( 0x22222222 );
	CHECK( slot >= 0 );

	const CRCSnapshot *snap = ring.getSlot( slot );
	CHECK( snap != NULL );
	if( snap == NULL )
		return;
	CHECK_EQ( (Int)snap->m_frame, 60 );
	CHECK_EQ( (Int)snap->m_objects.size(), 4 );
	CHECK_EQ( (Int)snap->m_randomSeed, (Int)(0xF00D0000 + 60) );

	// the per object running CRCs are what a diff between two players' dumps compares
	CHECK_EQ( (Int)snap->m_objects[0].m_id, 100 );
	CHECK_EQ( (Int)snap->m_objects[3].m_id, 103 );
	CHECK_EQ( (Int)snap->m_objects[2].m_runningCRC, (Int)(0xAB000000 + 60 * 1000 + 2) );
	CHECK_NEAR( snap->m_objects[3].m_health, 53.0f, 0.001f );

	// a CRC nobody recorded names no frame at all, rather than the wrong one
	CHECK_EQ( ring.findSlotByCRC( 0x44444444 ), -1 );
}

TEST(crcsnapshotring_still_holds_the_frame_a_late_mismatch_report_asks_for)
{
	CRCSnapshotRing ring;

	// the run ahead can be 64 frames; at the default CRC interval the ring has to outlive that
	for( Int i = 1; i <= CRC_SNAPSHOT_RING_SIZE; ++i )
		fillSnapshot( ring, i * 10, 0x1000 + i, 2 );

	CHECK_EQ( ring.getNumSnapshots(), CRC_SNAPSHOT_RING_SIZE );
	CHECK( ring.findSlotByCRC( 0x1000 + 1 ) >= 0 );

	// one more frame pushes the oldest out, and nothing else
	fillSnapshot( ring, (CRC_SNAPSHOT_RING_SIZE + 1) * 10, 0x2000, 2 );
	CHECK_EQ( ring.getNumSnapshots(), CRC_SNAPSHOT_RING_SIZE );
	CHECK_EQ( ring.findSlotByCRC( 0x1000 + 1 ), -1 );
	CHECK( ring.findSlotByCRC( 0x1000 + 2 ) >= 0 );
	CHECK( ring.findSlotByCRC( 0x2000 ) >= 0 );

	// newest first ordering survives the wrap
	const CRCSnapshot *newest = ring.getSlot( ring.getNthNewestSlot( 0 ) );
	const CRCSnapshot *older  = ring.getSlot( ring.getNthNewestSlot( 1 ) );
	CHECK( newest != NULL && older != NULL );
	if( newest == NULL || older == NULL )
		return;
	CHECK_EQ( (Int)newest->m_frame, (CRC_SNAPSHOT_RING_SIZE + 1) * 10 );
	CHECK_EQ( (Int)older->m_frame, CRC_SNAPSHOT_RING_SIZE * 10 );
	CHECK_EQ( ring.getNthNewestSlot( CRC_SNAPSHOT_RING_SIZE ), -1 );
	CHECK_EQ( ring.getNthNewestSlot( -1 ), -1 );
}

TEST(crcsnapshotring_never_hands_back_a_half_written_frame)
{
	CRCSnapshotRing ring;
	fillSnapshot( ring, 30, 0x11111111, 3 );

	// a CRC pass that starts but does not finish must not become the newest snapshot
	ring.beginSnapshot( 60 );
	ring.addObject( 100, 0xDEADBEEF, 0.0f, 0.0f, 0.0f, 1.0f );

	CHECK_EQ( ring.getNumSnapshots(), 1 );
	const CRCSnapshot *snap = ring.getSlot( ring.getNewestSlot() );
	CHECK( snap != NULL );
	if( snap == NULL )
		return;
	CHECK_EQ( (Int)snap->m_frame, 30 );
	CHECK_EQ( ring.findSlotByCRC( 0x11111111 ), ring.getNewestSlot() );

	ring.clear();
	CHECK_EQ( ring.getNumSnapshots(), 0 );
	CHECK_EQ( ring.getNewestSlot(), -1 );
	CHECK_EQ( ring.findSlotByCRC( 0x11111111 ), -1 );

	// addObject with no snapshot open is a no-op, not a write through a bad index
	ring.addObject( 1, 2, 0.0f, 0.0f, 0.0f, 0.0f );
	CHECK_EQ( ring.getNumSnapshots(), 0 );
}

/* --------------------------------------------------------------------------------------------
	 A lockstep game only stays in step if both machines feed the same numbers into it, and the
	 join request is the last moment at which that is a refused join instead of a desynced match.
	 EA wrote the LAN check and left it commented out; it is back, and this pins what it decides. */

TEST(gamedatamatch_lets_identical_data_through_and_stops_everything_else)
{
	// same build, same INI set: the only case that may start a game
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_MATCHES );

	// a different INI set is the usual case, and the one a player can go and fix
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0x11223345, 0xAABBCCDD, 0x11223344 ), GAMEDATA_INI_DIFFERS );

	// a different build reads the same INI files into different code
	CHECK_EQ( compareGameData( 0xAABBCCDE, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_EXE_DIFFERS );

	// one bit is a mismatch: this is a data check, not a similarity score
	CHECK_EQ( compareGameData( 0x11223344, 0x00000001, 0x11223344, 0x00000003 ), GAMEDATA_INI_DIFFERS );
}

TEST(gamedatamatch_blames_the_executable_when_both_differ)
{
	/* Both CRCs differ.  Saying "different INI set" would send the player off to compare data
		 files that their build would read differently anyway. */
	CHECK_EQ( compareGameData( 1, 2, 3, 4 ), GAMEDATA_EXE_DIFFERS );
}

TEST(gamedatamatch_refuses_a_machine_that_reports_no_data_at_all)
{
	/* Nothing computes zero - the executable CRC always folds in the version number and the INI
		 CRC always folds in megabytes of text.  Zero means the other machine never checked. */
	CHECK_EQ( compareGameData( 0, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_UNKNOWN );
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0, 0xAABBCCDD, 0x11223344 ), GAMEDATA_UNKNOWN );

	// and it outranks the CRCs that do match, rather than being reported as a plain match
	CHECK_EQ( compareGameData( 0, 0, 0, 0 ), GAMEDATA_UNKNOWN );

	// every result names itself for the log
	CHECK_STR( gameDataMatchName( GAMEDATA_MATCHES ), "same data" );
	CHECK_STR( gameDataMatchName( GAMEDATA_EXE_DIFFERS ), "different executable or multiplayer scripts" );
	CHECK_STR( gameDataMatchName( GAMEDATA_INI_DIFFERS ), "different INI set" );
	CHECK_STR( gameDataMatchName( GAMEDATA_UNKNOWN ), "data not reported" );
}

/* --------------------------------------------------------------------------------------------
	 Two machines only compute the same floats if the FPU control word says the same thing on both.
	 Nothing in the process guarantees that - Direct3D sets it when it creates a device, and any DLL
	 in the process can set it and never put it back - so GameLogic::update re-asserts it at the top
	 of every logic frame.  These pin what "asserts it" means. */

TEST(setfpmode_pins_the_control_word_from_whatever_it_finds)
{
	UnsignedInt entry = _controlfp( 0, 0 );		// leave the process the way we found it

	// the mode the simulation runs in: 24-bit precision, round to nearest
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );
	CHECK_EQ( getFPMode() & _MCW_PC, (UnsignedInt)(_PC_24 & _MCW_PC) );
	CHECK_EQ( getFPMode() & _MCW_RC, (UnsignedInt)(_RC_NEAR & _MCW_RC) );

	// what a driver that grabbed the FPU and never gave it back looks like
	_controlfp( _PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC );
	CHECK_NE( getFPMode(), expectedFPMode() );
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	// and the other direction, so this is not just "setFPMode lowers the precision"
	_controlfp( _PC_53 | _RC_UP, _MCW_PC | _MCW_RC );
	CHECK_NE( getFPMode(), expectedFPMode() );
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	// it is idempotent - a second logic frame does not move it
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	_controlfp( entry, _MCW_PC | _MCW_RC );
}

TEST(setfpmode_leaves_the_exception_mask_in_a_known_state)
{
	UnsignedInt entry = _controlfp( 0, 0 );

	/* setFPMode writes only the precision and rounding fields, but it calls _fpreset() first, so
		 the rest of the word - the exception masks above all - lands in the same known state on
		 every machine rather than wherever the last DLL left it.  An unmasked invalid-operation
		 exception on one machine and a masked one on another is a crash on one side and a quiet NaN
		 on the other. */
	_controlfp( 0, _MCW_EM );		// unmask everything: the state a debugger or a bad DLL can leave
	setFPMode();
	CHECK_EQ( _controlfp( 0, 0 ) & _MCW_EM, (UnsignedInt)_MCW_EM );

	// and the x87 stack is reset with it, so an __asm block that pushed and forgot to pop is
	// not carried into the next logic frame
	CHECK_EQ( getFPMode(), expectedFPMode() );

	_controlfp( entry, _MCW_PC | _MCW_RC | _MCW_EM );
}

// ---------------------------------------------------------------------------------------------
// The disconnect screen's decision (MULTIPLAYER 2.3).  DisconnectManager::update used to bring
// the screen up on stall duration alone, which cannot tell a slow game from a broken one.
// ---------------------------------------------------------------------------------------------

// the thresholds the shipped GlobalData defaults use, so the witnesses below describe the game
static const UnsignedInt DISCONNECT_MS = 8000;
static const UnsignedInt SILENCE_MS    = 12000;
static const UnsignedInt WEDGED_MS     = 20000;

static StallVerdict judge( UnsignedInt stallMS, UnsignedInt silenceMS )
{
	return judgeStall( stallMS, silenceMS, DISCONNECT_MS, SILENCE_MS, WEDGED_MS );
}

TEST(judgestall_a_short_stall_is_not_a_disconnect)
{
	// the common case: the game is waiting on a frame and everybody is still sending
	CHECK_EQ( (int)judge( 0, 0 ), (int)STALL_RUNNING );
	CHECK_EQ( (int)judge( 4000, 500 ), (int)STALL_RUNNING );
	CHECK_EQ( (int)judge( DISCONNECT_MS, 500 ), (int)STALL_RUNNING );	// boundary is exclusive

	// and a short stall stays running even if somebody has been quiet a while: below the
	// disconnect time we do not look at silence at all, because we are not stalled yet
	CHECK_EQ( (int)judge( 1000, 30000 ), (int)STALL_RUNNING );

	CHECK( !stallNeedsDisconnectScreen( STALL_RUNNING ) );
}

TEST(judgestall_a_long_stall_with_everyone_talking_is_only_slow)
{
	// this is the case EA got wrong: 5s of no frame progress, but packets are still arriving from
	// every player, so the game is behind and will catch up.  No screen, no vote, no drop.
	CHECK_EQ( (int)judge( DISCONNECT_MS + 1, 0 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( 15000, 3000 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( WEDGED_MS - 1, SILENCE_MS - 1 ), (int)STALL_WAITING );

	CHECK( !stallNeedsDisconnectScreen( STALL_WAITING ) );
}

TEST(judgestall_silence_from_a_player_is_a_disconnect)
{
	// stalled, and somebody has stopped sending entirely - that is what the screen is for
	CHECK_EQ( (int)judge( DISCONNECT_MS + 1, SILENCE_MS ), (int)STALL_SILENT );
	CHECK_EQ( (int)judge( 9000, 60000 ), (int)STALL_SILENT );

	CHECK( stallNeedsDisconnectScreen( STALL_SILENT ) );
}

TEST(judgestall_silence_shorter_than_the_keepalive_round_is_not_silence)
{
	// ConnectionManager::doKeepAlive walks one slot per second and resets at MAX_SLOTS, so a
	// player who is merely stalled themselves still only reaches us every 8s.  The silence
	// threshold has to sit above that or the fix would call a slow player a disconnected one.
	CHECK( SILENCE_MS > 8000 );
	CHECK_EQ( (int)judge( 19000, 8000 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( 19000, 11999 ), (int)STALL_WAITING );
}

TEST(judgestall_a_stall_past_the_ceiling_gives_up_anyway)
{
	// packets are arriving but the frame has not moved in 20 seconds: whatever is wrong, it is
	// not going to fix itself, and refusing to ever show the screen would hang the game forever
	CHECK_EQ( (int)judge( WEDGED_MS, 0 ), (int)STALL_WEDGED );
	CHECK_EQ( (int)judge( 120000, 0 ), (int)STALL_WEDGED );

	CHECK( stallNeedsDisconnectScreen( STALL_WEDGED ) );

	// silence still wins over the ceiling: the report should name the real cause
	CHECK_EQ( (int)judge( 120000, SILENCE_MS ), (int)STALL_SILENT );
}

TEST(judgestall_the_verdict_is_monotonic_in_the_stall_time)
{
	// once the screen is warranted, waiting longer must never take it back away again
	Bool seenScreen = FALSE;
	for( UnsignedInt ms = 0; ms <= 40000; ms += 250 )
	{
		Bool needs = stallNeedsDisconnectScreen( judge( ms, 30000 ) );
		if( needs )
			seenScreen = TRUE;
		else
			CHECK( !seenScreen );
	}
	CHECK( seenScreen );

	// every verdict has a name for the log
	CHECK( strlen( stallVerdictName( STALL_RUNNING ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_WAITING ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_SILENT ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_WEDGED ) ) > 0 );
}

// ---------------------------------------------------------------------------------------------
// The keep-alive round (MULTIPLAYER 2.4).  NetworkKeepAliveDelay was parsed, logged and never
// read; doKeepAlive counted whole seconds against MAX_SLOTS in two function statics instead.
// ---------------------------------------------------------------------------------------------

TEST(keepalive_the_round_is_bounded_whatever_the_ini_says)
{
	// the shipped GameData.ini asks for 20 s, which is exactly where consumer NAT mappings of the
	// era expired - refreshing the hole at the moment it closes is no refresh at all
	CHECK_EQ( keepAliveRoundMS( 20 ), (UnsignedInt)KEEPALIVE_MAX_ROUND_SECONDS * 1000 );
	CHECK_EQ( keepAliveRoundMS( 3600 ), (UnsignedInt)KEEPALIVE_MAX_ROUND_SECONDS * 1000 );

	// and a zero would be a divide by zero one function along
	CHECK_EQ( keepAliveRoundMS( 0 ), (UnsignedInt)KEEPALIVE_MIN_ROUND_SECONDS * 1000 );
	CHECK_EQ( keepAliveRoundMS( 1 ), (UnsignedInt)KEEPALIVE_MIN_ROUND_SECONDS * 1000 );

	// in between, the knob is the knob
	CHECK_EQ( keepAliveRoundMS( 4 ), (UnsignedInt)4000 );
	CHECK_EQ( keepAliveRoundMS( 8 ), (UnsignedInt)8000 );

	// the ceiling is what EA's counting loop actually produced, so the default behaviour is the
	// behaviour the game shipped with
	CHECK_EQ( (int)KEEPALIVE_MAX_ROUND_SECONDS, 8 );
	CHECK( KEEPALIVE_MIN_ROUND_SECONDS < KEEPALIVE_MAX_ROUND_SECONDS );
}

TEST(keepalive_slots_are_staggered_across_the_round)
{
	// eight slots over an eight second round: one per second, which is what EA's loop did
	CHECK_EQ( keepAliveSlotsDue( 0, 8000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 999, 8000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 1000, 8000, 8 ), 2 );
	CHECK_EQ( keepAliveSlotsDue( 6999, 8000, 8 ), 7 );
	CHECK_EQ( keepAliveSlotsDue( 7000, 8000, 8 ), 8 );

	// past the end of the round nothing more comes due - the round restarts instead
	CHECK_EQ( keepAliveSlotsDue( 8000, 8000, 8 ), 8 );
	CHECK_EQ( keepAliveSlotsDue( 100000, 8000, 8 ), 8 );

	// a shorter round packs the same eight into less time rather than dropping any
	CHECK_EQ( keepAliveSlotsDue( 0, 2000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 250, 2000, 8 ), 2 );
	CHECK_EQ( keepAliveSlotsDue( 1750, 2000, 8 ), 8 );
}

TEST(keepalive_the_count_never_goes_backwards_and_never_overruns)
{
	// doKeepAlive walks m_keepAliveNextSlot up to this number and indexes m_connections with it,
	// so a value outside [0, MAX_SLOTS] would be an out of bounds write in a network path
	Int last = 0;
	for( UnsignedInt ms = 0; ms <= 20000; ms += 17 )
	{
		Int due = keepAliveSlotsDue( ms, 8000, 8 );
		CHECK( due >= last );
		CHECK( due >= 0 && due <= 8 );
		last = due;
	}
	CHECK_EQ( last, 8 );

	// degenerate inputs cannot produce an index either
	CHECK_EQ( keepAliveSlotsDue( 0, 8000, 0 ), 0 );
	CHECK_EQ( keepAliveSlotsDue( 0, 0, 8 ), 8 );			// round shorter than a slot: all at once
	CHECK_EQ( keepAliveSlotsDue( 0, 4, 8 ), 8 );
}

TEST(keepalive_every_player_gets_one_within_the_round)
{
	// the property that matters to a NAT: no slot waits longer than the round for its packet
	UnsignedInt roundMS = keepAliveRoundMS( 20 );		// what the shipped INI produces
	CHECK( !keepAliveRoundIsOver( roundMS - 1, roundMS ) );
	CHECK( keepAliveRoundIsOver( roundMS, roundMS ) );

	// the last slot is due strictly before the round ends, so it is never skipped by the restart
	CHECK_EQ( keepAliveSlotsDue( roundMS - 1, roundMS, 8 ), 8 );
	CHECK( roundMS <= 15000 );		// under the shortest NAT UDP timeouts seen in the wild
}

// ---------------------------------------------------------------------------------------------
// The synthetic link simulator (MULTIPLAYER 6).  The transport can hold packets back and throw
// some away, so a lossy or slow line can be reproduced without a second machine.  Both decisions
// are pure, so both can be pinned here - including the two things EA got wrong in them, which
// nobody could have noticed because the whole facility was compiled out of the shipping build.
// ---------------------------------------------------------------------------------------------

TEST(linksim_packet_loss_percentage_is_the_percentage)
{
	// the count of losing rolls over the hundred a GameClientRandomValue(1, 100) can produce is
	// the percentage itself.  EA rolled (0, 100) - a hundred and one outcomes - against the same
	// comparison, so every setting was one point too lossy...
	Int pct;
	for( pct = 0; pct <= 100; pct++ )
	{
		Int lost = 0;
		for( Int roll = 1; roll <= 100; roll++ )
			if (linkSimPacketIsLost( pct, roll ))
				lost++;
		CHECK_EQ( lost, pct );
	}

	// ...and the setting that says "do not drop anything" dropped one packet in a hundred and one
	CHECK( !linkSimPacketIsLost( 0, 1 ) );
	CHECK( linkSimPacketIsLost( 100, 100 ) );
	CHECK( linkSimPacketIsLost( 1, 1 ) );
	CHECK( !linkSimPacketIsLost( 1, 2 ) );
}

TEST(linksim_delivery_is_the_average_plus_the_jitter)
{
	// no modulation asked for: the delay is exactly what was asked for
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, 0 ), 1040u );
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, 15 ), 1055u );
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, -15 ), 1025u );

	// an amplitude with no period is not a modulation, and must not become one
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 500, 0, 0 ), 1040u );

	// nothing is ever delivered before it arrived, however the jitter falls
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, -400 ), 1000u );
	CHECK_EQ( linkSimDeliveryTime( 0, 0, 0, 0, -400 ), 0u );
}

TEST(linksim_the_modulation_has_the_period_it_says_it_has)
{
	// a period in milliseconds, read the way the field documents itself: zero phase at the start of
	// every period, the peak a quarter of the way in, the trough three quarters in.
	const Int period = 8000;
	const Int amp = 100;

	CHECK_EQ( linkSimDeliveryTime( 0, 200, amp, period, 0 ), 200u );					// sin 0
	CHECK_EQ( linkSimDeliveryTime( period/4, 200, amp, period, 0 ), (UnsignedInt)(period/4 + 300) );	// sin pi/2
	CHECK_EQ( linkSimDeliveryTime( period/2, 200, amp, period, 0 ), (UnsignedInt)(period/2 + 200) );	// sin pi
	CHECK_EQ( linkSimDeliveryTime( 3*period/4, 200, amp, period, 0 ), (UnsignedInt)(3*period/4 + 100) );	// sin 3pi/2

	// and it repeats: the same point of the next period gives the same delay
	CHECK_EQ( linkSimDeliveryTime( period, 200, amp, period, 0 ) - period, 200u );
	CHECK_EQ( linkSimDeliveryTime( period + period/4, 200, amp, period, 0 ) - (period + period/4), 300u );

	/* EA's own line was sin(now * period), which at any usable setting moves the argument by whole
		 radians every millisecond - a second noise source, not a modulation - and overflows an
		 UnsignedInt after a minute of uptime.  The property that pins the difference: over one
		 period the delay is a single smooth hump, so consecutive milliseconds differ by almost
		 nothing.  Under EA's reading they differ by the whole amplitude. */
	UnsignedInt a = linkSimDeliveryTime( 1000000, 200, amp, period, 0 ) - 1000000;
	UnsignedInt b = linkSimDeliveryTime( 1000001, 200, amp, period, 0 ) - 1000001;
	CHECK( (a > b ? a - b : b - a) <= 1u );

	// still true where EA's version wrapped: uptimes past the point now * period overflows
	UnsignedInt late = 0xFFFFF000u;
	UnsignedInt c = linkSimDeliveryTime( late, 200, amp, period, 0 ) - late;
	UnsignedInt d = linkSimDeliveryTime( late + 1, 200, amp, period, 0 ) - (late + 1);
	CHECK( c >= 100u && c <= 300u );
	CHECK( (c > d ? c - d : d - c) <= 1u );
}

// ---------------------------------------------------------------------------------------------
// Whether the room actually disagreed about a frame (MULTIPLAYER 1.1, upstream #2796).
// ---------------------------------------------------------------------------------------------

TEST(crcagreement_everyone_still_playing_has_to_have_reported)
{
	Bool reported[4]  = { TRUE, TRUE, TRUE, FALSE };
	Bool connected[4] = { TRUE, TRUE, TRUE, TRUE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x1111u, 0u };

	// four connected players, three hashes: the fourth packet is not here yet, which is not a
	// mismatch.  Decide on the next CRC frame instead.
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_TOO_FEW );

	// once it arrives, and it agrees
	reported[3] = TRUE;
	crc[3] = 0x1111u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_AGREE );
}

TEST(crcagreement_a_player_who_has_left_does_not_get_a_vote)
{
	// slot 3 is gone, but their last hash - computed on a machine that was already tearing the
	// game down - arrived this frame.  EA compared it against everybody who is still playing and
	// ended the match on it.
	Bool reported[4]  = { TRUE, TRUE, TRUE, TRUE };
	Bool connected[4] = { TRUE, TRUE, TRUE, FALSE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x1111u, 0xDEADBEEFu };

	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_AGREE );

	// the leaver is also not counted towards the quorum: three connected players, and the two
	// hashes that are here are not enough.
	reported[2] = FALSE;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_TOO_FEW );
}

TEST(crcagreement_two_players_who_are_both_here_and_disagree_is_a_mismatch)
{
	Bool reported[4]  = { TRUE, TRUE, TRUE, TRUE };
	Bool connected[4] = { TRUE, TRUE, TRUE, TRUE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x2222u, 0x1111u };

	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_MISMATCH );

	// the disagreement is found wherever it sits, including against the first reporter
	crc[0] = 0x2222u; crc[1] = 0x1111u; crc[2] = 0x1111u; crc[3] = 0x1111u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_MISMATCH );

	// but only between players who are both still connected
	connected[0] = FALSE;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_AGREE );
}

TEST(crcagreement_a_room_with_nobody_left_in_it_is_not_a_mismatch)
{
	Bool reported[2]  = { FALSE, FALSE };
	Bool connected[2] = { FALSE, FALSE };
	UnsignedInt crc[2] = { 0u, 0u };

	// nothing to compare, and nothing expected: agreement by default, never a mismatch.  EA read
	// begin() unconditionally here.
	CHECK_EQ( crcAgreement( reported, crc, connected, 2, 0 ), CRC_AGREEMENT_AGREE );

	// one player alone in the room agrees with themselves
	reported[0] = TRUE; connected[0] = TRUE; crc[0] = 0x99u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 2, 1 ), CRC_AGREEMENT_AGREE );
}

// ---------------------------------------------------------------------------------------------
// Reading the shared random stream without spending it.  Client-side effects - a sound the player
// muted, a particle the detail settings threw away - used to roll from the logic stream, so
// whether they happened at all changed how the simulation unfolded on that machine.
// ---------------------------------------------------------------------------------------------

TEST(logicrandom_unchanged_does_not_move_the_shared_seed)
{
	InitRandom( 12345 );
	const UnsignedInt before = GetGameLogicRandomSeedCRC();

	for (int i = 0; i < 50; ++i)
	{
		GetGameLogicRandomValueUnchanged( 0, 99 );
		GetGameLogicRandomValueRealUnchanged( 0.0f, 1.0f );
	}

	// the whole point: a machine that made these calls and a machine that skipped them are still
	// holding the same seed, so they still agree about the next thing the simulation rolls.
	CHECK_EQ( GetGameLogicRandomSeedCRC(), before );

	// and the ordinary call still does move it, or the check above proves nothing
	GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );
	CHECK_NE( GetGameLogicRandomSeedCRC(), before );
}

TEST(logicrandom_unchanged_is_the_same_answer_on_every_machine)
{
	InitRandom( 777 );
	const Int first = GetGameLogicRandomValueUnchanged( 0, 1000000 );

	// same seed state, same answer - repeatedly, because it does not consume anything
	for (int i = 0; i < 10; ++i)
		CHECK_EQ( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );

	// a second machine that reached this point through the same logic holds the same seed and gets
	// the same answer, which is what made this safe to use for a scripted sound's variant pick
	InitRandom( 777 );
	CHECK_EQ( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );

	// once the simulation itself rolls, the answer moves on with it
	GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );
	InitRandom( 778 );
	CHECK_NE( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );
}

TEST(logicrandom_unchanged_honours_its_bounds)
{
	InitRandom( 4242 );
	for (int i = 0; i < 200; ++i)
	{
		GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );	// walk the seed along

		const Int n = GetGameLogicRandomValueUnchanged( 7, 11 );
		CHECK( n >= 7 && n <= 11 );

		const Real r = GetGameLogicRandomValueRealUnchanged( -2.0f, 2.0f );
		CHECK( r >= -2.0f && r <= 2.0f );
	}

	// degenerate ranges answer the way the ordinary versions do
	CHECK_EQ( GetGameLogicRandomValueUnchanged( 5, 5 ), 5 );
	CHECK_EQ( GetGameLogicRandomValueRealUnchanged( 3.0f, 3.0f ), 3.0f );
}

// ---------------------------------------------------------------------------------------------
// Looking a script up by name.  This used to walk every side's script list and every group inside
// it, on every call, and a subroutine call does it twice.
// ---------------------------------------------------------------------------------------------

TEST(scriptengine_finds_a_script_by_name_wherever_it_lives)
{
	SidesList sides;
	SidesList *savedSides = TheSidesList;
	TheSidesList = &sides;

	Dict emptyDict;
	sides.addSide( &emptyDict );
	sides.addSide( &emptyDict );

	// side 0: one loose script, and one inside a group
	ScriptList *list0 = newInstance(ScriptList);
	sides.getSideInfo( 0 )->setScriptList( list0 );

	Script *loose = newInstance(Script);
	loose->setName( AsciiString( "LooseScript" ) );
	list0->addScript( loose, 0 );

	ScriptGroup *group = newInstance(ScriptGroup);
	group->setName( AsciiString( "TheGroup" ) );
	list0->addGroup( group, 0 );

	Script *inGroup = newInstance(Script);
	inGroup->setName( AsciiString( "GroupedScript" ) );
	group->addScript( inGroup, 0 );

	// side 1: another loose one, to prove the search does not stop at the first side
	ScriptList *list1 = newInstance(ScriptList);
	sides.getSideInfo( 1 )->setScriptList( list1 );

	Script *otherSide = newInstance(Script);
	otherSide->setName( AsciiString( "OtherSideScript" ) );
	list1->addScript( otherSide, 0 );

	ScriptEngine engine;
	CHECK( engine.findScriptByName( AsciiString( "LooseScript" ) ) == loose );
	CHECK( engine.findScriptByName( AsciiString( "GroupedScript" ) ) == inGroup );
	CHECK( engine.findScriptByName( AsciiString( "OtherSideScript" ) ) == otherSide );
	CHECK( engine.findScriptByName( AsciiString( "NoSuchScript" ) ) == NULL );
	CHECK( engine.findScriptByName( AsciiString() ) == NULL );

	// asking twice must answer the same, whether the index was built on this call or the last one
	CHECK( engine.findScriptByName( AsciiString( "GroupedScript" ) ) == inGroup );

	// a new map throws the index away; the same engine must pick up the new scripts
	sides.getSideInfo( 0 )->setScriptList( NULL );
	list0->deleteInstance();
	sides.getSideInfo( 1 )->setScriptList( NULL );
	list1->deleteInstance();

	ScriptList *reloaded = newInstance(ScriptList);
	sides.getSideInfo( 0 )->setScriptList( reloaded );
	Script *afterLoad = newInstance(Script);
	afterLoad->setName( AsciiString( "AfterLoad" ) );
	reloaded->addScript( afterLoad, 0 );

	engine.newMap();
	CHECK( engine.findScriptByName( AsciiString( "AfterLoad" ) ) == afterLoad );
	CHECK( engine.findScriptByName( AsciiString( "LooseScript" ) ) == NULL );

	sides.getSideInfo( 0 )->setScriptList( NULL );
	reloaded->deleteInstance();
	TheSidesList = savedSides;
}

// ---------------------------------------------------------------------------------------------
// The particle system manager's bookkeeping.  Finding a system by id, and unlinking a dead one,
// both used to be linear walks of every live system - on paths the renderer and the logic take
// thousands of times a frame.
// ---------------------------------------------------------------------------------------------

// A particle system stamps itself with TheGameClient's frame, so the test needs one of those too.
// The base class' constructor only zeroes counters, but its destructor tears down half the world's
// globals (the shell, the in-game UI, the campaign manager), so the stub is made once and never
// destroyed.
class TestGameClient : public GameClient
{
public:
	virtual void createRayEffectByTemplate( const Coord3D *, const Coord3D *, const ThingTemplate * ) {}
	virtual void addScorch( const Coord3D *, Real, Scorches ) {}
	virtual Drawable *friend_createDrawable( const ThingTemplate *, DrawableStatus = DRAWABLE_STATUS_NONE ) { return NULL; }
	virtual void setTeamColor( Int, Int, Int ) {}
	virtual void adjustLOD( Int ) {}
	virtual void notifyTerrainObjectMoved( Object * ) {}
	virtual Display *createGameDisplay( void ) { return NULL; }
	virtual InGameUI *createInGameUI( void ) { return NULL; }
	virtual GameWindowManager *createWindowManager( void ) { return NULL; }
	virtual FontLibrary *createFontLibrary( void ) { return NULL; }
	virtual DisplayStringManager *createDisplayStringManager( void ) { return NULL; }
	virtual VideoPlayerInterface *createVideoPlayer( void ) { return NULL; }
	virtual TerrainVisual *createTerrainVisual( void ) { return NULL; }
	virtual Keyboard *createKeyboard( void ) { return NULL; }
	virtual Mouse *createMouse( void ) { return NULL; }
	virtual SnowManager *createSnowManager( void ) { return NULL; }
	virtual void setFrameRate( Real ) {}
};

static GameClient *theTestGameClient = NULL;

static GameClient *getTestGameClient( void )
{
	if( theTestGameClient == NULL )
		theTestGameClient = new TestGameClient;
	return theTestGameClient;
}

// The real managers live in GameEngineDevice; all we need is something concrete to file systems in.
class TestParticleSystemManager : public ParticleSystemManager
{
public:
	virtual Int getOnScreenParticleCount( void ) { return 0; }
	virtual void doParticles( RenderInfoClass & ) {}
	virtual void queueParticleRender() {}
	virtual void preloadAssets( TimeOfDay ) {}
};

TEST(particlesys_find_answers_by_id_and_forgets_a_dead_system)
{
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	ParticleSystemManager *savedManager = TheParticleSystemManager;

	{
		TestParticleSystemManager mgr;
		TheParticleSystemManager = &mgr;

		ParticleSystemTemplate *tmpl = mgr.newTemplate( AsciiString( "TestParticleSystem" ) );
		CHECK( tmpl != NULL );

		ParticleSystem *a = mgr.createParticleSystem( tmpl, FALSE );
		ParticleSystem *b = mgr.createParticleSystem( tmpl, FALSE );
		ParticleSystem *c = mgr.createParticleSystem( tmpl, FALSE );
		CHECK( a != NULL && b != NULL && c != NULL );

		const ParticleSystemID idA = a->getSystemID();
		const ParticleSystemID idB = b->getSystemID();
		const ParticleSystemID idC = c->getSystemID();
		CHECK( idA != idB && idB != idC && idA != idC );

		CHECK( mgr.findParticleSystem( idA ) == a );
		CHECK( mgr.findParticleSystem( idB ) == b );
		CHECK( mgr.findParticleSystem( idC ) == c );
		CHECK( mgr.findParticleSystem( (ParticleSystemID)0x7fffffff ) == NULL );
		CHECK( mgr.findParticleSystem( INVALID_PARTICLE_SYSTEM_ID ) == NULL );
		CHECK_EQ( mgr.getParticleSystemCount(), 3 );

		// killing one out of the middle must not disturb the other two, and its id must stop resolving
		b->deleteInstance();
		CHECK( mgr.findParticleSystem( idB ) == NULL );
		CHECK( mgr.findParticleSystem( idA ) == a );
		CHECK( mgr.findParticleSystem( idC ) == c );
		CHECK_EQ( mgr.getParticleSystemCount(), 2 );

		a->deleteInstance();
		c->deleteInstance();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );
		CHECK( mgr.findParticleSystem( idA ) == NULL );
		CHECK( mgr.findParticleSystem( idC ) == NULL );
	}

	TheParticleSystemManager = savedManager;
	TheGameClient = savedClient;
}

TEST(particlesys_reset_lets_the_ids_start_over_without_ghosts)
{
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	ParticleSystemManager *savedManager = TheParticleSystemManager;

	{
		TestParticleSystemManager mgr;
		TheParticleSystemManager = &mgr;

		ParticleSystemTemplate *tmpl = mgr.newTemplate( AsciiString( "TestParticleSystem" ) );
		CHECK( tmpl != NULL );

		for( Int i = 0; i < 8; ++i )
			CHECK( mgr.createParticleSystem( tmpl, FALSE ) != NULL );
		CHECK_EQ( mgr.getParticleSystemCount(), 8 );

		mgr.reset();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );

		// reset rewinds the id counter, so an entry left behind would answer for a brand new system
		ParticleSystem *fresh = mgr.createParticleSystem( tmpl, FALSE );
		CHECK( fresh != NULL );
		CHECK( mgr.findParticleSystem( fresh->getSystemID() ) == fresh );

		mgr.reset();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );
	}

	TheParticleSystemManager = savedManager;
	TheGameClient = savedClient;
}

// ---------------------------------------------------------------------------------------------
// Per-game state that a reused Player has to give back.
// ---------------------------------------------------------------------------------------------

TEST(energy_a_new_game_does_not_inherit_the_last_games_sabotage)
{
	Energy e;

	// a sabotage that runs to frame 30000 of the game that is being left
	e.setPowerSabotagedTillFrame( 30000 );
	CHECK_EQ( e.getPowerSabotagedTillFrame(), 30000u );

	// the Player array is reused, so the next game gets init() rather than a constructor.  EA left
	// the stamp behind: the new game's frame counter starts at 0, so 30000 is still in the future
	// and that player produced no power at all until frame 30000 - on the machines that saw the
	// sabotage, and nowhere else.
	e.init( NULL );
	CHECK_EQ( e.getPowerSabotagedTillFrame(), 0u );
}

// ---------------------------------------------------------------------------------------------
// The cushion, and the self-slug it drives (MULTIPLAYER 3.3).  Two type defects met in the
// middle here: an unsigned frame subtraction that could wrap, and an Int sentinel returned
// through an UnsignedInt.
// ---------------------------------------------------------------------------------------------

TEST(framecushion_is_the_margin_and_never_negative)
{
	CHECK_EQ( frameCushion( 100, 100 ), 0 );
	CHECK_EQ( frameCushion( 130, 100 ), 30 );
	CHECK_EQ( frameCushion( 100, 99 ), 1 );

	// the defect: a command for a frame that has already run.  EA's unsigned subtraction made this
	// 4294967295, which arrives at FrameMetrics::addCushion(Int) as -1 - that class's "no sample
	// yet" sentinel - and wiped the minimum cushion for the window.
	CHECK_EQ( frameCushion( 99, 100 ), 0 );
	CHECK_EQ( frameCushion( 0, 1 ), 0 );
	CHECK_EQ( frameCushion( 100, 5000 ), 0 );

	// and the frame counter is unsigned, so the difference has to survive its wraparound too
	CHECK_EQ( frameCushion( 5, 0xFFFFFFFEu ), 7 );
	CHECK_EQ( frameCushion( 0xFFFFFFFEu, 5 ), 0 );
}

TEST(selfslug_does_not_fire_on_a_cushion_nobody_has_measured)
{
	// FrameMetrics::init leaves the minimum cushion at -1.  ConnectionManager::getMinimumCushion
	// returned UnsignedInt, so that reached Network::timeForNewFrame as four billion frames of
	// margin: the largest cushion representable, produced by the state that knows the least.
	CHECK( !shouldSelfSlug( -1, 20, 10 ) );
	CHECK( !shouldSelfSlug( -100, 20, 10 ) );

	// nor on a run-ahead that has not been established
	CHECK( !shouldSelfSlug( 0, 0, 10 ) );
	CHECK( !shouldSelfSlug( 0, -1, 10 ) );
}

TEST(selfslug_fires_once_the_margin_eats_into_the_slack)
{
	// run-ahead 20, slack 10% -> the threshold is 2 frames of margin
	CHECK( shouldSelfSlug( 0, 20, 10 ) );
	CHECK( shouldSelfSlug( 1, 20, 10 ) );
	CHECK( !shouldSelfSlug( 2, 20, 10 ) );
	CHECK( !shouldSelfSlug( 19, 20, 10 ) );

	// a bigger run-ahead is given proportionally more margin before it worries
	CHECK( shouldSelfSlug( 5, 60, 10 ) );
	CHECK( !shouldSelfSlug( 6, 60, 10 ) );

	// a run-ahead too small for the slack to round to a whole frame still gets the brake: see
	// selfslug_threshold_has_a_floor_the_shipped_run_ahead_cannot_undercut
	CHECK( shouldSelfSlug( 0, 9, 10 ) );
	CHECK( shouldSelfSlug( 1, 9, 10 ) );
	CHECK( !shouldSelfSlug( 2, 9, 10 ) );
}

TEST(selfslug_threshold_has_a_floor_the_shipped_run_ahead_cannot_undercut)
{
	/* The run-ahead a real room plays on is a handful of frames - MIN_RUNAHEAD is four, and
		 computeRunAhead only clears that above about 240 ms round trip - so NetworkRunAheadSlack's
		 10 % of it is worth well under a frame.  A brake that waits until less than one frame of
		 margin is left has already lost: at 30 Hz that is under 33 ms of warning for a hitch that
		 takes longer than that to signal, let alone correct.  Hence the floor. */
	CHECK_EQ( selfSlugThreshold( 10, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );

	// the floor holds wherever the arithmetic would land under it, including at zero slack
	CHECK_EQ( selfSlugThreshold( 10, 0 ), SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK_EQ( selfSlugThreshold( 1, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK_EQ( selfSlugThreshold( 19, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );

	// and gets out of the way as soon as the configured slack is worth more than it
	CHECK_EQ( selfSlugThreshold( 30, 10 ), 3 );
	CHECK_EQ( selfSlugThreshold( 64, 10 ), 6 );
	CHECK_EQ( selfSlugThreshold( 20, 50 ), 10 );

	// the floor is what shouldSelfSlug actually uses - no second copy of the arithmetic
	for( Int runAhead = 1; runAhead <= 64; ++runAhead )
	{
		Int threshold = selfSlugThreshold( runAhead, 10 );
		CHECK( threshold >= SELFSLUG_MIN_THRESHOLD_FRAMES );
		CHECK( shouldSelfSlug( threshold - 1, runAhead, 10 ) );
		CHECK( !shouldSelfSlug( threshold, runAhead, 10 ) );
	}

	// a floor is a floor, not a slug-always: the brake still lets go
	CHECK( !shouldSelfSlug( SELFSLUG_MIN_THRESHOLD_FRAMES, 10, 10 ) );
}

/* How far ahead the room schedules its commands.  This is the number that decides whether a
	 click is answered promptly or the match stalls waiting for a packet, and until now it was
	 neither: the floor under it (MIN_RUNAHEAD, ten frames) was larger than the formula's own answer
	 on every link anyone plays on, so every game ran at 333 ms of input delay and the arithmetic
	 that was supposed to adapt the window never got a vote. */

TEST(the_run_ahead_covers_the_trip_it_is_sized_for)
{
	/* The one thing a run-ahead must never do is come out shorter than the wire.  getMaximumLatency
		 sums the two worst average round trips, so the trip a command has to survive is half of it,
		 and the window has to cover that at the rate the room is running.  EA truncated the division:
		 a 150 ms round trip is 2.25 frames of wire at 30 Hz and truncates to 2 - 66 ms of window for
		 75 ms of travel, which arrives late every single time. */
	for( Int ms = 0; ms <= 800; ms += 5 )
	{
		Real latency = (Real)ms / 1000.0f;
		for( Int fps = 5; fps <= 30; fps += 5 )
		{
			Int runAhead = computeRunAhead( latency, fps, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );

			// the window, in seconds, against the one-way trip it has to cover
			CHECK( (Real)runAhead / (Real)fps >= (latency / 2.0f) );
		}
	}
}

TEST(the_run_ahead_carries_a_margin_the_percentage_alone_cannot_give)
{
	/* An average round trip is exceeded half the time, so a window sized to exactly the average is
		 wrong half the time.  The proportional slack was meant to be that margin and cannot be: 10 %
		 of a four frame window is zero frames, 10 % of a ten frame window is one.  The fixed
		 allowance is what actually covers the jitter, and it is what makes a low floor safe. */
	for( Int ms = 0; ms <= 800; ms += 5 )
	{
		Real latency = (Real)ms / 1000.0f;
		Int runAhead = computeRunAhead( latency, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );

		// at least RUNAHEAD_JITTER_FRAMES of room beyond the trip itself, at every latency
		Real slackSeconds = ( (Real)runAhead / 30.0f ) - ( latency / 2.0f );
		CHECK( slackSeconds >= ( (Real)RUNAHEAD_JITTER_FRAMES / 30.0f ) - 0.0005f );
	}
}

TEST(the_run_ahead_is_shorter_than_the_shipped_floor_on_the_links_that_do_not_need_it)
{
	/* What the change is for.  A LAN game and a good broadband game used to be given the same third
		 of a second of input delay as a transatlantic one, because the floor was ten frames and the
		 formula never beat it. */
	const Int shippedFloor = 10;			// EA's MIN_RUNAHEAD, 333 ms of input delay at 30 Hz

	CHECK( computeRunAhead( 0.000f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// LAN
	CHECK( computeRunAhead( 0.040f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 40 ms
	CHECK( computeRunAhead( 0.100f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 100 ms
	CHECK( computeRunAhead( 0.160f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 160 ms

	// and the shortest of them is the floor itself, not something under it
	CHECK_EQ( computeRunAhead( 0.000f, 30, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );

	/* And what it is not: the links that were relying on the floor get more window than the floor
		 gave them, not less.  600 ms summed round trip is where EA's formula finally reached ten. */
	CHECK( computeRunAhead( 0.300f, 30, 10, MIN_RUNAHEAD, 64 ) >= 7 );
	CHECK( computeRunAhead( 0.600f, 30, 10, MIN_RUNAHEAD, 64 ) > shippedFloor );
	CHECK( computeRunAhead( 1.000f, 30, 10, MIN_RUNAHEAD, 64 ) > 16 );
}

TEST(the_run_ahead_stays_inside_the_bounds_the_network_buffers_are_built_for)
{
	// the window indexes frame buffers sized from MAX_FRAMES_AHEAD; it may not walk out of them
	for( Int ms = 0; ms <= 20000; ms += 25 )
	{
		Int runAhead = computeRunAhead( (Real)ms / 1000.0f, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );
		CHECK( runAhead >= MIN_RUNAHEAD );
		CHECK( runAhead <= MAX_FRAMES_AHEAD / 2 );
	}

	// a rate of zero is not a division by zero, and a negative latency is not a negative window
	CHECK_EQ( computeRunAhead( 0.0f, 0, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );
	CHECK_EQ( computeRunAhead( -1.0f, 30, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );
}

TEST(the_shipped_run_ahead_floor_leaves_the_self_slug_brake_room_to_work)
{
	/* The floor and the brake are one decision, not two.  The self-slug fires when the measured
		 cushion drops below selfSlugThreshold(runAhead), so a floor at or below that threshold means
		 a room at its shortest window is braking permanently - the stall the brake exists to avoid,
		 applied continuously. */
	CHECK( MIN_RUNAHEAD > SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK( selfSlugThreshold( MIN_RUNAHEAD, 10 ) < MIN_RUNAHEAD );
	CHECK( !shouldSelfSlug( MIN_RUNAHEAD, MIN_RUNAHEAD, 10 ) );

	// and the buffers the window is drawn from are big enough for the largest window it can ask for
	CHECK( MIN_RUNAHEAD <= MAX_FRAMES_AHEAD / 2 );
	CHECK( FRAME_DATA_LENGTH > 2 * MAX_FRAMES_AHEAD );
	CHECK( FRAMES_TO_KEEP > MAX_FRAMES_AHEAD / 2 );
}

/* The room's logic rate.  ConnectionManager::updateRunAhead runs on the packet router: it takes
	 the slowest frame rate any player reported, settles it into the allowed range, and broadcasts
	 the result as the rate every machine paces its logic on.  The reported rate is the rate a
	 player *achieved*, so a player who is keeping up reports back exactly the rate they were told
	 to run at - which is why the broadcast rate has to be a step above the measured minimum or it
	 can never rise again.  simulateRoom below is that whole loop, with each player achieving the
	 smaller of their own capability and the rate they were commanded. */

static Int simulateRoom( const Int *capability, Int numPlayers, Int startRate, Int rounds,
												 Int fpsLimit )
{
	Int rate = startRate;
	for( Int round = 0; round < rounds; ++round )
	{
		Int minFps = -1;
		for( Int player = 0; player < numPlayers; ++player )
		{
			Int reported = capability[player] < rate ? capability[player] : rate;
			if( minFps == -1 || reported < minFps )
				minFps = reported;
		}
		rate = probeRoomFrameRate( settleRoomFrameRate( minFps, fpsLimit ), fpsLimit );
	}
	return rate;
}

TEST(room_frame_rate_settles_into_the_allowed_range)
{
	CHECK_EQ( settleRoomFrameRate( 30, 30 ), 30 );
	CHECK_EQ( settleRoomFrameRate( 17, 30 ), 17 );

	// nobody plays at two frames a second, whatever the metrics claim
	CHECK_EQ( settleRoomFrameRate( 2, 30 ), ROOM_FRAME_RATE_FLOOR );
	CHECK_EQ( settleRoomFrameRate( 0, 30 ), ROOM_FRAME_RATE_FLOOR );
	CHECK_EQ( settleRoomFrameRate( -1, 30 ), ROOM_FRAME_RATE_FLOOR );

	// and the room never runs faster than the game is configured to
	CHECK_EQ( settleRoomFrameRate( 100, 30 ), 30 );
	CHECK_EQ( settleRoomFrameRate( 100, 60 ), 60 );
	CHECK_EQ( settleRoomFrameRate( 45, 60 ), 45 );
}

TEST(room_frame_rate_probe_always_moves_by_at_least_one_frame)
{
	/* The step is what breaks the latch, so a step of zero is the bug.  Integer division eats it
		 below ten frames a second: (9 * 110) / 100 is 9. */
	for( Int settled = ROOM_FRAME_RATE_FLOOR; settled < 30; ++settled )
		CHECK( probeRoomFrameRate( settled, 30 ) > settled );

	CHECK_EQ( probeRoomFrameRate( 5, 30 ), 6 );
	CHECK_EQ( probeRoomFrameRate( 9, 30 ), 10 );
	CHECK_EQ( probeRoomFrameRate( 10, 30 ), 11 );
	CHECK_EQ( probeRoomFrameRate( 20, 30 ), 22 );

	// but never past the limit, and at the limit it is a no-op rather than an overshoot
	CHECK_EQ( probeRoomFrameRate( 30, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 29, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 28, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 55, 60 ), 60 );

	/* EA capped this step at a hardcoded 30 while the settled rate was capped at
		 FramesPerSecondLimit, so a room configured above 30 told its slowest player to slow down. */
	CHECK_EQ( probeRoomFrameRate( 40, 60 ), 44 );
}

TEST(room_frame_rate_climbs_back_after_one_player_hitches)
{
	/* The defect this pins: every machine paces its logic on the broadcast rate, so a machine that
		 is keeping up measures exactly that rate and reports it back.  With the room commanded at
		 the reported minimum, the minimum is then whatever it already was - for ever.  One player's
		 two second hitch dropped the room to 12 and the whole match stayed in slow motion. */
	const Int fpsLimit = 30;
	Int capable[4] = { 30, 30, 30, 30 };

	// the room is at 12 because somebody hitched; they have recovered, everyone can do 30 now
	Int rate = simulateRoom( capable, 4, 12, 1, fpsLimit );
	CHECK( rate > 12 );

	// and it keeps climbing, round after round, until it is back at the limit
	Int previous = 12;
	for( Int round = 1; round <= 20; ++round )
	{
		rate = simulateRoom( capable, 4, 12, round, fpsLimit );
		CHECK( rate >= previous );
		previous = rate;
	}
	CHECK_EQ( simulateRoom( capable, 4, 12, 20, fpsLimit ), 30 );

	// from the floor as well, and from a two player room, and with a raised limit
	CHECK_EQ( simulateRoom( capable, 4, ROOM_FRAME_RATE_FLOOR, 40, fpsLimit ), 30 );
	CHECK_EQ( simulateRoom( capable, 2, 6, 40, fpsLimit ), 30 );

	Int capable60[2] = { 60, 60 };
	CHECK_EQ( simulateRoom( capable60, 2, 10, 40, 60 ), 60 );
}

TEST(room_frame_rate_still_pins_to_a_genuinely_slow_player_without_oscillating)
{
	/* The step must not turn into a speed wobble: a player who really cannot do better than 12
		 has to hold the room near 12 and hold it *steady*.  EA's "keep the current rate if the
		 minimum is within 10 % of it" band is what would break this - it ignores the slow player
		 for exactly as long as the step keeps the rate within 10 % of them, so the room walks
		 12-13-14-15 and falls back to 12.  The band is gone; the step does its job alone. */
	const Int fpsLimit = 30;
	Int mixed[3] = { 30, 30, 12 };

	Int rate = simulateRoom( mixed, 3, 30, 30, fpsLimit );
	CHECK( rate >= 12 );
	CHECK( rate <= 14 );

	// settled means settled: over the last twenty rounds the rate must not move at all
	Int settledRate = simulateRoom( mixed, 3, 30, 10, fpsLimit );
	for( Int round = 10; round <= 30; ++round )
		CHECK_EQ( simulateRoom( mixed, 3, 30, round, fpsLimit ), settledRate );

	// the room does slow down for them, though - that part is the point of the mechanism
	CHECK( settledRate < 30 );

	// a machine below the floor cannot drag the room under it either
	Int hopeless[2] = { 30, 1 };
	CHECK_EQ( simulateRoom( hopeless, 2, 30, 30, fpsLimit ),
						probeRoomFrameRate( ROOM_FRAME_RATE_FLOOR, fpsLimit ) );
}

TEST(resend_window_is_measured_from_the_start_of_the_match_not_four_billion)
{
	/* ConnectionManager::sendSingleFrameToPlayer refuses to resend a frame older than
		 FRAMES_TO_KEEP, which is (MAX_FRAMES_AHEAD / 2) + 1 = 65 - about two seconds at 30 Hz, and
		 exactly the window a stalled player can be behind by, since nobody advances while the room
		 is waiting for them. */
	const Int KEEP = 65;

	// the frame we are on, and everything inside the window, is resendable
	CHECK( !frameIsTooOldToResend( 1000, 1000, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1000, 999, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1000, 1000 - KEEP, KEEP ) );

	// one frame past the window is not
	CHECK( frameIsTooOldToResend( 1000, 1000 - KEEP - 1, KEEP ) );
	CHECK( frameIsTooOldToResend( 1000, 0, KEEP ) );

	/* The defect: EA computed `(currentFrame - FRAMES_TO_KEEP) > requestedFrame` on UnsignedInts,
		 so for the first 65 frames of every match the left side wrapped to about four billion and
		 every request was refused.  A stall in the opening two seconds could not be repaired by the
		 resend at all - it had to wait for the retry and then the disconnect screen. */
	CHECK( !frameIsTooOldToResend( 0, 0, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1, 0, KEEP ) );
	CHECK( !frameIsTooOldToResend( 10, 3, KEEP ) );
	CHECK( !frameIsTooOldToResend( KEEP, 0, KEEP ) );
	CHECK( frameIsTooOldToResend( KEEP + 1, 0, KEEP ) );

	// a frame we have not reached is not old; we simply have nothing for it yet
	CHECK( !frameIsTooOldToResend( 100, 101, KEEP ) );
	CHECK( !frameIsTooOldToResend( 100, 1000000, KEEP ) );

	// and the frame counter itself wrapping does not reopen or close the window by accident
	CHECK( !frameIsTooOldToResend( 5, 0xFFFFFFFEu, KEEP ) );
	CHECK( frameIsTooOldToResend( 5, 0xFFFFFF00u, KEEP ) );
}

/* The packet router fallback plan: who relays for everybody, and in what order they take over.
	 Walked in two places, both of which used to be able to read one entry past the end of it. */
TEST(packet_router_succession_never_walks_off_the_end_of_the_plan)
{
	const Int SLOTS = 8;
	const UnsignedInt NONE = (UnsignedInt)SLOTS;		// the "plan is exhausted" answer
	const UnsignedInt EMPTY = (UnsignedInt)-1;			// what an emptied entry is left holding

	/* A full eight player game.  The last entry is the case EA's walk got wrong: the loop stopped at
		 MAX_SLOTS-1 whether it matched or not, the unconditional ++ then made the index MAX_SLOTS, and
		 the read landed on the member after the array. */
	const UnsignedInt full[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 0 ), 1u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 5 ), 6u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 6 ), 7u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 7 ), NONE );

	/* A four player game leaves the rest of the plan as -1.  Reading that as a slot number gives
		 four billion, which is the same wrong answer by a different route. */
	const UnsignedInt four[8] = { 0, 1, 2, 3, EMPTY, EMPTY, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 0 ), 1u );
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 2 ), 3u );
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 3 ), NONE );

	/* Every leave compacts the plan, so the entries are slot numbers with gaps and the tail fills
		 with -1 from the end. */
	const UnsignedInt compacted[8] = { 1, 2, 4, 5, 6, 7, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 1 ), 2u );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 2 ), 4u );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 7 ), NONE );

	/* Asking about a slot that is not in the plan at all - the state the hardcoded initial router
		 could produce when slot 0 held no human player. */
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 0 ), NONE );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 3 ), NONE );

	/* Last player standing. */
	const UnsignedInt alone[8] = { 3, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( alone, SLOTS, 3 ), NONE );

	/* The succession is a walk of the plan, never a re-election: repeated calls march down the plan
		 in order and stop, so two machines running it on the same plan cannot end up disagreeing
		 about who is relaying.  That property is why the plan is not reordered from local latency
		 measurements - see MULTIPLAYER.md 2.5. */
	UnsignedInt router = compacted[0];
	Int steps = 0;
	while( router < (UnsignedInt)SLOTS )
	{
		UnsignedInt next = nextPacketRouterSlot( compacted, SLOTS, router );
		CHECK( next == NONE || next > router );
		router = next;
		++steps;
		CHECK( steps <= SLOTS );
	}
	CHECK_EQ( steps, 6 );		// six live entries, then the plan is out

}

TEST(selfslug_is_monotonic_in_the_cushion)
{
	// once there is enough margin the answer must stay no, or the frame rate would oscillate
	Bool seenNo = FALSE;
	for( Int cushion = 0; cushion <= 200; ++cushion )
	{
		Bool slug = shouldSelfSlug( cushion, 60, 10 );
		if( !slug )
			seenNo = TRUE;
		else
			CHECK( !seenNo );
	}
	CHECK( seenNo );
}

// ---------------------------------------------------------------------------------------------
// TerrainLogic used to answer "which waypoint is called X" and "which trigger area is called X"
// by walking the whole list, on every call, and the scripts ask constantly.  Both are indexed now,
// and the index has to notice the list changing under it.
// ---------------------------------------------------------------------------------------------
class TestTerrainLogic : public TerrainLogic
{
public:
	void addTestWaypoint( UnsignedInt id, AsciiString name )
	{
		Coord3D loc;
		loc.x = (Real)id;
		loc.y = 0.0f;
		loc.z = 0.0f;
		Waypoint *way = newInstance(Waypoint)( (WaypointID)id, name, &loc,
																					 AsciiString::TheEmptyString,
																					 AsciiString::TheEmptyString,
																					 AsciiString::TheEmptyString, FALSE );
		linkWaypoint( way );
	}

	void dropAllWaypoints( void ) { deleteWaypoints(); }
};

// A missing waypoint reads as id 0 / name "<none>" rather than a null dereference, so a broken
// index reports itself instead of taking the whole run down.
static UnsignedInt waypointID( Waypoint *way ) { return way ? (UnsignedInt)way->getID() : 0; }
static AsciiString waypointName( Waypoint *way ) { return way ? way->getName() : AsciiString("<none>"); }

TEST(terrainlogic_finds_a_waypoint_by_name_and_by_id)
{
	TestTerrainLogic tl;

	tl.addTestWaypoint( 1, "Alpha" );
	tl.addTestWaypoint( 2, "Bravo" );
	tl.addTestWaypoint( 3, "Charlie" );

	CHECK( tl.getWaypointByName( "Bravo" ) != NULL );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Bravo" ) ), (UnsignedInt)2 );
	CHECK( tl.getWaypointByID( 3 ) != NULL );
	CHECK_STR( waypointName( tl.getWaypointByID( 3 ) ).str(), "Charlie" );
	CHECK( tl.getWaypointByName( "Nobody" ) == NULL );
	CHECK( tl.getWaypointByID( 99 ) == NULL );

	// the index was built by those lookups; a waypoint added afterwards still has to be found
	tl.addTestWaypoint( 4, "Delta" );
	CHECK( tl.getWaypointByName( "Delta" ) != NULL );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Delta" ) ), (UnsignedInt)4 );
	CHECK( tl.getWaypointByName( "Alpha" ) != NULL );

	// the list is built by pushing onto the head and the old search returned the first match from
	// the head, so the most recently added of two waypoints sharing a name is the one that answers
	tl.addTestWaypoint( 5, "Alpha" );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Alpha" ) ), (UnsignedInt)5 );

	tl.dropAllWaypoints();
	CHECK( tl.getWaypointByName( "Alpha" ) == NULL );
	CHECK( tl.getWaypointByID( 1 ) == NULL );
}

TEST(terrainlogic_finds_a_trigger_area_by_name_and_notices_edits)
{
	PolygonTrigger::deleteTriggers();

	TestTerrainLogic tl;

	PolygonTrigger *zone1 = newInstance(PolygonTrigger)( 4 );
	zone1->setTriggerName( "Zone1" );
	PolygonTrigger::addPolygonTrigger( zone1 );

	PolygonTrigger *zone2 = newInstance(PolygonTrigger)( 4 );
	zone2->setTriggerName( "Zone2" );
	PolygonTrigger::addPolygonTrigger( zone2 );

	CHECK( tl.getTriggerAreaByName( "Zone1" ) == zone1 );
	CHECK( tl.getTriggerAreaByName( "Zone2" ) == zone2 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == NULL );

	// added after the index was built
	PolygonTrigger *zone3 = newInstance(PolygonTrigger)( 4 );
	zone3->setTriggerName( "Zone3" );
	PolygonTrigger::addPolygonTrigger( zone3 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == zone3 );

	// renamed after the index was built
	zone3->setTriggerName( "Zone9" );
	CHECK( tl.getTriggerAreaByName( "Zone9" ) == zone3 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == NULL );

	// unlinked after the index was built
	PolygonTrigger::removePolygonTrigger( zone2 );
	CHECK( tl.getTriggerAreaByName( "Zone2" ) == NULL );
	CHECK( tl.getTriggerAreaByName( "Zone1" ) == zone1 );
	zone2->deleteInstance();

	PolygonTrigger::deleteTriggers();
	CHECK( tl.getTriggerAreaByName( "Zone1" ) == NULL );
}

// A restarted skirmish must draw the same random colors, start positions and factions as the first
// run did.  The draws only happen for a slot whose value is still negative, so the setup captured
// before the first draw has to be put back before the second start.
TEST(gameslot_restores_the_pre_randomization_setup_on_a_restart)
{
	GameSlot slot;

	// nothing captured yet, and the map's own "random" markers are what the slot starts with
	CHECK( !slot.hasSavedOriginalSetup() );
	CHECK_EQ( slot.getColor(), -1 );
	CHECK_EQ( slot.getStartPos(), -1 );
	CHECK_EQ( slot.getPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// first start: capture, then let the draws resolve every one of them
	slot.saveOriginalSetup();
	CHECK( slot.hasSavedOriginalSetup() );
	CHECK_EQ( slot.getOriginalColor(), -1 );
	CHECK_EQ( slot.getOriginalStartPos(), -1 );
	CHECK_EQ( slot.getOriginalPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	slot.setColor( 3 );
	slot.setStartPos( 5 );
	slot.setPlayerTemplate( 2 );
	CHECK_EQ( slot.getColor(), 3 );
	CHECK_EQ( slot.getStartPos(), 5 );
	CHECK_EQ( slot.getPlayerTemplate(), 2 );

	// second start: the capture is still there, so the slot is rolled back instead of re-captured
	CHECK( slot.hasSavedOriginalSetup() );
	slot.setColor( slot.getOriginalColor() );
	slot.setStartPos( slot.getOriginalStartPos() );
	slot.setPlayerTemplate( slot.getOriginalPlayerTemplate() );
	CHECK_EQ( slot.getColor(), -1 );
	CHECK_EQ( slot.getStartPos(), -1 );
	CHECK_EQ( slot.getPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// and the originals survive the rollback, so a third start rolls back to the same place
	CHECK_EQ( slot.getOriginalColor(), -1 );
	CHECK_EQ( slot.getOriginalStartPos(), -1 );
	CHECK_EQ( slot.getOriginalPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// a slot that was set up by hand is captured as-is, not as the random markers
	GameSlot fixed;
	fixed.setColor( 4 );
	fixed.setStartPos( 1 );
	fixed.setPlayerTemplate( 6 );
	fixed.saveOriginalSetup();
	CHECK_EQ( fixed.getOriginalColor(), 4 );
	CHECK_EQ( fixed.getOriginalStartPos(), 1 );
	CHECK_EQ( fixed.getOriginalPlayerTemplate(), 6 );

	// leaving the lobby clears the capture, so the next game captures its own setup
	fixed.reset();
	CHECK( !fixed.hasSavedOriginalSetup() );
	CHECK_EQ( fixed.getOriginalColor(), -1 );
}

// The game's fingerprint over a BitFlags used to be taken with sizeof(this) - the size of the
// pointer - so only the first four bytes of any flag set were in it.  ObjectStatusMaskType is 47
// bits wide, so everything from RIDER1 upwards could differ between two machines without the CRC
// ever noticing.
static UnsignedInt crcOfStatusMask( ObjectStatusMaskType mask )
{
	XferCRC xfer;
	xfer.open( "test" );
	mask.xfer( &xfer );
	xfer.close();
	return xfer.getCRC();
}

TEST(bitflags_crc_covers_every_word_of_the_flag_set)
{
	ObjectStatusMaskType empty;
	empty.clear();

	// a bit inside the first word has always been covered
	ObjectStatusMaskType low;
	low.clear();
	low.set( OBJECT_STATUS_DESTROYED );
	CHECK_NE( crcOfStatusMask( low ), crcOfStatusMask( empty ) );

	// ...and so is everything past it now
	CHECK( (Int)OBJECT_STATUS_IMMOBILE >= 32 );
	ObjectStatusMaskType high;
	high.clear();
	high.set( OBJECT_STATUS_IMMOBILE );
	CHECK_NE( crcOfStatusMask( high ), crcOfStatusMask( empty ) );

	ObjectStatusMaskType deployed;
	deployed.clear();
	deployed.set( OBJECT_STATUS_DEPLOYED );
	CHECK_NE( crcOfStatusMask( deployed ), crcOfStatusMask( empty ) );

	// two different high bits are two different fingerprints, not one
	CHECK_NE( crcOfStatusMask( high ), crcOfStatusMask( deployed ) );

	// and the same mask still fingerprints the same way twice
	CHECK_EQ( crcOfStatusMask( high ), crcOfStatusMask( high ) );

	// the whole set is hashed, not a prefix of it: clearing a high bit off a full mask changes it
	ObjectStatusMaskType full, fullMinusOne;
	full.clear();
	fullMinusOne.clear();
	for( Int i = 0; i < (Int)OBJECT_STATUS_COUNT; ++i )
	{
		full.set( i );
		if( i != (Int)OBJECT_STATUS_DEPLOYED )
			fullMinusOne.set( i );
	}
	CHECK_NE( crcOfStatusMask( full ), crcOfStatusMask( fullMinusOne ) );
}

// ---------------------------------------------------------------------------------------------
// Network command ids are sixteen bits and wrap.  NetCommandList insert-sorts by them, so the
// comparison has to survive the wrap or a player's own orders come out of order - and, because a
// broken comparison makes the sorted insert depend on arrival order, come out differently on two
// machines that received the same commands in different orders.
// ---------------------------------------------------------------------------------------------
TEST(network_command_ids_are_ordered_across_the_sixteen_bit_wrap)
{
	// away from the wrap, this is plain ordering
	CHECK( IsCommandIdNewer( 1, 0 ) );
	CHECK( !IsCommandIdNewer( 0, 1 ) );
	CHECK( !IsCommandIdNewer( 7, 7 ) );			// the same id is not newer than itself

	// across the wrap - the raw > comparison gets both of these backwards
	CHECK( IsCommandIdNewer( 0, 65535 ) );
	CHECK( !IsCommandIdNewer( 65535, 0 ) );
	CHECK( IsCommandIdNewer( 3, 65530 ) );
	CHECK( !IsCommandIdNewer( 65530, 3 ) );

	// half the id space ahead is still ahead; one past that is read as behind
	CHECK( IsCommandIdNewer( 0x7FFF, 0 ) );
	CHECK( !IsCommandIdNewer( 0x8000, 0 ) );

	// every consecutive pair across a run that crosses 65535 -> 0 is ordered, both ways round
	Bool allOrdered = TRUE;
	UnsignedShort id = 65500;
	for( Int i = 0; i < 100; ++i )
	{
		UnsignedShort next = (UnsignedShort)(id + 1);
		if( !IsCommandIdNewer( next, id ) || IsCommandIdNewer( id, next ) )
			allOrdered = FALSE;
		id = next;
	}
	CHECK( allOrdered );
	CHECK_EQ( (Int)id, 64 );					// the run really did cross the wrap
}

//////////////////////////////////////////////////////////////////////////////
// Control groups
//////////////////////////////////////////////////////////////////////////////

/* The ten control group keys produce squad numbers 0..NUM_HOTKEY_SQUADS-1.  The camera-jump
	 handler tested "1 through 10" instead, so the 0 key never centred on its group and the number
	 it did accept was one past the last squad.  All four handlers ask this one function now. */
TEST(hotkey_squad_index_covers_group_zero_and_stops_at_the_last_squad)
{
	CHECK_EQ( (Int)NUM_HOTKEY_SQUADS, 10 );

	CHECK( isValidHotkeySquadIndex( 0 ) );					// the "0" key, the one that was dropped
	CHECK( isValidHotkeySquadIndex( 1 ) );
	CHECK( isValidHotkeySquadIndex( NUM_HOTKEY_SQUADS - 1 ) );

	CHECK( !isValidHotkeySquadIndex( NUM_HOTKEY_SQUADS ) );	// the one it used to accept
	CHECK( !isValidHotkeySquadIndex( -1 ) );

	// every squad the keys can name is a squad Player::getHotkeySquad will actually look up
	Bool allInRange = TRUE;
	for( Int group = 0; group < 10; ++group )
		if( !isValidHotkeySquadIndex( group ) )
			allInRange = FALSE;
	CHECK( allInRange );
}

//////////////////////////////////////////////////////////////////////////////
// Network command list ordering
//////////////////////////////////////////////////////////////////////////////

/* Helper: an ack for command "commandID" from player "playerID".  An ack's own id is always zero;
	 the number it sorts by is the id of the command it acknowledges. */
static NetAckStage1CommandMsg *makeAck( UnsignedShort commandID, UnsignedByte playerID )
{
	NetAckStage1CommandMsg *ack = newInstance( NetAckStage1CommandMsg );
	ack->setCommandID( commandID );
	ack->setOriginalPlayerID( playerID );
	ack->setPlayerID( playerID );
	ack->setID( 0 );
	return ack;
}

/* The command list sorts by type, then player, then sort number - and for an ack the sort number
	 is the acknowledged command's id, not the ack's own id, which is always zero.  The insertion
	 shortcut compared ids instead, so IsCommandIdNewer(0, 0) was false for every ack and the
	 shortcut was dead for the one message that arrives in bulk. */
TEST(netcommand_ordering_uses_the_sort_number_not_the_always_zero_ack_id)
{
	NetAckStage1CommandMsg *older = makeAck( 3, 2 );
	NetAckStage1CommandMsg *newer = makeAck( 7, 2 );

	// what the old comparison looked at: identical, and so never "newer"
	CHECK_EQ( (Int)older->getID(), 0 );
	CHECK_EQ( (Int)newer->getID(), 0 );
	CHECK( !IsCommandIdNewer( newer->getID(), older->getID() ) );

	// what the list actually orders by
	CHECK_EQ( older->getSortNumber(), 3 );
	CHECK_EQ( newer->getSortNumber(), 7 );

	CHECK( IsCommandFromSamePlayerGroup( older, newer ) );
	CHECK( IsCommandNewerInSamePlayerGroup( newer, older ) );
	CHECK( !IsCommandNewerInSamePlayerGroup( older, newer ) );

	// a different player is a different group, however the numbers compare
	NetAckStage1CommandMsg *otherPlayer = makeAck( 7, 5 );
	CHECK( !IsCommandFromSamePlayerGroup( newer, otherPlayer ) );
	CHECK( !IsCommandNewerInSamePlayerGroup( otherPlayer, older ) );

	// the full ordering is lexicographic: type, then player, then sort number
	CHECK( IsCommandNewer( otherPlayer, newer ) );			// same type, higher player
	CHECK( !IsCommandNewer( newer, otherPlayer ) );
	CHECK( IsCommandNewer( newer, older ) );				// same type and player, higher sort number

	NetAckStage2CommandMsg *laterType = newInstance( NetAckStage2CommandMsg );
	laterType->setCommandID( 0 );
	laterType->setPlayerID( 2 );
	CHECK( IsCommandNewer( laterType, newer ) );				// higher type beats a higher sort number
	CHECK( !IsCommandNewer( newer, laterType ) );

	older->detach();
	newer->detach();
	otherPlayer->detach();
	laterType->detach();
}

/* Acks sort to the head of the list, so the shortcut is what keeps a burst of them linear.  Order
	 has to come out the same whether it is taken or not: in order, out of order, and duplicated. */
TEST(netcommand_list_orders_a_burst_of_acks_by_acknowledged_command)
{
	NetCommandList *list = newInstance( NetCommandList );
	list->init();

	// in ascending order - the path the shortcut is there for
	static const UnsignedShort inOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	Int i;
	for( i = 0; i < 8; ++i )
	{
		NetAckStage1CommandMsg *ack = makeAck( inOrder[i], 1 );
		list->addMessage( ack );
		ack->detach();
	}

	// out of order, and one exact duplicate of a command already on the list
	static const UnsignedShort outOfOrder[] = { 12, 9, 11, 10, 5 };
	for( i = 0; i < 5; ++i )
	{
		NetAckStage1CommandMsg *ack = makeAck( outOfOrder[i], 1 );
		list->addMessage( ack );
		ack->detach();
	}

	CHECK_EQ( list->length(), 12 );			// the duplicate 5 was dropped, 1..12 remain

	Int expected = 1;
	Bool ascending = TRUE;
	for( NetCommandRef *ref = list->getFirstMessage(); ref != NULL; ref = ref->getNext() )
	{
		if( ref->getCommand()->getSortNumber() != expected )
			ascending = FALSE;
		++expected;
	}
	CHECK( ascending );
	CHECK_EQ( expected, 13 );

	list->deleteInstance();
}

//////////////////////////////////////////////////////////////////////////////
// Starting a new game
//////////////////////////////////////////////////////////////////////////////

/* MSG_NEW_GAME comes down the message stream like any other command, and the dispatcher used to
	 act on it whatever the game was doing.  Handed one mid-match, a machine tears down the game the
	 others are still playing and they wait in the disconnect screen for frames that never come. */
TEST(a_new_game_only_starts_from_a_standing_start)
{
	// nothing running: the only state a new game may begin from
	CHECK( IsReadyToStartNewGame( FALSE, FALSE, FALSE ) );

	// each of the three on its own is enough to refuse
	CHECK( !IsReadyToStartNewGame( TRUE,  FALSE, FALSE ) );		// a match is already running
	CHECK( !IsReadyToStartNewGame( FALSE, TRUE,  FALSE ) );		// the last one is still being torn down
	CHECK( !IsReadyToStartNewGame( FALSE, FALSE, TRUE  ) );		// a map is part way through loading

	// and no combination of them lets one through
	Bool refusedEveryBusyState = TRUE;
	for( Int bits = 1; bits < 8; ++bits )
		if( IsReadyToStartNewGame( (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0 ) )
			refusedEveryBusyState = FALSE;
	CHECK( refusedEveryBusyState );
}

/* A network command names the object it acts on by id, and nothing in the message ties that id to
	 the machine that sent it.  The special power cases ran whatever object they were handed, so a
	 doctored command could fire another player's superweapon.  They ask this first now.  The
	 predicate compares identity only and never dereferences either pointer, so the test can use
	 stand-in addresses for players. */
TEST(a_command_may_only_name_an_object_its_sender_controls)
{
	const Player *alice = (const Player *)0x100;
	const Player *bob   = (const Player *)0x200;

	CHECK( isPlayerCommandingOwnObject( alice, alice ) );	// your own unit
	CHECK( !isPlayerCommandingOwnObject( alice, bob ) );	// somebody else's
	CHECK( !isPlayerCommandingOwnObject( bob, alice ) );

	// an object with no controller is nobody's to command, and neither is anything at all when the
	// message names a player slot that is not in the game
	CHECK( !isPlayerCommandingOwnObject( alice, NULL ) );
	CHECK( !isPlayerCommandingOwnObject( NULL, alice ) );
	CHECK( !isPlayerCommandingOwnObject( NULL, NULL ) );
}

/* A player index that arrives from outside this machine is only a number: the replay reader freads
	 it over a -1 and never checks the read, and a network command carries whatever the sending
	 machine put in it.  getNthPlayer answers NULL for anything outside the list, and the logic
	 dispatcher used to assert - i.e. do nothing in a release build - and dereference it anyway. */
TEST(a_player_index_from_outside_this_machine_is_bounded_before_use)
{
	// the whole list, and nothing either side of it
	CHECK( isValidPlayerIndex( 0 ) );
	CHECK( isValidPlayerIndex( MAX_PLAYER_COUNT - 1 ) );
	CHECK( !isValidPlayerIndex( MAX_PLAYER_COUNT ) );
	CHECK( !isValidPlayerIndex( -1 ) );		// what a failed fread of a replay leaves behind

	// a truncated or hostile value off the wire
	CHECK( !isValidPlayerIndex( 0x7fffffff ) );
	CHECK( !isValidPlayerIndex( (Int)0x80000000 ) );
	CHECK( !isValidPlayerIndex( -12345 ) );

	Bool acceptedOnlyTheList = TRUE;
	for( Int i = -64; i < 64; ++i )
		if( isValidPlayerIndex( i ) != (i >= 0 && i < MAX_PLAYER_COUNT) )
			acceptedOnlyTheList = FALSE;
	CHECK( acceptedOnlyTheList );
}

/* Starting a game used to seed only the logic stream.  The client and audio streams kept the
	 time_t the process was seeded with at startup, so they read differently on every machine in the
	 room and differently again on every playback of the same replay - and any draw that leaks out of
	 the client stream into something the simulation can see is then a divergence.  A game's seed now
	 sets all three, and the logic stream still lands exactly where it did. */
TEST(a_games_seed_sets_the_client_and_audio_streams_as_well_as_the_logic_one)
{
	const Int DRAWS = 16;
	Int client[DRAWS], audio[DRAWS], logic[DRAWS];

	InitRandom( 31337 );
	const UnsignedInt logicSeedAfterInit = GetGameLogicRandomSeedCRC();
	for( Int i = 0; i < DRAWS; ++i )
	{
		client[i] = GameClientRandomValue( 0, 1000000 );
		audio[i]  = GameAudioRandomValue( 0, 1000000 );
		logic[i]  = GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ );
	}

	// the same seed on the machine next to you, or on the same machine tomorrow, reads the same
	InitRandom( 31337 );
	CHECK_EQ( GetGameLogicRandomSeedCRC(), logicSeedAfterInit );
	Bool clientRepeated = TRUE, audioRepeated = TRUE, logicRepeated = TRUE;
	for( Int i = 0; i < DRAWS; ++i )
	{
		if( GameClientRandomValue( 0, 1000000 ) != client[i] ) clientRepeated = FALSE;
		if( GameAudioRandomValue( 0, 1000000 )  != audio[i]  ) audioRepeated  = FALSE;
		if( GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ ) != logic[i] ) logicRepeated = FALSE;
	}
	CHECK( clientRepeated );
	CHECK( audioRepeated );
	CHECK( logicRepeated );

	// and a different game is a different game, in all three
	InitRandom( 31338 );
	CHECK_NE( GetGameLogicRandomSeedCRC(), logicSeedAfterInit );
	Bool clientMoved = FALSE, audioMoved = FALSE, logicMoved = FALSE;
	for( Int i = 0; i < DRAWS; ++i )
	{
		if( GameClientRandomValue( 0, 1000000 ) != client[i] ) clientMoved = TRUE;
		if( GameAudioRandomValue( 0, 1000000 )  != audio[i]  ) audioMoved  = TRUE;
		if( GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ ) != logic[i] ) logicMoved = TRUE;
	}
	CHECK( clientMoved );
	CHECK( audioMoved );
	CHECK( logicMoved );
}

/* A map's INI overrides an object template by hanging a copy off the end of the original's chain,
	 under the same name.  findTemplate hands back the head of that chain; Object's constructor walks
	 to the end.  WeaponSet::xfer wrote a live object's template name into the save and looked it up
	 again on load without walking, so a unit on an overridden map came back holding the weapon set of
	 the template the override replaced.  Both sites go through finalOverrideOf now.

	 The chain lives entirely in Overridable, and finalOverrideOf touches nothing else, so the test
	 builds it out of plain Overridables: a ThingTemplate cannot be constructed here, its constructor
	 reads TheGlobalData.  ThingTemplate's only base is Overridable, non-virtually, so the cast is an
	 identity on the pointer. */
TEST(a_template_looked_up_by_name_is_taken_to_the_end_of_its_override_chain)
{
	Overridable *base     = newInstance( Overridable );
	Overridable *override = newInstance( Overridable );
	Overridable *later    = newInstance( Overridable );

	#define AS_TEMPLATE(p) ((const ThingTemplate *)(const Overridable *)(p))

	// nothing overrides it yet, so the answer is the template itself
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(base) );

	// one override, then a second hung off the first - which is how newOverride builds them
	base->setNextOverride( override );
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(override) );

	override->setNextOverride( later );
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(later) );
	CHECK( finalOverrideOf( AS_TEMPLATE(override) ) == AS_TEMPLATE(later) );
	CHECK( finalOverrideOf( AS_TEMPLATE(later) ) == AS_TEMPLATE(later) );

	// and a name that matched nothing stays nothing rather than being dereferenced
	CHECK( finalOverrideOf( NULL ) == NULL );

	#undef AS_TEMPLATE

	// one delete, not three: ~Overridable deletes whatever the chain still points at
	base->deleteInstance();
}

/* The simulation math fingerprint exists so that two players comparing mismatch dumps can tell
	 "our arithmetic differs" from "our game states diverged".  That only works if the number is a
	 function of the machine's math alone - in particular, it must not depend on whatever FPU mode
	 the caller happened to be in, and it must not leave that mode changed behind it. */
TEST(the_simulation_math_fingerprint_is_the_machines_math_and_not_the_callers_fpu_mode)
{
	/* Three callers, three FPU modes, none of them the one the simulation runs in unless it happens
		 to be: 53-bit precision is what the C runtime starts a process in and what a driver that
		 resets the FPU leaves behind, and round-to-chop is a mode the game never wants but nothing
		 stops a plugin from leaving set.  All three must produce the same fingerprint. */

	setFPMode();
	const UnsignedInt fromSimulationMode = SimulationMathCrc::calculate();

	// a real value, not a CRC of nothing at all, and repeatable
	CHECK( fromSimulationMode != 0 );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );

	_controlfp( _PC_53, _MCW_PC );
	const UnsignedInt modeIn53 = getFPMode();
	CHECK( modeIn53 != expectedFPMode() );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );
	CHECK_EQ( getFPMode(), modeIn53 );	// and the caller's mode is still the caller's

	_controlfp( _PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC );
	const UnsignedInt modeInChop = getFPMode();
	CHECK( modeInChop != expectedFPMode() );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );
	CHECK_EQ( getFPMode(), modeInChop );

	// and back where the rest of the tests expect to find it
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );
}

// ------------------------------------------------------------------------------------------------
// The radar's shroud layer.  It used to be poked one pixel at a time straight into a Direct3D
// texture, with a lock and an unlock around each pixel; it is a main-memory buffer now, and the
// texture hears about it once a frame, over the rectangle that changed.

TEST(radar_shroud_cache_starts_owing_the_whole_texture_a_write)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );

	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 0 );
	CHECK_EQ( cache.getDirtyMinY(), 0 );
	CHECK_EQ( cache.getDirtyMaxX(), 127 );
	CHECK_EQ( cache.getDirtyMaxY(), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 0, 0 ), 0 );
	CHECK_EQ( (Int)cache.getAlpha( 127, 127 ), 0 );
}

TEST(radar_shroud_cache_grows_its_dirty_rectangle_around_what_changed)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );

	// pretend the frame it was created in has been drawn
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );
	CHECK( !cache.isDirty() );

	cache.setAlpha( 10, 20, 255 );
	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 10 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 20 );

	// a second, far away pixel: the rectangle is the union, not the last one written
	cache.setAlpha( 3, 90, 127 );
	CHECK_EQ( cache.getDirtyMinX(), 3 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 90 );

	// and one inside it changes nothing about the bounds
	cache.setAlpha( 5, 50, 1 );
	CHECK_EQ( cache.getDirtyMinX(), 3 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 90 );

	CHECK_EQ( (Int)cache.getAlpha( 10, 20 ), 255 );
	CHECK_EQ( (Int)cache.getAlpha( 3, 90 ), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 5, 50 ), 1 );
}

TEST(radar_shroud_cache_ignores_writes_that_change_nothing)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );

	// the shroud is written cell by cell every time a unit moves, and most of those writes say
	// what the pixel already said
	cache.setAlpha( 40, 40, 0 );
	CHECK( !cache.isDirty() );

	cache.setAlpha( 40, 40, 255 );
	CHECK( cache.isDirty() );
	cache.flushTo( surface, 128 * 4, 4 );
	cache.setAlpha( 40, 40, 255 );
	CHECK( !cache.isDirty() );
}

TEST(radar_shroud_cache_drops_points_off_the_texture)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );

	// the caller walks a rectangle of map cells and some of them fall outside the radar
	cache.setAlpha( -1, 40, 255 );
	cache.setAlpha( 40, -1, 255 );
	cache.setAlpha( 128, 40, 255 );
	cache.setAlpha( 40, 128, 255 );
	cache.setAlpha( 10000, 10000, 255 );
	CHECK( !cache.isDirty() );
	CHECK_EQ( (Int)cache.getAlpha( -1, 40 ), 0 );
	CHECK_EQ( (Int)cache.getAlpha( 128, 128 ), 0 );

	// the last legal pixel is legal
	cache.setAlpha( 127, 127, 255 );
	CHECK( cache.isDirty() );
	CHECK_EQ( (Int)cache.getAlpha( 127, 127 ), 255 );
}

TEST(radar_shroud_cache_writes_only_the_dirty_rectangle_into_the_surface)
{
	enum { W = 16, H = 8, PITCH = 128 };			// a pitch wider than the rows, like a real lock
	RadarShroudCache cache;
	cache.setSize( W, H );

	UnsignedByte surface[ PITCH * H ];
	memset( surface, 0xCD, sizeof( surface ) );
	cache.flushTo( surface, PITCH, 4 );				// the opening flush covers everything
	CHECK( !cache.isDirty() );

	// past the end of each row nothing was touched
	for( Int y = 0; y < H; y++ )
		for( Int x = W * 4; x < PITCH; x++ )
			CHECK_EQ( (Int)surface[ y * PITCH + x ], 0xCD );

	memset( surface, 0xCD, sizeof( surface ) );
	cache.setAlpha( 2, 3, 255 );
	cache.setAlpha( 4, 5, 127 );
	cache.flushTo( surface, PITCH, 4 );

	// rows 3..5, columns 2..4, and not one byte more
	Int written = 0;
	for( Int y = 0; y < H; y++ )
	{
		for( Int x = 0; x < W; x++ )
		{
			UnsignedInt pixel;
			memcpy( &pixel, surface + y * PITCH + x * 4, sizeof( pixel ) );
			const Bool inside = ( y >= 3 && y <= 5 && x >= 2 && x <= 4 );
			if( inside )
			{
				++written;
				CHECK_EQ( pixel, ((UnsignedInt)cache.getAlpha( x, y )) << 24 );
			}
			else
			{
				CHECK_EQ( pixel, 0xCDCDCDCD );
			}
		}
	}
	CHECK_EQ( written, 9 );
	CHECK( !cache.isDirty() );

	// and a flush with nothing to say does not touch the surface at all
	memset( surface, 0xCD, sizeof( surface ) );
	cache.flushTo( surface, PITCH, 4 );
	for( Int i = 0; i < (Int)sizeof( surface ); i++ )
		CHECK_EQ( (Int)surface[ i ], 0xCD );
}

TEST(radar_shroud_cache_puts_the_alpha_where_the_pixel_format_wants_it)
{
	enum { W = 4, H = 2 };
	RadarShroudCache cache;
	cache.setSize( W, H );
	cache.setAlpha( 1, 1, 255 );
	cache.setAlpha( 2, 1, 127 );

	// eight bits of alpha, in the top byte, black underneath: GameMakeColor( 0, 0, 0, alpha )
	UnsignedInt wide[ W * H ];
	memset( wide, 0, sizeof( wide ) );
	cache.flushTo( wide, W * 4, 4 );
	CHECK_EQ( wide[ 1 * W + 1 ], 0xFF000000 );
	CHECK_EQ( wide[ 1 * W + 2 ], 0x7F000000 );
	CHECK_EQ( wide[ 0 * W + 0 ], 0u );

	// four bits of it, in the top nibble.  The DrawPixel this replaces masked the colour with
	// 0xFFFF here and threw the whole alpha away.
	cache.clear( 0 );
	cache.setAlpha( 1, 1, 255 );
	cache.setAlpha( 2, 1, 127 );
	UnsignedShort narrow[ W * H ];
	memset( narrow, 0, sizeof( narrow ) );
	cache.flushTo( narrow, W * 2, 2 );
	CHECK_EQ( (Int)narrow[ 1 * W + 1 ], 0xF000 );
	CHECK_EQ( (Int)narrow[ 1 * W + 2 ], 0x7000 );
	CHECK_EQ( (Int)narrow[ 0 * W + 0 ], 0 );
}

TEST(radar_shroud_cache_clear_owes_the_whole_texture_a_write_again)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );
	cache.setAlpha( 60, 60, 255 );
	cache.flushTo( surface, 128 * 4, 4 );

	cache.clear( 0 );
	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 0 );
	CHECK_EQ( cache.getDirtyMinY(), 0 );
	CHECK_EQ( cache.getDirtyMaxX(), 127 );
	CHECK_EQ( cache.getDirtyMaxY(), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 60, 60 ), 0 );
}

// ---------------------------------------------------------------------------------------------
// A listbox scroll offset is a pixel offset into the running height of every entry above it.
// Both of the fields that carry it used to be Short while the running total they are compared
// with, and assigned from, is an Int.
// ---------------------------------------------------------------------------------------------
TEST(listbox_scroll_offset_survives_a_list_taller_than_a_signed_short)
{
	ListboxData list;
	memset( &list, 0, sizeof( list ) );

	// four thousand chat lines or replay files at fifteen pixels a row
	list.totalHeight = 60000;
	list.displayHeight = 200;
	list.displayPos = 45000;

	CHECK_EQ( list.displayPos, 45000 );
	CHECK( list.displayPos > 0 );
	CHECK( list.displayPos + list.displayHeight <= list.totalHeight );

	// scrolling to the bottom, the way the slider and the mouse wheel do it
	list.displayPos = list.totalHeight - list.displayHeight;
	CHECK_EQ( list.displayPos, 59800 );
	CHECK( list.displayPos > 0 );
}

// ------------------------------------------------------------------------------------------------
// The trigonometry the simulation runs on.
//
// IEEE 754 pins +, -, *, / and sqrt: every machine rounds those identically, so the simulation was
// always safe there.  It says nothing at all about sin, cos, atan2, asin, acos or pow, and the
// implementations duly differ - ucrtbase dispatches on the host CPU and ships with Windows rather
// than with the game, and x87's FSIN and FCOS are microcoded differently by Intel and by AMD.  In a
// lockstep simulation one bit of disagreement about a unit's facing is two different games a second
// later.  Those calls now go through WWMath's DetTrig, which is an integer table.
// ------------------------------------------------------------------------------------------------

/* The trig fingerprint is the other half of the mismatch dump.  The first number in that dump is
	 the machine's C runtime and is allowed to differ between two players; this one is not, because
	 it is what the simulation actually computes with.  If it ever differs across two machines the
	 desync is in the arithmetic and nowhere else. */
TEST(simulation_trig_fingerprint_is_pinned)
{
	setFPMode();

	const UnsignedInt fingerprint = SimulationMathCrc::calculateSimulationTrig();

	/* Pinned to a literal on purpose: a table regenerated from Tools/gentrigtables.py, a changed
		 scale in dettrig.cpp, or a compiler that starts contracting the interpolation differently all
		 move every angle in the simulation, and this is where that gets noticed rather than in
		 somebody's replay.  Update it deliberately, never to make the build green. */
	const UnsignedInt expected = 0xEACAF02Bu;
	if (fingerprint != expected)
		printf("    simulation trig fingerprint is 0x%8.8X, expected 0x%8.8X\n", fingerprint, expected);
	CHECK_EQ(fingerprint, expected);

	// and, like the runtime one, it must not depend on the FPU mode the caller was in
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);
	_controlfp(_PC_53, _MCW_PC);
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);
	_controlfp(_PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC);
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);

	setFPMode();
	CHECK_EQ(getFPMode(), expectedFPMode());
}

/* Strip C and C++ comments and the contents of string and character literals, so the scanner below
	 reads code and nothing else.  EA's own comments talk about sin() and acos() in several places. */
static void stripCommentsAndLiterals(char *text, size_t length)
{
	size_t i = 0;
	while (i < length)
	{
		if (text[i] == '/' && i + 1 < length && text[i + 1] == '/')
		{
			while (i < length && text[i] != '\n')
				text[i++] = ' ';
		}
		else if (text[i] == '/' && i + 1 < length && text[i + 1] == '*')
		{
			text[i++] = ' ';
			text[i++] = ' ';
			while (i < length && !(text[i] == '*' && i + 1 < length && text[i + 1] == '/'))
				text[i] = (text[i] == '\n') ? '\n' : ' ', ++i;
			if (i < length) text[i++] = ' ';
			if (i < length) text[i++] = ' ';
		}
		else if (text[i] == '"' || text[i] == '\'')
		{
			const char quote = text[i];
			text[i++] = ' ';
			while (i < length && text[i] != quote)
			{
				if (text[i] == '\\' && i + 1 < length)
					text[i++] = ' ';
				if (i < length)
					text[i++] = ' ';
			}
			if (i < length) text[i++] = ' ';
		}
		else
		{
			++i;
		}
	}
}

static bool isIdentChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* The C runtime names the simulation may not call.  Everything here is implementation-defined;
	 sqrt, fabs, floor, ceil and fmod are absent because IEEE pins them and they are fine.

	 Bare "log" is absent too, and that is a compromise: <math.h>'s log collides with the engine's
	 own logging methods, so scanning for it is all false positives.  logf and log10 cover the form
	 anything doing real math would actually write. */
static const char *const theForbiddenMathNames[] = {
	"sin", "sinf", "cos", "cosf", "tan", "tanf",
	"asin", "asinf", "acos", "acosf", "atan", "atanf", "atan2", "atan2f",
	"pow", "powf", "exp", "expf", "logf", "log10", "log10f",
	"sinh", "sinhf", "cosh", "coshf", "tanh", "tanhf",
	NULL
};

static int scanSourceForRuntimeMath(const char *path, const char *displayName)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *text = (char *)malloc((size_t)size + 1);
	size_t got = fread(text, 1, (size_t)size, fp);
	fclose(fp);
	text[got] = 0;

	stripCommentsAndLiterals(text, got);

	int hits = 0;
	for (int n = 0; theForbiddenMathNames[n] != NULL; ++n)
	{
		const char *name = theForbiddenMathNames[n];
		const size_t len = strlen(name);
		const char *at = text;
		while ((at = strstr(at, name)) != NULL)
		{
			const char *after = at + len;
			const char *before = (at == text) ? NULL : at - 1;

			// a whole identifier, called: nothing glued to either end, an open paren after it
			bool wholeWord = (before == NULL || !isIdentChar(*before)) && !isIdentChar(*after);
			const char *p = after;
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
			bool called = (*p == '(');

			/* Not a member: "->log(", ".log(" and "::log(" are the engine's own, and a declaration
				 like "void log(" is one too - only a call has a value in front of it. */
			const char *q = before;
			while (q != NULL && q > text && (*q == ' ' || *q == '\t')) --q;
			bool member = (q != NULL && (*q == '.' || *q == '>' || *q == ':'));

			if (wholeWord && called && !member)
			{
				if (hits < 4)
				{
					int line = 1;
					for (const char *c = text; c < at; ++c)
						if (*c == '\n') ++line;
					printf("    %s:%d calls %s()\n", displayName, line, name);
				}
				++hits;
			}
			at = after;
		}
	}

	free(text);
	return hits;
}

static int scanTreeForRuntimeMath(const char *dir, const char *display, int *filesScanned)
{
	char pattern[MAX_PATH];
	sprintf(pattern, "%s\\*", dir);

	WIN32_FIND_DATAA find;
	HANDLE h = FindFirstFileA(pattern, &find);
	if (h == INVALID_HANDLE_VALUE)
		return 0;

	int hits = 0;
	do
	{
		if (find.cFileName[0] == '.')
			continue;

		char child[MAX_PATH];
		char childDisplay[MAX_PATH];
		sprintf(child, "%s\\%s", dir, find.cFileName);
		sprintf(childDisplay, "%s/%s", display, find.cFileName);

		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			hits += scanTreeForRuntimeMath(child, childDisplay, filesScanned);
			continue;
		}

		const char *dot = strrchr(find.cFileName, '.');
		if (dot == NULL || (strcmp(dot, ".cpp") != 0 && strcmp(dot, ".h") != 0))
			continue;

		/* SimulationMathCrc.cpp is the deliberate exception: its whole job is to fingerprint the
			 machine's own runtime math, so it has to call it.  MiniLog defines a method named log. */
		if (strcmp(find.cFileName, "SimulationMathCrc.cpp") == 0
			|| strcmp(find.cFileName, "MiniLog.cpp") == 0
			|| strcmp(find.cFileName, "MiniLog.h") == 0)
			continue;

		++(*filesScanned);
		hits += scanSourceForRuntimeMath(child, childDisplay);
	}
	while (FindNextFileA(h, &find));

	FindClose(h);
	return hits;
}

/* Promised by Libraries/Include/Lib/Trig.h, and the only thing that keeps the conversion from
	 rotting: nothing stops the next person from typing sinf(). */
TEST(simulation_uses_no_runtime_trig)
{
	static const char *const roots[] = {
		"Source\\GameLogic", "Source\\Common", "Include\\GameLogic", "Include\\Common", NULL
	};

	int filesScanned = 0;
	int hits = 0;
	for (int i = 0; roots[i] != NULL; ++i)
	{
		char dir[MAX_PATH];
		sprintf(dir, "%s\\%s", GAMEENGINE_SOURCE_DIR, roots[i]);
		hits += scanTreeForRuntimeMath(dir, roots[i], &filesScanned);
	}

	// a scanner that found nothing because it looked nowhere would pass silently
	CHECK(filesScanned > 500);
	if (hits != 0)
		printf("    %d runtime math call(s) in the simulation; use Lib/Trig.h\n", hits);
	CHECK_EQ(hits, 0);
}

/* The build placement preview draws the door of the structure it is carrying before that structure
	 exists, so it cannot go through ExitInterface - it asks the production exit module's *data* for
	 the two points instead.  A module that produces nothing must keep answering no, or every wall
	 segment would sprout a rally line out of (0,0,0). */
TEST(production_exit_points_come_off_the_module_data_before_there_is_an_object)
{
	Coord3D create, natural;

	DefaultProductionExitUpdateModuleData def;
	def.m_unitCreatePoint.x = 10.0f;   def.m_unitCreatePoint.y = -3.0f;  def.m_unitCreatePoint.z = 1.0f;
	def.m_naturalRallyPoint.x = 40.0f; def.m_naturalRallyPoint.y = 5.0f; def.m_naturalRallyPoint.z = 0.0f;

	CHECK( def.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, 10.0f, 0.0001f );
	CHECK_NEAR( create.y, -3.0f, 0.0001f );
	CHECK_NEAR( create.z, 1.0f, 0.0001f );
	CHECK_NEAR( natural.x, 40.0f, 0.0001f );
	CHECK_NEAR( natural.y, 5.0f, 0.0001f );

	// the war factory / barracks style queue module and the supply center answer the same way
	QueueProductionExitUpdateModuleData queue;
	queue.m_unitCreatePoint.x = 7.0f;
	queue.m_naturalRallyPoint.x = 70.0f;
	CHECK( queue.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, 7.0f, 0.0001f );
	CHECK_NEAR( natural.x, 70.0f, 0.0001f );

	SupplyCenterProductionExitUpdateModuleData supply;
	supply.m_unitCreatePoint.x = -8.0f;
	supply.m_naturalRallyPoint.x = -80.0f;
	CHECK( supply.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, -8.0f, 0.0001f );
	CHECK_NEAR( natural.x, -80.0f, 0.0001f );

	// and anything that is not a production exit module says no and touches nothing
	UpdateModuleData notAnExit;
	create.x = 1234.0f;
	natural.x = 5678.0f;
	CHECK( notAnExit.getProductionExitPointsInModelSpace( create, natural ) == FALSE );
	CHECK_NEAR( create.x, 1234.0f, 0.0001f );
	CHECK_NEAR( natural.x, 5678.0f, 0.0001f );
}

/* Those points are in model space, and the preview line is only useful if it swings around with
	 the building as the player wheels it.  This is the transform the renderer applies: a structure
	 dropped at (100,200) facing a quarter turn left puts a door that is 10 units "ahead" of the
	 model origin 10 units to the north of the building, not 10 units east of it. */
TEST(a_placed_structures_exit_point_turns_with_the_structure)
{
	Coord3D create, natural;

	DefaultProductionExitUpdateModuleData def;
	def.m_unitCreatePoint.x = 10.0f;   def.m_unitCreatePoint.y = 0.0f;   def.m_unitCreatePoint.z = 0.0f;
	def.m_naturalRallyPoint.x = 30.0f; def.m_naturalRallyPoint.y = 0.0f; def.m_naturalRallyPoint.z = 0.0f;
	CHECK( def.getProductionExitPointsInModelSpace( create, natural ) );

	Matrix3D transform;
	transform.Make_Identity();
	transform.Rotate_Z( PI / 2.0f );
	transform.Set_Translation( Vector3( 100.0f, 200.0f, 5.0f ) );

	Vector3 exitLoc( create.x, create.y, create.z );
	transform.Transform_Vector( transform, exitLoc, &exitLoc );
	CHECK_NEAR( exitLoc.X, 100.0f, 0.001f );
	CHECK_NEAR( exitLoc.Y, 210.0f, 0.001f );
	CHECK_NEAR( exitLoc.Z, 5.0f, 0.001f );

	Vector3 rallyLoc( natural.x, natural.y, natural.z );
	transform.Transform_Vector( transform, rallyLoc, &rallyLoc );
	CHECK_NEAR( rallyLoc.X, 100.0f, 0.001f );
	CHECK_NEAR( rallyLoc.Y, 230.0f, 0.001f );

	// the two points are distinct, which is what tells the renderer to draw a line and not just a puck
	CHECK( !(exitLoc == rallyLoc) );
}

/** SnapCameraRotateTo45 quantizes the camera heading instead of easing to it, so the arithmetic is
	 the whole feature: the heading is always the nearest eighth, and a rotate key moves it exactly one
	 eighth from wherever it stands.  Both signs matter - the old copies of this rounded the wrong way
	 below zero, which put a heading of a few degrees left of north a whole eighth further left. */
TEST(camera_heading_snaps_to_the_nearest_eighth_on_both_sides_of_zero)
{
	const Real step = PI / 4.0f;

	CHECK_NEAR( View_snapAngleToEighth( 0.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step ), -step, 0.0001f );

	// just off an eighth, either way, stays on it
	CHECK_NEAR( View_snapAngleToEighth( 0.1f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -0.1f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step + 0.1f ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step - 0.1f ), -step, 0.0001f );

	// past the halfway point it belongs to the next one, on both sides
	CHECK_NEAR( View_snapAngleToEighth( step * 0.6f ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step * 0.6f ), -step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step * 0.4f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step * 0.4f ), 0.0f, 0.0001f );

	CHECK_NEAR( View_snapAngleToEighth( 3.0f * step - 0.05f ), 3.0f * step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -3.0f * step + 0.05f ), -3.0f * step, 0.0001f );
}

/** One press of a rotate key is one eighth, from whatever the heading happens to be - including a
	 heading a script left off the grid, which is snapped first so the key never lands between two
	 eighths. */
TEST(a_rotate_key_press_moves_the_camera_exactly_one_eighth)
{
	const Real step = PI / 4.0f;

	CHECK_NEAR( View_stepAngleByEighths( 0.0f, 1 ), step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( 0.0f, -1 ), -step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( step, 1 ), 2.0f * step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( -step, -1 ), -2.0f * step, 0.0001f );

	// eight presses come back to where they started
	Real a = 0.0f;
	for( Int i = 0; i < 8; ++i )
		a = View_stepAngleByEighths( a, 1 );
	CHECK_NEAR( a, 2.0f * PI, 0.0001f );

	// off the grid: snapped first, then stepped, so the result is still an eighth
	CHECK_NEAR( View_stepAngleByEighths( 0.1f, 1 ), step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( -0.1f, -1 ), -step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( 0.0f, 0 ), 0.0f, 0.0001f );
}

/** Placing a structure buys a plan, not a building.  The object does go down at once - it is paid
	 for, it holds the ground, and it can be clicked and cancelled - but nothing is built until a
	 builder walks over to it, so it is drawn as the same translucent silhouette that was following
	 the cursor a moment earlier.  A plan on the map must never read as a building already standing
	 there. */
TEST(a_structure_waiting_for_its_builder_is_drawn_as_a_silhouette)
{
	CHECK( Object_isAwaitingBuilder( TRUE, 0.0f ) == TRUE );

	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ), PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );

	// what followed the cursor and what landed on the map are drawn at the same opacity
	CHECK_NEAR( Drawable_effectiveOpacity( PLACEMENT_SILHOUETTE_OPACITY, 1.0f, FALSE ),
							Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ), 0.0001f );

	// and it is see-through, which is the whole reason the renderer puts it in the translucent pass
	CHECK( Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ) != 1.0f );
}

/** The first percent of work is what ends the plan, so the moment the builder arrives and starts
	 the structure turns solid and stays solid for the rest of its life.  A finished building carries
	 CONSTRUCTION_COMPLETE (-1) and must not be mistaken for one sitting at zero. */
TEST(a_structure_the_builder_has_reached_is_drawn_solid)
{
	CHECK( Object_isAwaitingBuilder( TRUE, 0.1f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( TRUE, 99.9f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( FALSE, 0.0f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( FALSE, CONSTRUCTION_COMPLETE ) == FALSE );

	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 1.0f, FALSE ), 1.0f, 0.0001f );
}

/** The silhouette scales whatever opacity the drawable already asked for instead of replacing it,
	 so a stealthed or half-faded drawable is not dragged back up to 45% by being a plan, and one
	 that has been faded all the way out stays out. */
TEST(the_placement_silhouette_scales_the_opacity_it_is_given)
{
	CHECK_NEAR( Drawable_effectiveOpacity( 0.5f, 1.0f, TRUE ), 0.5f * PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );
	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 0.5f, TRUE ), 0.5f * PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );
	CHECK_NEAR( Drawable_effectiveOpacity( 0.0f, 1.0f, TRUE ), 0.0f, 0.0001f );
}

/** Ground you have already scouted stays buildable after your units leave it, so a base can be
	 planned out into the fog and the builder sent to walk there.  Shroud - terrain nobody of yours
	 has ever laid eyes on - is still off limits, which is what stops the map being read through a
	 placement cursor. */
TEST(a_base_can_be_planned_into_fog_but_not_into_shroud)
{
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_CLEAR ) == FALSE );
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_FOGGED ) == FALSE );
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_SHROUDED ) == TRUE );
}

/** A plan is not a scout.  A structure that has been placed but not started opens no shroud at all,
	 so drawing a base out into the fog cannot be used to see what is standing there.  Once the
	 builder arrives EA's own rule takes over - the structure sees itself and no further - and a
	 finished building goes back to the sight its template gives it. */
TEST(a_planned_structure_opens_no_shroud_until_the_work_starts)
{
	const Real templateRange = 300.0f;
	const Real boundingRadius = 40.0f;

	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 0.0f, boundingRadius ), 0.0f, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 0.1f, boundingRadius ), boundingRadius, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 99.9f, boundingRadius ), boundingRadius, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, FALSE, CONSTRUCTION_COMPLETE, boundingRadius ),
							templateRange, 0.0001f );

	// a structure that clears no shroud at all is not the same as one with no vision by template
	CHECK_NEAR( Object_shroudClearingRange( 0.0f, FALSE, CONSTRUCTION_COMPLETE, boundingRadius ), 0.0f, 0.0001f );
}

/** Since the plan reveals nothing, the fog it was placed in would swallow it - and there would be
	 nothing left on screen to click on and cancel.  Your own plan is drawn through the fog; anything
	 else fogged, including an enemy's, is still hidden. */
TEST(your_own_plan_is_drawn_through_the_fog_that_hides_everything_else)
{
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_FOGGED, TRUE ) == FALSE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_SHROUDED, TRUE ) == FALSE );

	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_FOGGED, FALSE ) == TRUE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_SHROUDED, FALSE ) == TRUE );

	// nothing changes for what the player can see anyway
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_CLEAR, FALSE ) == FALSE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_PARTIAL_CLEAR, FALSE ) == FALSE );
}

/** There is nothing to stop about a building that is still going up, so the stop key calls it off
	 instead - the same cancel, refund and all, that the command bar button on that structure does.
	 One structure of your own only: a mixed selection or anything already finished still means
	 stop. */
TEST(the_stop_key_cancels_a_building_that_is_still_going_up)
{
	CHECK( Command_stopMeansCancelConstruction( 1, TRUE, TRUE ) == TRUE );

	CHECK( Command_stopMeansCancelConstruction( 1, TRUE, FALSE ) == FALSE );		// finished building
	CHECK( Command_stopMeansCancelConstruction( 1, FALSE, TRUE ) == FALSE );	// not yours
	CHECK( Command_stopMeansCancelConstruction( 2, TRUE, TRUE ) == FALSE );		// more than one thing
	CHECK( Command_stopMeansCancelConstruction( 0, FALSE, FALSE ) == FALSE );	// nothing selected
}

/** The plan sits in fog on its owner's screen on purpose, and the fog gate on orders would then
	 refuse every click on it: no build cursor, no resume, nothing but selection.  A player's own
	 plan is never hidden from that player's own builders.  Everything else the gate does is
	 untouched - an enemy in fog is still out of reach, the AI and scripts still ignore the gate
	 entirely. */
TEST(the_fog_never_hides_your_own_plan_from_your_own_builder)
{
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, TRUE, TRUE ) == FALSE );		// your own plan
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, TRUE, FALSE ) == TRUE );		// anything else fogged

	CHECK( ActionManager_shroudHidesTarget( TRUE, TRUE, TRUE, FALSE ) == FALSE );		// from a script
	CHECK( ActionManager_shroudHidesTarget( FALSE, FALSE, TRUE, FALSE ) == FALSE );	// asked by the AI
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, FALSE, FALSE ) == FALSE );	// in plain sight
}

//-------------------------------------------------------------------------------------------------
/** A plan is a silhouette, not a building: cancelling it - or an enemy shooting it - must not set
	off the explosion, collapse and rubble of a structure that never stood.  Object::onDie skips
	its die modules and destroys it outright when this says so. */
//-------------------------------------------------------------------------------------------------
TEST(a_plan_dies_without_an_explosion_but_a_building_does_not)
{
	CHECK( Object_deathIsSilent( TRUE, 0.0f ) == TRUE );			// still waiting for its builder
	CHECK( Object_deathIsSilent( TRUE, 0.5f ) == FALSE );			// half up, it blows up like a building
	CHECK( Object_deathIsSilent( TRUE, 100.0f ) == FALSE );		// the last frame of construction
	CHECK( Object_deathIsSilent( FALSE, 0.0f ) == FALSE );		// finished: percent means nothing here
}
