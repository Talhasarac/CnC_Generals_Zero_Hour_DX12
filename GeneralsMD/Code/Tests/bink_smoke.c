/*
 * bink_smoke - checks the reconstructed Bink ABI in Libraries/Source/WWVegas/
 * Bink/include/bink.h against the real BINKW32.DLL the game ships.
 *
 * Nothing in that header could be taken from an SDK, so the two things it had
 * to guess are exactly what this proves:
 *
 *   1. the BINK struct's leading fields - Width/Height/Frames/FrameRate/
 *      FrameRateDiv are compared against the same values read straight out of
 *      the .bik file header, so a shifted offset shows up as a mismatch;
 *   2. the BINKSURFACE* constants - one frame is decoded twice, once as 32 bit
 *      and once as 24 bit, and the two must agree pixel for pixel.
 *
 * It loads ".\\BINKW32.DLL" by an explicit path on purpose (a backslash: LoadLibrary
 * does not take forward slashes in a path, it would fall back to the search
 * order and find the stub next to this exe): the plain name would
 * find the no-op stub sitting next to this exe in the build tree.  Run it with
 * GeneralsMD/Run as the working directory (CTest does).  With no game data it
 * reports "skip" and passes - Run/ is not in the repo.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "bink.h"

static int failures = 0;

#define CHECK_EQ(a, b, what)                                                   \
	do {                                                                        \
		unsigned int _a = (unsigned int)(a), _b = (unsigned int)(b);              \
		if (_a != _b) {                                                          \
			printf("FAIL %s: %u != %u\n", (what), _a, _b);                        \
			++failures;                                                            \
		}                                                                        \
	} while (0)

static unsigned int rd32(const unsigned char *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

int main(int argc, char **argv) {
	const char *path = (argc > 1) ? argv[1] : "Data/English/Movies/EA_LOGO.BIK";
	unsigned char hdr[40];
	char dllPath[MAX_PATH];
	unsigned int fileFrames, fileWidth, fileHeight, fileRate, fileRateDiv;
	unsigned char *buf32, *buf24;
	unsigned int w, h, i, frame;
	int nonBlack = 0;
	HMODULE dll;
	HBINK bnk;

	HBINK (__stdcall *pOpen)(const char *, unsigned int);
	void  (__stdcall *pClose)(HBINK);
	int   (__stdcall *pDoFrame)(HBINK);
	void  (__stdcall *pNextFrame)(HBINK);
	int   (__stdcall *pCopy)(HBINK, void *, int, unsigned int, unsigned int,
	                         unsigned int, unsigned int);
	void  (__stdcall *pSetSoundTrack)(unsigned int, unsigned int *);
	const char *(__stdcall *pGetError)(void);

	FILE *f = fopen(path, "rb");
	if (!f || fread(hdr, 1, sizeof hdr, f) != sizeof hdr) {
		if (f) fclose(f);
		printf("skip: no %s (game data is not in the repo)\n", path);
		return 0;
	}
	fclose(f);
	if (hdr[0] != 'B' || hdr[1] != 'I' || hdr[2] != 'K') {
		printf("FAIL %s is not a Bink file\n", path);
		return 1;
	}
	/* Bink file header: frames at 8, width at 20, height at 24, fps at 28/32. */
	fileFrames  = rd32(hdr + 8);
	fileWidth   = rd32(hdr + 20);
	fileHeight  = rd32(hdr + 24);
	fileRate    = rd32(hdr + 28);
	fileRateDiv = rd32(hdr + 32);

	/* An absolute path, because a relative one still goes through the DLL search
	   order - and that starts at this exe's own directory, where the build puts
	   the no-op stub also called binkw32.dll. */
	if (!GetFullPathNameA("BINKW32.DLL", sizeof dllPath, dllPath, NULL)) return 1;
	dll = LoadLibraryA(dllPath);
	if (!dll) {
		printf("skip: no %s\n", dllPath);
		return 0;
	}
	pOpen          = (void *)GetProcAddress(dll, "_BinkOpen@8");
	pClose         = (void *)GetProcAddress(dll, "_BinkClose@4");
	pDoFrame       = (void *)GetProcAddress(dll, "_BinkDoFrame@4");
	pNextFrame     = (void *)GetProcAddress(dll, "_BinkNextFrame@4");
	pCopy          = (void *)GetProcAddress(dll, "_BinkCopyToBuffer@28");
	pSetSoundTrack = (void *)GetProcAddress(dll, "_BinkSetSoundTrack@8");
	pGetError      = (void *)GetProcAddress(dll, "_BinkGetError@0");
	if (!pOpen || !pClose || !pDoFrame || !pNextFrame || !pCopy || !pSetSoundTrack) {
		printf("FAIL BINKW32.DLL does not export the decorated names bink.def lists\n");
		return 1;
	}

	pSetSoundTrack(0, 0);				/* decode silently, no audio device needed */
	bnk = pOpen(path, BINKPRELOADALL);
	if (!bnk) {
		printf("FAIL BinkOpen(%s) returned NULL: %s\n", path,
		       pGetError ? pGetError() : "(no BinkGetError)");
		return 1;
	}

	CHECK_EQ(bnk->Width, fileWidth, "BINK.Width");
	CHECK_EQ(bnk->Height, fileHeight, "BINK.Height");
	CHECK_EQ(bnk->Frames, fileFrames, "BINK.Frames");
	CHECK_EQ(bnk->FrameNum, 1, "BINK.FrameNum starts at 1");
	CHECK_EQ(bnk->FrameRate, fileRate, "BINK.FrameRate");
	CHECK_EQ(bnk->FrameRateDiv, fileRateDiv, "BINK.FrameRateDiv");

	w = fileWidth;
	h = fileHeight;
	buf32 = (unsigned char *)malloc(w * h * 4);
	buf24 = (unsigned char *)malloc(w * h * 3);

	/* Skip over any black lead-in, else the pixel comparison proves nothing. */
	for (frame = 0; frame < 60 && !nonBlack; ++frame) {
		pDoFrame(bnk);
		memset(buf32, 0xCD, w * h * 4);
		pCopy(bnk, buf32, (int)(w * 4), h, 0, 0, BINKSURFACE32 | BINKCOPYALL);
		for (i = 0; i < w * h * 4; ++i) {
			if (buf32[i] != 0 && buf32[i] != 0xCD) { nonBlack = 1; break; }
		}
		if (!nonBlack && frame + 1 < bnk->Frames) pNextFrame(bnk);
	}
	if (!nonBlack) {
		printf("FAIL first %u frames decoded to nothing but black\n", frame);
		++failures;
	} else {
		memset(buf24, 0xCD, w * h * 3);
		pCopy(bnk, buf24, (int)(w * 3), h, 0, 0, BINKSURFACE24 | BINKCOPYALL);
		for (i = 0; i < w * h; ++i) {
			if (buf32[i * 4 + 0] != buf24[i * 3 + 0] ||
			    buf32[i * 4 + 1] != buf24[i * 3 + 1] ||
			    buf32[i * 4 + 2] != buf24[i * 3 + 2]) {
				printf("FAIL pixel %u differs between BINKSURFACE32 and BINKSURFACE24\n", i);
				++failures;
				break;
			}
		}
	}

	free(buf32);
	free(buf24);
	pClose(bnk);
	FreeLibrary(dll);

	printf("%s: %ux%u, %u frames, %u/%u fps - %d failure(s)\n", path, w, h,
	       fileFrames, fileRate, fileRateDiv, failures);
	return failures;
}
