/*
 * CdaPfn.h - stub for the SafeDisc "protected function" markers.
 *
 * The real header ships with the SafeDisc SDK, which is binary-only and was
 * stripped from the source release.  Its macros wrap a function so the
 * copy-protection layer can verify it at runtime; with no SDK there is nothing
 * to verify, so they expand to nothing.
 *
 * WinMain.cpp is the only file in the tree that uses them (munkeeFunc, guarded
 * by _INTERNAL), so this is the whole surface: two macros and the two constants
 * they are called with.
 */

#pragma once

#ifndef __CDAPFN_H_
#define __CDAPFN_H_

#define CDAPFN_OVERHEAD_L1 0
#define CDAPFN_OVERHEAD_L2 0
#define CDAPFN_OVERHEAD_L3 0
#define CDAPFN_OVERHEAD_L4 0
#define CDAPFN_OVERHEAD_L5 0

#define CDAPFN_CONSTRAINT_NONE 0

// expands to an empty declaration - the call site writes the trailing semicolon
#define CDAPFN_DECLARE_GLOBAL( fn, overhead, constraint )

#define CDAPFN_ENDMARK( fn ) ((void)0)

#endif // __CDAPFN_H_
