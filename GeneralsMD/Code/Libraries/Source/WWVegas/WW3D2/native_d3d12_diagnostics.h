#pragma once

#include <windows.h>
#include <chrono>

namespace NativeD3D12Internal {
void DiagnosticLog(const char* message);
bool CheckHr(HRESULT hr, const char* operation);
	struct CpuTimer {
		double* total;
		std::chrono::steady_clock::time_point start;
		explicit CpuTimer(double* value) : total(value) { if (total) start = std::chrono::steady_clock::now(); }
		~CpuTimer() { if (total) *total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count(); }
	};
}
