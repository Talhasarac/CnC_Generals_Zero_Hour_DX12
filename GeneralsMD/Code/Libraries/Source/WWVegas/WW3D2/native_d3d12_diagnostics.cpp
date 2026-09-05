#include "native_d3d12_diagnostics.h"
#include <cstdio>

namespace NativeD3D12Internal {
	void DiagnosticLog(const char* message)
	{
		if (!GetEnvironmentVariableA("GENERALS_D3D12_DIAGNOSTICS", nullptr, 0) &&
			!GetEnvironmentVariableA("GENERALS_D3D12_PROFILE", nullptr, 0)) return;
		FILE* file = nullptr;
		if (fopen_s(&file, "NativeD3D12.log", "a") == 0 && file) {
			std::fputs(message, file);
			std::fclose(file);
		}
	}
	void LogD3D12Failure(const char* operation, HRESULT hr)
	{
		char buffer[256] = {};
		std::snprintf(buffer, sizeof(buffer), "NativeD3D12: %s failed (0x%08lX)\n",
			operation, static_cast<unsigned long>(hr));
		OutputDebugStringA(buffer);
		DiagnosticLog(buffer);
	}


bool CheckHr(HRESULT hr, const char* operation)
{
	if (SUCCEEDED(hr)) return true;
	LogD3D12Failure(operation, hr);
	return false;
}
}
