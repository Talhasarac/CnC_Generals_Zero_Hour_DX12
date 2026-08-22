// Phase 1 checkpoint: prove the vendored DX8 SDK headers/libs and the d3d8.dll
// we end up with actually work - create a windowed device on a hidden window,
// clear, present.  Prints adapter info and which runtime the device landed on;
// "DX8 smoke OK" means pass.  The build copies our d3d8to9 d3d8.dll next to the
// exe, so the CTest runs go through it like the game does: plain (d3d8to9's
// default, Direct3D 9) and with "-d3d12" (the opt-in, Direct3D 9On12 ->
// Direct3D 12); each run requires to have really landed on its runtime.

#include <windows.h>
#include <d3d8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	bool wantD3D12 = false;
	bool show = false;	// "-show": a visible window, cleared purple and presented for 8 s - for a human to check the present path
	bool exact = false;	// "-exact": client area exactly the back buffer size (like the game; overlay/direct-flip eligible)
	bool popup = false;	// "-popup": the game's window style + black background brush
	bool big = false;	// "-big": 1280x768 back buffer instead of 640x480
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d3d12") == 0) wantD3D12 = true;
		if (strcmp(argv[i], "-show") == 0) show = true;
		if (strcmp(argv[i], "-exact") == 0) exact = true;
		if (strcmp(argv[i], "-popup") == 0) popup = true;
		if (strcmp(argv[i], "-big") == 0) big = true;
	}
	bool vsync = false, copy = false, flip = false, bb2 = false;	// present-parameter variants for the human check
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-vsync") == 0) vsync = true;	// D3DPRESENT_INTERVAL_ONE (d3d8 windowed is always IMMEDIATE otherwise)
		if (strcmp(argv[i], "-copy") == 0) copy = true;	// D3DSWAPEFFECT_COPY
		if (strcmp(argv[i], "-flip") == 0) flip = true;	// D3DSWAPEFFECT_FLIP
		if (strcmp(argv[i], "-bb2") == 0) bb2 = true;	// BackBufferCount = 2
	}
	int bbW = big ? 1280 : 640, bbH = big ? 768 : 480, posX = 100, posY = 100;
	for (int i = 1; i + 1 < argc; i++) {	// "-w N -h N": back buffer size, "-x N -y N": window position
		if (strcmp(argv[i], "-w") == 0) bbW = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-h") == 0) bbH = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-x") == 0) posX = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-y") == 0) posY = atoi(argv[i + 1]);
	}

	IDirect3D8 * d3d = Direct3DCreate8(D3D_SDK_VERSION);
	if (!d3d) { printf("FAIL: Direct3DCreate8 returned NULL\n"); return 1; }

	D3DADAPTER_IDENTIFIER8 id;
	if (d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id) == D3D_OK) {
		printf("adapter: %s\n", id.Description);
	}

	WNDCLASSA wc = {0};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "dx8smoke";
	if (popup) wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);	// the game's class (WinMain.cpp)
	RegisterClassA(&wc);
	DWORD style = popup ? (WS_POPUP | WS_DLGFRAME | WS_CAPTION | WS_SYSMENU) : WS_OVERLAPPEDWINDOW;
	if (show) style |= WS_VISIBLE;
	RECT wr = { 0, 0, show ? bbW : 64, show ? bbH : 64 };
	if (exact) AdjustWindowRect(&wr, style, FALSE);	// client == back buffer, as the game sizes its window
	HWND hwnd = CreateWindowA("dx8smoke", "dx8smoke - should be PURPLE", style,
	                          posX, posY, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, wc.hInstance, NULL);
	if (!hwnd) { printf("FAIL: CreateWindow\n"); d3d->Release(); return 1; }

	D3DDISPLAYMODE mode;
	d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

	// the game's own choices (dx8wrapper.cpp): windowed, discard, auto depth, mixed vertex
	// processing - plus a lockable back buffer so the result can be read back and checked
	D3DPRESENT_PARAMETERS pp = {0};
	if (show) { pp.BackBufferWidth = bbW; pp.BackBufferHeight = bbH; }
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = mode.Format;
	pp.hDeviceWindow = hwnd;
	pp.EnableAutoDepthStencil = TRUE;
	pp.AutoDepthStencilFormat = D3DFMT_D16;
	pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	// d3d8to9 makes every windowed present IMMEDIATE; only D3DSWAPEFFECT_COPY_VSYNC reaches D3D9 as COPY + INTERVAL_ONE
	if (vsync) pp.SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;
	if (copy) pp.SwapEffect = D3DSWAPEFFECT_COPY;
	if (flip) pp.SwapEffect = D3DSWAPEFFECT_FLIP;
	if (bb2) pp.BackBufferCount = 2;

	IDirect3DDevice8 * dev = NULL;
	HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
	                               D3DCREATE_MIXED_VERTEXPROCESSING, &pp, &dev);
	if (hr != D3D_OK || !dev) {
		printf("FAIL: CreateDevice hr=0x%08lx\n", (unsigned long)hr);
		d3d->Release();
		return 1;
	}

	dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(64, 0, 128), 1.0f, 0);

	// read the cleared back buffer back: proves the runtime really rendered, not just created
	// (a D3D12 path that only returned a device but painted nothing would pass otherwise)
	{
		IDirect3DSurface8 * bb = NULL;
		if (dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb) == D3D_OK && bb) {
			D3DLOCKED_RECT lr;
			if (bb->LockRect(&lr, NULL, D3DLOCK_READONLY) == D3D_OK) {
				const unsigned char * px = (const unsigned char *)lr.pBits + 10 * lr.Pitch + 10 * 4;
				printf("back buffer pixel (b,g,r) = (%u,%u,%u)\n", px[0], px[1], px[2]);
				bool ok = px[2] > 48 && px[2] < 80 && px[1] < 16 && px[0] > 112 && px[0] < 144;
				bb->UnlockRect();
				if (!ok) {
					printf("FAIL: back buffer does not hold the clear colour (64,0,128)\n");
					bb->Release(); dev->Release(); d3d->Release(); DestroyWindow(hwnd);
					return 1;
				}
			} else {
				printf("(back buffer not lockable on this runtime - pixel check skipped)\n");
			}
			bb->Release();
		}
	}
	HRESULT phr = dev->Present(NULL, NULL, NULL, NULL);
	printf("Present hr=0x%08lx\n", (unsigned long)phr);
	if (show) {
		// keep clearing + presenting for 8 s while pumping messages, so a human can look
		const DWORD until = GetTickCount() + 8000;
		while (GetTickCount() < until) {
			MSG msg;
			while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
			dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(160, 0, 200), 1.0f, 0);
			dev->Present(NULL, NULL, NULL, NULL);
			Sleep(16);
		}
	}

	// which runtime the d3d8.dll we ended up with put the device on: the system d3d8.dll
	// (Direct3D 8), our d3d8to9 (Direct3D 9), or its "-d3d12" / D3D8TO9_D3D12 opt-in
	// (Direct3D 9On12 -> Direct3D 12).  Checked while the device is alive: d3d9 unloads
	// d3d9on12.dll/d3d12.dll again once the last object is released.
	printf("runtime: %s\n", GetModuleHandleA("d3d9on12.dll") ? "Direct3D 12 (9On12)"
	                      : GetModuleHandleA("d3d9.dll")     ? "Direct3D 9"
	                                                         : "Direct3D 8");
	printf("loaded: d3d9=%d d3d9on12=%d d3d12=%d D3D12Core=%d\n",
	       GetModuleHandleA("d3d9.dll") != NULL, GetModuleHandleA("d3d9on12.dll") != NULL,
	       GetModuleHandleA("d3d12.dll") != NULL, GetModuleHandleA("D3D12Core.dll") != NULL);
	const bool onD3D12 = GetModuleHandleA("d3d9on12.dll") != NULL && GetModuleHandleA("d3d12.dll") != NULL;
	if (wantD3D12 ? !onD3D12 : onD3D12) {
		printf(wantD3D12 ? "FAIL: -d3d12 requested but the device is not on Direct3D 12 (d3d8to9 fell back, or this is not our d3d8.dll)\n"
		                 : "FAIL: the device is on Direct3D 9On12 without -d3d12\n");
		dev->Release();
		d3d->Release();
		DestroyWindow(hwnd);
		return 1;
	}

	dev->Release();
	d3d->Release();
	DestroyWindow(hwnd);
	printf("DX8 smoke OK\n");
	return 0;
}
