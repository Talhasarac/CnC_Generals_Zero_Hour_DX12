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

// FILE: JobSystem.cpp ////////////////////////////////////////////////////////////////////////////
// Desc:   THREADING-ROADMAP.md 3.1 - a fork-join pool, and nothing else.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/JobSystem.h"
#include "Common/EarlyCommandLine.h"
#include "GameLogic/FPUControl.h"

namespace
{

	enum { MAX_WORKERS = 31 };		// one interlocked counter's worth; nothing here scales past it

	HANDLE				s_threads[ MAX_WORKERS ] = { 0 };
	Int						s_numWorkers = 0;
	Bool					s_running = FALSE;

	HANDLE				s_workReady = NULL;		// released once per worker at each fork
	HANDLE				s_allDone = NULL;			// auto-reset; set by the last worker to finish
	volatile LONG	s_quitting = 0;

	// The job in flight.  Written by the forking thread before the semaphore is released and read
	// by the workers after they wake on it, which is the pairing that publishes them.
	JobSystem::JobFunc	s_fn = NULL;
	void								*s_context = NULL;
	volatile LONG				s_nextIndex = 0;
	LONG								s_count = 0;
	LONG								s_grain = 1;
	volatile LONG				s_workersBusy = 0;

	// Per-thread, so the allocator can tell whose hand is in the pool without a lock of its own.
	__declspec(thread) Bool	s_isWorker = FALSE;
	volatile LONG						s_workerAllocations = 0;

	/** Claim chunks of the current job until there are none left.  Every thread in the fork runs
		* this, the forking one included - a pool of N-1 workers plus the caller is N threads of
		* work, and the caller blocking while the workers run would waste the best one of them. */
	void runChunks( void )
	{
		for( ;; )
		{
			const LONG start = InterlockedExchangeAdd( (LONG *)&s_nextIndex, s_grain );
			if( start >= s_count )
				break;

			LONG end = start + s_grain;
			if( end > s_count )
				end = s_count;

			for( LONG i = start; i < end; ++i )
				s_fn( (Int)i, s_context );
		}
	}

	DWORD WINAPI workerMain( LPVOID )
	{
		s_isWorker = TRUE;

		/* The FPU control word is per-thread and a new thread starts on the CRT default, not on
			 what setFPMode() left on the main thread.  A job that computes a float with a different
			 precision or rounding mode than the rest of the game is the kind of bug that shows up as
			 a rare visual difference and never as a crash, so this is not optional even for work
			 that never touches the simulation. */
		setFPMode();

		for( ;; )
		{
			WaitForSingleObject( s_workReady, INFINITE );
			if( s_quitting )
				break;

			runChunks();

			if( InterlockedDecrement( (LONG *)&s_workersBusy ) == 0 )
				SetEvent( s_allDone );
		}
		return 0;
	}

}  // anonymous namespace

//-------------------------------------------------------------------------------------------------
void JobSystem::init( Int workers )
{
	if( s_running )
		return;

	if( workers < 0 )
	{
		/* -jobthreads <n>, read here rather than from CommandLine.cpp's table because the pool is
			 started before that table runs and because 0 has to be reachable - which is the whole
			 point: it is the single-threaded baseline to measure a threading change against. */
		char value[ 32 ];
		if( findEarlyCommandLineValue( L"-jobthreads", value, sizeof(value) ) )
		{
			workers = atoi( value );
		}
		else
		{
			SYSTEM_INFO si;
			::GetSystemInfo( &si );
			// The forking thread works too, so the pool wants one fewer than the machine has.
			workers = (Int)si.dwNumberOfProcessors - 1;
		}
	}
	if( workers > MAX_WORKERS )
		workers = MAX_WORKERS;
	if( workers < 0 )
		workers = 0;

	s_quitting = 0;
	s_numWorkers = 0;

	if( workers > 0 )
	{
		s_workReady = ::CreateSemaphore( NULL, 0, MAX_WORKERS, NULL );
		s_allDone = ::CreateEvent( NULL, FALSE, FALSE, NULL );
		if( s_workReady == NULL || s_allDone == NULL )
		{
			// No pool is a supported configuration - parallel_for runs inline - so a failure here
			// costs speed, never correctness.
			DEBUG_LOG(("JobSystem: could not create sync objects, running single threaded\n"));
			if( s_workReady ) { ::CloseHandle( s_workReady ); s_workReady = NULL; }
			if( s_allDone ) { ::CloseHandle( s_allDone ); s_allDone = NULL; }
			workers = 0;
		}
	}

	for( Int i = 0; i < workers; ++i )
	{
		DWORD id = 0;
		s_threads[ i ] = ::CreateThread( NULL, 0, workerMain, NULL, 0, &id );
		if( s_threads[ i ] == NULL )
			break;						// take what we got; the pool is allowed to be smaller than asked for
		++s_numWorkers;
	}

	s_running = TRUE;
	DEBUG_LOG(("JobSystem: %d worker threads\n", s_numWorkers));
}

