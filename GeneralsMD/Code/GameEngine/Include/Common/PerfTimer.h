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

// PerfTimer.h ////////////////////////////////////////////////////////////////////////////////////
// John McDonald
// July 2002

#pragma once

#ifndef __PERFTIMER_H__
#define __PERFTIMER_H__

#if defined(_DEBUG) || defined(_INTERNAL)
	/*
		NOTE NOTE NOTE: never check this in with this enabled, since there is a nonzero time penalty
		for running in this mode. Only enable it for local builds for testing purposes! (srj)
	*/
	#define NO_PERF_TIMERS
#else
	#define NO_PERF_TIMERS
#endif

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not

#ifdef PERF_TIMERS
#include "GameLogic/GameLogic.h"
#include "Common/PerfMetrics.h"
#include "Common/GlobalData.h"
#endif

// Forward Declarations
class DebugDisplayInterface;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#define NO_USE_QPF	// non-QPF is much faster.

#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
//-------------------------------------------------------------------------------------------------
void InitPrecisionTimer();

//-------------------------------------------------------------------------------------------------
void GetPrecisionTimerTicksPerSec(Int64* t);

//-------------------------------------------------------------------------------------------------
__forceinline void GetPrecisionTimer(Int64* t)
{
#ifdef USE_QPF
	QueryPerformanceCounter((LARGE_INTEGER*)t);
#else
	// CPUID is needed to force serialization of any previous instructions. 
	__asm 
	{
		// for now, I am commenting this out. It throws the timings off a bit more (up to .001%) jkmcd
		//		CPUID
		RDTSC
		MOV ECX,[t]
		MOV [ECX], EAX
		MOV [ECX+4], EDX
	}
#endif
}
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#ifdef PERF_TIMERS

//-------------------------------------------------------------------------------------------------
class PerfGather
{
public:
	// If net only (default), subtract perf timers running inside. [8/12/2003]
	PerfGather( const char *identifier, Bool netOnly=true );
	virtual ~PerfGather( );

	__forceinline void startTimer();
	__forceinline void stopTimer();

	enum
	{
		PERF_GROSSTIME = 0x01,
		PERF_NETTIME = 0x02,
		PERF_CALLCOUNT = 0x04
	};

	static void resetAll();
	static void initPerfDump(const char* fname, Int options);
	static void termPerfDump();
	static void dumpAll(UnsignedInt frame);
	static void displayGraph(UnsignedInt frame);

	/* THREADING-ROADMAP.md section 0 step 3: the per-frame CSV answers "what did frame 4312
		 cost", which is the wrong question when deciding what to thread.  accumulateFrame() folds
		 the frame that just ended into a running total and dumpSummary() prints the average cost
		 per frame per scope at the end of the run, next to HEADLESS RESULT.  Call accumulateFrame
		 exactly once per frame, before resetAll().

		 `frameMS` is the whole loop pass as the eye saw it.  A pass longer than SPIKE_MS gets its
		 own line in the log with the scopes that owned it, because an average cannot tell you what
		 a stutter was - only the frame it happened on can, and by the end of the run that frame is
		 long gone. */
	enum { SPIKE_MS = 17 };			// one frame at 60Hz; anything longer is visible

	static void accumulateFrame(Real frameMS);
	static void dumpSummary();

	void reset();

private:

	/* The stack of gathers currently open, so a scope can subtract its children's time from its
		 own.  Per *thread*, not per process: CriticalSection::enter and FileSystem::openFile both
		 carry a gather and both are called from threads that are not the main one (the texture
		 loader, the log writer, the job pool).  Sharing one stack between them walks the head off
		 the end of the array and corrupts whatever static happens to sit after it - which is what
		 a PERF_TIMERS build used to die of on its first rendered frame.

		 An index rather than a pointer because a thread-local pointer cannot be initialised with
		 the address of a thread-local array; the index is zero-initialised per thread for free,
		 and slot 0 stays the NULL "no parent" sentinel. */
	enum { MAX_ACTIVE_STACK = 256 };
	static __declspec(thread) PerfGather* m_active[MAX_ACTIVE_STACK];
	static __declspec(thread) Int m_activeDepth;
	/* TRUE only on the thread initPerfDump() was called from.  The counters below are shared and
		 an Int64 += is two writes on x86, so a gather on a scope several threads enter (CritSec,
		 FileSystem) used to report torn nonsense - the FileSystem row came out at 27 *seconds* per
		 frame.  Only the profiled thread accumulates now, which is also the right answer to the
		 question the table exists for: where the frame goes is where the main thread goes. */
	static __declspec(thread) Bool m_gatherOnThisThread;
	static Int64 s_stopStartOverhead;	// overhead for stop+start a timer

	static PerfGather*& getHeadPtr();

	void addToList();
	void removeFromList();

	/* The counters below are shared, and a gather on a scope that several threads enter (CritSec,
		 FileSystem) has them incremented from several threads without a lock.  On x86 an Int64 +=
		 is two writes, so such a row can be off by a torn update.  Left alone deliberately: this is
		 a measurement build, the affected rows are the ones whose absolute value nobody acts on,
		 and making them atomic would cost more than the thing being measured. */
	const char*		m_identifier;
	Int64					m_startTime;
	Int64					m_runningTimeGross;
	Int64					m_runningTimeNet;
	Int						m_callCount;
	Int64					m_totalTimeNet;			// summed over every frame accumulateFrame() saw
	Int64					m_totalTimeGross;
	Int64					m_totalCallCount;
	Int64					m_worstFrameNet;		// the single worst frame this scope ever had
	PerfGather*		m_next;
	PerfGather*		m_prev;
	Bool					m_ignore;
	Bool					m_netTimeOnly;
};

//-------------------------------------------------------------------------------------------------
void PerfGather::startTimer()
{
	if (!m_gatherOnThisThread)
		return;

	// The depth moves whether or not the slot exists, so start and stop stay balanced even if
	// something nests deeper than the array; only the parent attribution is lost.
	if (++m_activeDepth < MAX_ACTIVE_STACK)
		m_active[m_activeDepth] = this;
	GetPrecisionTimer(&m_startTime);
}

