/*
 * Stub for the BYTEmark/nbench benchmark library.
 *
 * The real sources (nbench0/nbench1/emfloat/misc/sysspec, per Benchmark.dsp) were
 * stripped from the repo along with the other third-party code.  Exactly one call
 * site exists in the whole game:
 *
 *   GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp
 *     -> testMinimumRequirements() -> RunBenchmark(0, NULL, &f, &i, &m)
 *
 * and its results only feed GameLODManager, which compares them against the
 * BenchProfile presets in the game's INI to pick a static detail level on first
 * launch (GameLOD.cpp:299-320).  Nothing else in the engine reads them, so a
 * constant is enough to boot; running BYTEmark on a 2020s CPU would in any case
 * report "far faster than every 2003 preset", which is what the constants say.
 *
 * ponytail: fixed indices, not a measurement.  These are the calibration knob for
 * detail-level selection -- if Phase 5 shows the game picking a silly LOD, tune
 * them against the shipped BenchProfile entries (or vendor real nbench) rather
 * than patching GameLOD.  Higher = machine rated faster.
 */

#include "benchmark.h"

/* Roughly "top of the 2003 preset table, with headroom". Same units the shipped
 * BenchProfile INI entries use; ratios are all GameLOD ever takes of them. */
#define BENCH_FLOAT_INDEX 100.0f
#define BENCH_INT_INDEX   100.0f
#define BENCH_MEM_INDEX   100.0f

int RunBenchmark(int argc, char *argv[], float *floatResult, float *intResult, float *memResult)
{
	(void)argc;
	(void)argv;

	/* The one caller always passes all three, but the real nbench tolerated NULLs
	 * and this is an external-facing C entry point, so keep the checks. */
	if (floatResult != 0) *floatResult = BENCH_FLOAT_INDEX;
	if (intResult   != 0) *intResult   = BENCH_INT_INDEX;
	if (memResult   != 0) *memResult   = BENCH_MEM_INDEX;

	return 0;
}
