/*
 * Minimal Miles Sound System 6 stub header.
 *
 * The real Miles SDK is binary-only and absent from this repo (Libraries/Source/
 * WWVegas/Miles6 is gitignored, like the other stripped SDKs). WWAudio headers
 * pull Mss.H in for a handful of opaque handles and scalar typedefs only, so the
 * declarations below are enough to compile everything the ww3d2 target reaches
 * (soundrobj.cpp -> wwaudio.h). No audio actually plays.
 *
 * ponytail: stub sized to what WWAudio/*.h references today; grow it per missing
 * symbol when Phase 4 links MilesAudioDevice, or drop it entirely if a real
 * audio backend (OpenAL/miniaudio) replaces Miles.
 */

#ifndef MSS_H
#define MSS_H

/* The real Mss.H pulls in the Windows multimedia headers; WWAudio relies on that
 * for LPWAVEFORMAT in its Miles-facing signatures. */
#include <windows.h>
#include <mmsystem.h>

#ifndef AILCALLBACK
#define AILCALLBACK __stdcall
#endif

typedef signed char        S8;
typedef unsigned char      U8;
typedef signed short       S16;
typedef unsigned short     U16;
typedef signed int         S32;
typedef unsigned int       U32;
typedef float              F32;
typedef double             F64;

/* Opaque Miles handles. The real SDK declares these as pointers to undefined
 * structs; keep that shape so `= NULL` and pointer comparisons still compile. */
struct _AIL_DIGDRIVER;   typedef struct _AIL_DIGDRIVER  *HDIGDRIVER;
struct _AIL_SAMPLE;      typedef struct _AIL_SAMPLE     *HSAMPLE;
struct _AIL_STREAM;      typedef struct _AIL_STREAM     *HSTREAM;
struct _AIL_PROVIDER;    typedef struct _AIL_PROVIDER   *HPROVIDER;
struct _AIL_3DSAMPLE;    typedef struct _AIL_3DSAMPLE   *H3DSAMPLE;
struct _AIL_3DPOBJECT;   typedef struct _AIL_3DPOBJECT  *H3DPOBJECT;
struct _AIL_TIMER;       typedef struct _AIL_TIMER      *HTIMER;

/* Global mutex around the Miles mixer; WWAudio/Utils.h wraps it in MMSLockClass. */
void AIL_lock(void);
void AIL_unlock(void);

#endif /* MSS_H */