//-------------------------------------------------------------------------------------------------
void PerfGather::stopTimer()
{
	DEBUG_ASSERTCRASH(this != NULL, ("I am null, uh oh"));

	if (!m_gatherOnThisThread)
		return;

	Int64 runTime;
	GetPrecisionTimer(&runTime);

	runTime -= m_startTime;

	m_runningTimeGross += runTime;
	m_runningTimeNet += runTime;

	++m_callCount;

#ifdef _DEBUG
	DEBUG_ASSERTCRASH(m_activeDepth > 0 && m_activeDepth < MAX_ACTIVE_STACK, ("active under/over flow"));
	DEBUG_ASSERTCRASH(m_active[m_activeDepth] == this, ("I am not the active timer, uh oh"));
#endif
	--m_activeDepth;

	PerfGather *parent = (m_activeDepth > 0 && m_activeDepth < MAX_ACTIVE_STACK) ? m_active[m_activeDepth] : NULL;
	if (parent)
	{
		// don't add the time it took for us to actually get the ticks (in startTimer) to our parent...
		parent->m_runningTimeGross -= (s_stopStartOverhead);
		if (parent->m_netTimeOnly) {
			parent->m_runningTimeNet -= (runTime + s_stopStartOverhead);
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class AutoPerfGather
{
private:
	PerfGather& m_g;
public:
	__forceinline AutoPerfGather(PerfGather& g);
	__forceinline ~AutoPerfGather();
};

//-------------------------------------------------------------------------------------------------
AutoPerfGather::AutoPerfGather(PerfGather& g) : m_g(g)
{
	m_g.startTimer();
}

//-------------------------------------------------------------------------------------------------
AutoPerfGather::~AutoPerfGather()
{
	m_g.stopTimer();
}

//-------------------------------------------------------------------------------------------------
class AutoPerfGatherIgnore
{
private:
	static Bool s_ignoring;
	PerfGather& m_g;
	Bool				m_oldIgnore;
public:
	__forceinline AutoPerfGatherIgnore(PerfGather& g);
	__forceinline ~AutoPerfGatherIgnore();
};

//-------------------------------------------------------------------------------------------------
AutoPerfGatherIgnore::AutoPerfGatherIgnore(PerfGather& g) : m_g(g)
{
	m_oldIgnore = s_ignoring;
	s_ignoring = true;

	m_g.startTimer();
}

//-------------------------------------------------------------------------------------------------
AutoPerfGatherIgnore::~AutoPerfGatherIgnore()
{
	m_g.stopTimer();

	if (s_ignoring)
		m_g.reset();

	s_ignoring = m_oldIgnore;
}

//-------------------------------------------------------------------------------------------------
#define DECLARE_TOTAL_PERF_TIMER(id)					static PerfGather s_##id(#id, false); 
#define DECLARE_PERF_TIMER(id)					static PerfGather s_##id(#id); 
#define USE_PERF_TIMER(id)							AutoPerfGather a_##id(s_##id);
#define IGNORE_PERF_TIMER(id)						AutoPerfGatherIgnore a_##id(s_##id);

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class PerfTimer
{
public:
	PerfTimer( const char *identifier, Bool crashWithInfo = true, Int startFrame = 0, Int endFrame = -1);
	virtual ~PerfTimer( );
	__forceinline void startTimer( void );
	__forceinline void stopTimer( void );
	
protected:
	Int64 m_startTime;

protected:
	void outputInfo( void );
	void showMetrics( void );

protected:
	const char *m_identifier;
	Bool m_crashWithInfo;
	UnsignedInt m_startFrame;
	UnsignedInt m_endFrame;
	UnsignedInt m_lastFrame;	// last frame we got data from
	Bool m_outputInfo;

	// total running time so far.
	Int64 m_runningTime;
	Int m_callCount;

	friend void StatMetricsDisplay( DebugDisplayInterface *dd, void *, FILE *fp );
	friend void EndStatMetricsDisplay( DebugDisplayInterface *dd, void *, FILE *fp );
};

//-------------------------------------------------------------------------------------------------
void PerfTimer::startTimer( void )
{
	UnsignedInt frm = (TheGameLogic ? TheGameLogic->getFrame() : m_startFrame);
	if (frm >= m_startFrame && (m_endFrame == -1 || frm <= m_endFrame)) 
	{
		GetPrecisionTimer(&m_startTime);
	}
}

//-------------------------------------------------------------------------------------------------
void PerfTimer::stopTimer( void )
{
	UnsignedInt frm = (TheGameLogic ? TheGameLogic->getFrame() : m_startFrame);
	if (frm >= m_startFrame && (m_endFrame == -1 || frm <= m_endFrame)) 
	{
		Int64 tmp;
		GetPrecisionTimer(&tmp);
		m_runningTime += (tmp - m_startTime);
		++m_callCount;
		m_lastFrame = frm;
	}
	

	if (TheGlobalData && TheGlobalData->m_showMetrics && m_endFrame > m_startFrame + PERFMETRICS_BETWEEN_METRICS) {
		m_endFrame = m_startFrame + PERFMETRICS_BETWEEN_METRICS;
	}

	if (m_endFrame > 0 && frm >= m_endFrame) {
		if (TheGlobalData->m_showMetrics) {
			showMetrics();
		}

		outputInfo();
	}
}


//-------------------------------------------------------------------------------------------------
extern void StatMetricsDisplay( DebugDisplayInterface *dd, void *, FILE *fp );

#else		// PERF_TIMERS

	#define DECLARE_PERF_TIMER(id)					
	#define  DECLARE_TOTAL_PERF_TIMER(id)					
	#define USE_PERF_TIMER(id)
	#define IGNORE_PERF_TIMER(id)	

#endif	// PERF_TIMERS

#endif /* __PERFTIMER_H__ */