//-------------------------------------------------------------------------------------------------
void JobSystem::shutdown( void )
{
	if( !s_running )
		return;

	/* There is never work in flight here: parallel_for does not return until its fork has joined,
		 so by the time anything can call this every worker is parked on the semaphore. */
	s_quitting = 1;
	if( s_workReady && s_numWorkers > 0 )
		::ReleaseSemaphore( s_workReady, s_numWorkers, NULL );

	for( Int i = 0; i < s_numWorkers; ++i )
	{
		if( s_threads[ i ] )
		{
			/* Bounded, not INFINITE.  This runs on the way out of the process and a worker that
				 somehow never wakes must not be able to hang the exit; the handle leaks instead,
				 which costs nothing at that point. */
			if( ::WaitForSingleObject( s_threads[ i ], 5000 ) == WAIT_OBJECT_0 )
				::CloseHandle( s_threads[ i ] );
			else
				DEBUG_LOG(("JobSystem: worker %d did not exit, leaking its handle\n", i));
			s_threads[ i ] = NULL;
		}
	}
	s_numWorkers = 0;

	if( s_workReady ) { ::CloseHandle( s_workReady ); s_workReady = NULL; }
	if( s_allDone ) { ::CloseHandle( s_allDone ); s_allDone = NULL; }
	s_running = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerCount( void )
{
	return s_numWorkers;
}

//-------------------------------------------------------------------------------------------------
Bool JobSystem::isWorkerThread( void )
{
	return s_isWorker;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerAllocationCount( void )
{
	return (Int)s_workerAllocations;
}

//-------------------------------------------------------------------------------------------------
void JobSystem::noteWorkerAllocation( void )
{
	InterlockedIncrement( (LONG *)&s_workerAllocations );
}

//-------------------------------------------------------------------------------------------------
void JobSystem::parallel_for( Int count, Int granularity, JobFunc fn, void *context )
{
	if( count <= 0 || fn == NULL )
		return;

	if( granularity < 1 )
		granularity = 1;

	/* Inline when there is nobody to hand work to, or when the whole range is one chunk anyway.
		 Same code path as the parallel case would take for a single chunk, so the single-threaded
		 machine is running tested code rather than a second implementation. */
	if( s_numWorkers <= 0 || count <= granularity )
	{
		for( Int i = 0; i < count; ++i )
			fn( i, context );
		return;
	}

	DEBUG_ASSERTCRASH( !s_isWorker, ("JobSystem::parallel_for from inside a job - this pool does not nest") );

	s_fn = fn;
	s_context = context;
	s_count = (LONG)count;
	s_grain = (LONG)granularity;
	s_nextIndex = 0;
	s_workersBusy = s_numWorkers;

	::ReleaseSemaphore( s_workReady, s_numWorkers, NULL );

	runChunks();		// the forking thread is a worker too

	/* Wait for the workers, not for the work: the chunks can all be claimed by this thread while a
		 worker is still on its way out of WaitForSingleObject, and returning then would let the
		 caller reuse the output buffers under it. */
	::WaitForSingleObject( s_allDone, INFINITE );

	s_fn = NULL;
	s_context = NULL;
}
