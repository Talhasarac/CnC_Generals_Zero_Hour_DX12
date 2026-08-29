/*
 * No-op stand-in for binkw32.dll.  Nothing here ever runs: it exists only so
 * the linker can produce an import library whose symbols carry the decorated
 * names the shipped BINKW32.DLL exports, which is what generals.exe binds to
 * at run time.  (`lib /def` alone cannot make that import library - it prefixes
 * a second underscore onto the already-decorated names.)
 *
 * The stub DLL built from this file must never be copied next to generals.exe.
 */
#include "bink.h"

HBINK __stdcall BinkOpen(const char *name, unsigned int flags) { return 0; }
void  __stdcall BinkClose(HBINK bnk) {}
int   __stdcall BinkWait(HBINK bnk) { return 0; }
int   __stdcall BinkDoFrame(HBINK bnk) { return 0; }
void  __stdcall BinkNextFrame(HBINK bnk) {}
int   __stdcall BinkGoto(HBINK bnk, unsigned int frame, int flags) { return 0; }
int   __stdcall BinkCopyToBuffer(HBINK bnk, void *dest, int destpitch,
                                 unsigned int destheight, unsigned int destx,
                                 unsigned int desty, unsigned int flags) { return 0; }
int   __stdcall BinkSetVolume(HBINK bnk, unsigned int trackid, int volume) { return 0; }
void  __stdcall BinkSetSoundTrack(unsigned int total_tracks, unsigned int *tracks) {}
int   __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN open, unsigned int param) { return 0; }
int   __stdcall BinkOpenDirectSound(unsigned int param) { return 0; }
