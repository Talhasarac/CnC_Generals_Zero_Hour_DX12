// Phase 1 checkpoint: prove the vendored DX8 SDK headers/libs and the OS's
// d3d8.dll actually work - create a windowed device on a hidden window, clear,
// present.  Prints adapter info; "DX8 smoke OK" means pass.

#include <windows.h>
#include <d3d8.h>
#include <stdio.h>

int main(void)
{
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
	RegisterClassA(&wc);
	HWND hwnd = CreateWindowA("dx8smoke", "dx8smoke", WS_OVERLAPPEDWINDOW,
	                          0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
	if (!hwnd) { printf("FAIL: CreateWindow\n"); d3d->Release(); return 1; }

	D3DDISPLAYMODE mode;
	d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

	D3DPRESENT_PARAMETERS pp = {0};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = mode.Format;
	pp.hDeviceWindow = hwnd;

	IDirect3DDevice8 * dev = NULL;
	HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
	                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
	if (hr != D3D_OK || !dev) {
		printf("FAIL: CreateDevice hr=0x%08lx\n", (unsigned long)hr);
		d3d->Release();
		return 1;
	}

	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(64, 0, 128), 1.0f, 0);
	dev->Present(NULL, NULL, NULL, NULL);

	dev->Release();
	d3d->Release();
	DestroyWindow(hwnd);
	printf("DX8 smoke OK\n");
	return 0;
}
