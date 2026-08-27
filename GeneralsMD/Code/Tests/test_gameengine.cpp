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
#include "GameClient/Water.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/IncomingDamage.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ScriptEngine.h"
#include "Common/TunnelTracker.h"
#include "Common/StateMachine.h"

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
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 4 ), 1600 );

	/* ...but never past the ceiling, however long it stays down. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 5 ), CONNECTION_MAX_RETRY_TIME );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 50 ), CONNECTION_MAX_RETRY_TIME );
	CHECK_EQ( (Int)Connection_retryDelayFor( CONNECTION_MAX_RETRY_TIME, 3 ), CONNECTION_MAX_RETRY_TIME );

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
