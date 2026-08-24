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
#include "GameClient/Water.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/ControlBar.h"

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

//////////////////////////////////////////////////////////////////////////////
// Control bar three-panel layout
//////////////////////////////////////////////////////////////////////////////
/*
 * ControlBarComputePanelLayout is the geometry half of ControlBar::layoutPanels:
 * screen size in, uniform scale and the three panel rectangles out.  It is split
 * out precisely so it can be driven at resolutions this machine cannot display -
 * the bar is laid out once at startup and there is no way to eyeball 1280x1024
 * without owning that monitor.
 *
 * The panels' authored rectangles were carved out of one continuous strip and add
 * up to 810 of the 800 design units.  The first cut of this layout scaled by
 * min(w/800, h/600), which on 16:9 is the height and leaves width to spare, but at
 * 4:3 and squarer is the width - so 810 units of panel went into 800 units of
 * screen, both seams overlapped, and the gaps the whole design is built around
 * closed to nothing.  Hence panels_keep_a_real_gap_on_square_screens below.
 */

static const Int theTestResolutions[][2] =
{
	{  800, 600 },		// 4:3, the resolution the .wnd is authored at
	{ 1024, 768 },		// 4:3
	{ 1280, 960 },		// 4:3
	{ 1280, 1024 },		// 5:4, squarer than the design
	{ 1600, 1200 },		// 4:3
	{ 1280, 720 },		// 16:9
	{ 1920, 1080 },		// 16:9
	{ 2560, 1080 },		// 21:9
};
static const Int theTestResolutionCount = sizeof(theTestResolutions) / sizeof(theTestResolutions[0]);

TEST(controlbar_panels_keep_a_real_gap_on_square_screens)
{
	/* The regression.  Every seam has to show world through it, at every aspect -
	   this is what failed at 4:3 and 5:4 when the scale was bounded by width alone. */
	for (Int i = 0; i < theTestResolutionCount; ++i)
	{
		IRegion2D panel[ControlBar::CB_PANEL_COUNT];
		Real scale = 0.0f;
		ControlBarComputePanelLayout(theTestResolutions[i][0], theTestResolutions[i][1], panel, &scale);

		CHECK(panel[ControlBar::CB_PANEL_CENTER].lo.x - panel[ControlBar::CB_PANEL_LEFT].hi.x >= 16);
		CHECK(panel[ControlBar::CB_PANEL_RIGHT].lo.x - panel[ControlBar::CB_PANEL_CENTER].hi.x >= 16);
	}
}

TEST(controlbar_panels_stay_on_screen_and_hug_their_edges)
{
	for (Int i = 0; i < theTestResolutionCount; ++i)
	{
		const Int w = theTestResolutions[i][0], h = theTestResolutions[i][1];
		IRegion2D panel[ControlBar::CB_PANEL_COUNT];
		Real scale = 0.0f;
		ControlBarComputePanelLayout(w, h, panel, &scale);

		// left panel pinned to the left edge, right panel to the right, all of it on screen
		CHECK_EQ(panel[ControlBar::CB_PANEL_LEFT].lo.x, 0);
		CHECK_EQ(panel[ControlBar::CB_PANEL_RIGHT].hi.x, w);

		for (Int p = 0; p < ControlBar::CB_PANEL_COUNT; ++p)
		{
			CHECK(panel[p].lo.x >= 0);
			CHECK(panel[p].hi.x <= w);
			CHECK(panel[p].lo.y >= 0);
			CHECK_EQ(panel[p].hi.y, h);		// every panel sits on the bottom edge
			CHECK(panel[p].width() > 0);
			CHECK(panel[p].height() > 0);
		}
	}
}

TEST(controlbar_panels_are_never_stretched)
{
	/* One uniform scale is the point of the whole exercise: a panel's shape must not
	   depend on the aspect of the screen it is drawn on, or the cameos inside it come
	   out as rectangles.  Compare each panel's width:height against the authored one. */
	static const Real designRatio[ControlBar::CB_PANEL_COUNT] =
	{
		186.0f / 179.0f,		// left   - design rect 0,421 .. 186,600
		438.0f / 168.0f,		// center - design rect 186,432 .. 624,600
		186.0f / 172.0f,		// right  - design rect 614,428 .. 800,600
	};

	for (Int i = 0; i < theTestResolutionCount; ++i)
	{
		IRegion2D panel[ControlBar::CB_PANEL_COUNT];
		Real scale = 0.0f;
		ControlBarComputePanelLayout(theTestResolutions[i][0], theTestResolutions[i][1], panel, &scale);

		for (Int p = 0; p < ControlBar::CB_PANEL_COUNT; ++p)
		{
			const Real ratio = (Real)panel[p].width() / (Real)panel[p].height();
			CHECK_NEAR(ratio, designRatio[p], 0.03f);		// slack for the floor/ceil rounding
		}
	}
}

TEST(controlbar_scale_is_bounded_by_height_on_a_wide_screen)
{
	/* 16:9 has width to spare, so the height is what runs out first and the bar is
	   drawn at exactly h/600 - the same size it would be on a 4:3 screen of that
	   height, which is what keeps it from growing as monitors get wider. */
	IRegion2D panel[ControlBar::CB_PANEL_COUNT];
	Real scale = 0.0f;
	ControlBarComputePanelLayout(1920, 1080, panel, &scale);
	CHECK_NEAR(scale, 1080.0f / 600.0f, 0.001f);
}

TEST(controlbar_scale_is_bounded_by_the_gap_on_a_square_screen)
{
	/* 5:4: neither the width nor the height bound leaves room between the panels, so
	   the gap bound is the one that has to win.  w/800 = 1.6 and h/600 = 1.7067 both
	   lose to (w/2 - 24) / 405. */
	IRegion2D panel[ControlBar::CB_PANEL_COUNT];
	Real scale = 0.0f;
	ControlBarComputePanelLayout(1280, 1024, panel, &scale);
	CHECK(scale < 1280.0f / 800.0f);
	CHECK_NEAR(scale, (1280.0f * 0.5f - 24.0f) / 405.0f, 0.001f);
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
 * blast radius of changing it is the entire game.  ControlBarComputePanelLayout works
 * around it locally by flooring and pinning the edges that have to be exact - a screen
 * height that ceils to one past the bottom of the screen is where this turned up.
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
