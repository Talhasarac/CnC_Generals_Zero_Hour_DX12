/*
 * bink.h - reconstructed subset of RAD Game Tools' Bink 1.x API.
 *
 * The Bink SDK is binary-only and was stripped from the source release, but the
 * game ships BINKW32.DLL and that DLL exports decorated stdcall names
 * (_BinkOpen@8, ...).  So BinkVideoPlayer only needs declarations, and the
 * stub in ../stub/ supplies an import library that binds to the real DLL at
 * runtime - the same recipe as Miles6/ and mss32.dll.
 *
 * Provenance of every declaration below:
 *   - the @N byte counts in BINKW32.DLL's export table fix the argument count
 *     of each function (dumpbin /exports), and the call sites in
 *     GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp fix the
 *     argument types.
 *   - the BINK struct's leading fields are Bink 1's documented public layout;
 *     the port reads only the seven declared here and Tests/bink_smoke.c
 *     checks all of them against the .bik file header.
 *   - the surface and open flags are Bink 1's published constants.
 */

#ifndef __BINK_H__
#define __BINK_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RAD's radbase.h shorthand.  BinkVideoPlayer.cpp declares its surface flag as
 * a "u32", so the missing SDK header supplied these - without them that file
 * does not compile.
 */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

/* Open flags. */
#define BINKPRELOADALL      0x00002000L   /* read the whole movie into memory */

/* Surface types for BinkCopyToBuffer. */
#define BINKSURFACE8P       0
#define BINKSURFACE24       1
#define BINKSURFACE24R      2
#define BINKSURFACE32       3
#define BINKSURFACE32R      4
#define BINKSURFACE32A      5
#define BINKSURFACE32RA     6
#define BINKSURFACE4444     7
#define BINKSURFACE5551     8
#define BINKSURFACE555      9
#define BINKSURFACE565      10
#define BINKSURFACE655      11
#define BINKSURFACE664      12
#define BINKSURFACEYUY2     13
#define BINKSURFACEUYVY     14
#define BINKSURFACEYV12     15

#define BINKCOPYALL         0x80000000L   /* copy the whole frame, not the dirty rects */

/*
 * The real BINK struct continues well past FrameRateDiv; it is allocated by
 * BinkOpen inside the DLL and only ever read here, so declaring the prefix the
 * port uses is enough - the trailing fields' offsets never come into play.
 */
typedef struct BINK
{
	unsigned int Width;           /* frame width, 1 based */
	unsigned int Height;          /* frame height, 1 based */
	unsigned int Frames;          /* number of frames, 1 based */
	unsigned int FrameNum;        /* frame about to be displayed, 1 based */
	unsigned int LastFrameNum;    /* last displayed frame, 1 based, 0 = none */
	unsigned int FrameRate;       /* frame rate numerator */
	unsigned int FrameRateDiv;    /* frame rate denominator */
} BINK, *HBINK;

typedef int (__stdcall *BINKSNDSYSOPEN)(unsigned int param);

HBINK __stdcall BinkOpen(const char *name, unsigned int flags);
void  __stdcall BinkClose(HBINK bnk);
int   __stdcall BinkWait(HBINK bnk);
int   __stdcall BinkDoFrame(HBINK bnk);
void  __stdcall BinkNextFrame(HBINK bnk);
int   __stdcall BinkGoto(HBINK bnk, unsigned int frame, int flags);
int   __stdcall BinkCopyToBuffer(HBINK bnk, void *dest, int destpitch,
                                 unsigned int destheight, unsigned int destx,
                                 unsigned int desty, unsigned int flags);
int   __stdcall BinkSetVolume(HBINK bnk, unsigned int trackid, int volume);
void  __stdcall BinkSetSoundTrack(unsigned int total_tracks, unsigned int *tracks);
int   __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN open, unsigned int param);
int   __stdcall BinkOpenDirectSound(unsigned int param);

#define BinkSoundUseDirectSound(lpDS) \
	BinkSetSoundSystem((BINKSNDSYSOPEN)BinkOpenDirectSound, (unsigned int)(lpDS))

#ifdef __cplusplus
}
#endif

#endif /* __BINK_H__ */
