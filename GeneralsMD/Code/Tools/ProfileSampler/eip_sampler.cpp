// eip_sampler - a 1 kHz sampling profiler for a running 32-bit generals.exe.
//
// There is no debugger on this machine, so this is how hotspots get names:
// suspend the target's main thread, read EIP out of its CONTEXT, resume, and
// tally the addresses.  At the end dbghelp turns the tallies into symbols.
//
//   eip_sampler generals.exe 30      sample the main thread for 30 seconds
//   eip_sampler 12345 10 -all        sample every thread of PID 12345
//
// Symbolization runs against the *live* process (SymInitialize with fInvade),
// not against the linker map: the .map's Publics do not list statics, and a
// sampler that cannot name statics lands most of its samples on whatever $R
// symbol happens to precede them.  Keep the PDB next to the exe.

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace
{

// Toolhelp is the only way to walk another process' threads without reaching
// into ntdll internals.  The snapshot is a point-in-time list; threads created
// after it is taken are simply not sampled.
struct ThreadRef
{
	DWORD id;
	ULONGLONG created;   // FILETIME as an integer; the earliest one is main()
};

DWORD find_process(const char *spec)
{
	// A pure number is a PID, anything else is an image name.
	char *end = nullptr;
	const unsigned long as_pid = strtoul(spec, &end, 10);
	if (end != spec && *end == 0) {
		return static_cast<DWORD>(as_pid);
	}

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32 pe = {sizeof(pe)};
	DWORD found = 0;
	for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
		if (_stricmp(pe.szExeFile, spec) == 0) {
			found = pe.th32ProcessID;
			break;
		}
	}
	CloseHandle(snap);
	return found;
}

std::vector<ThreadRef> list_threads(DWORD pid)
{
	std::vector<ThreadRef> threads;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return threads;
	}

	THREADENTRY32 te = {sizeof(te)};
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid) {
			continue;
		}
		HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (th == nullptr) {
			continue;
		}
		FILETIME created = {}, exited = {}, kernel = {}, user = {};
		if (GetThreadTimes(th, &created, &exited, &kernel, &user)) {
			ULARGE_INTEGER when;
			when.LowPart = created.dwLowDateTime;
			when.HighPart = created.dwHighDateTime;
			threads.push_back({te.th32ThreadID, when.QuadPart});
		}
		CloseHandle(th);
	}
	CloseHandle(snap);

	std::sort(threads.begin(), threads.end(),
		[](const ThreadRef &a, const ThreadRef &b) { return a.created < b.created; });
	return threads;
}

// Samples are bucketed by symbol rather than by address, so a hot loop spread
// over a hundred instructions reads as one row instead of a hundred.
struct Bucket
{
	std::string name;
	std::string file;
	unsigned line;
	unsigned long long samples;
};

std::string module_of(HANDLE proc, DWORD64 addr)
{
	IMAGEHLP_MODULE64 mi = {sizeof(mi)};
	if (!SymGetModuleInfo64(proc, addr, &mi)) {
		return "?";
	}
	return mi.ModuleName;
}

void report(HANDLE proc, const std::map<DWORD, unsigned long long> &hits,
	unsigned long long total, const char *title)
{
	std::map<std::string, Bucket> by_symbol;
	for (const auto &hit : hits) {
		const DWORD64 addr = hit.first;

		// SYMBOL_INFO carries its name inline past the end of the struct, so it
		// has to be over-allocated.  MaxNameLen counts characters, not bytes.
		char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
		SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(storage);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;

		std::string key;
		DWORD64 displacement = 0;
		if (SymFromAddr(proc, addr, &displacement, sym)) {
			key = sym->Name;
		} else {
			char raw[96];
			sprintf_s(raw, "%s!0x%08lx", module_of(proc, addr).c_str(),
				static_cast<unsigned long>(addr));
			key = raw;
		}

		Bucket &bucket = by_symbol[key];
		if (bucket.samples == 0) {
			bucket.name = key;
			bucket.line = 0;
			IMAGEHLP_LINE64 li = {sizeof(li)};
			DWORD line_displacement = 0;
			if (SymGetLineFromAddr64(proc, addr, &line_displacement, &li)) {
				bucket.file = li.FileName;
				bucket.line = li.LineNumber;
			}
		}
		bucket.samples += hit.second;
	}

	std::vector<Bucket> ranked;
	ranked.reserve(by_symbol.size());
	for (const auto &entry : by_symbol) {
		ranked.push_back(entry.second);
	}
	std::sort(ranked.begin(), ranked.end(),
		[](const Bucket &a, const Bucket &b) { return a.samples > b.samples; });

	printf("\n%s - %llu samples, %zu distinct addresses, %zu symbols\n",
		title, total, hits.size(), by_symbol.size());
	printf("%7s  %6s  %s\n", "samples", "pct", "symbol");
	const size_t shown = ranked.size() < 40 ? ranked.size() : 40;
	for (size_t i = 0; i < shown; ++i) {
		const Bucket &b = ranked[i];
		printf("%7llu  %5.1f%%  %s", b.samples, 100.0 * b.samples / total, b.name.c_str());
		if (b.line != 0) {
			// The path is long and only its tail is useful here.
			const size_t slash = b.file.find_last_of("\\/");
			const char *leaf = slash == std::string::npos
				? b.file.c_str() : b.file.c_str() + slash + 1;
			printf("   [%s:%u]", leaf, b.line);
		}
		printf("\n");
	}
	if (ranked.size() > shown) {
		printf("        ... %zu more symbols\n", ranked.size() - shown);
	}
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr,
			"usage: %s <exe-name|pid> <seconds> [-all]\n"
			"       -all samples every thread, not just main\n", argv[0]);
		return 2;
	}

	const DWORD pid = find_process(argv[1]);
	if (pid == 0) {
		fprintf(stderr, "eip_sampler: no process matching '%s'\n", argv[1]);
		return 1;
	}
	const double seconds = atof(argv[2]);
	const bool all_threads = argc > 3 && _stricmp(argv[3], "-all") == 0;

	HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (proc == nullptr) {
		fprintf(stderr, "eip_sampler: OpenProcess(%lu) failed, error %lu "
			"(run elevated?)\n", pid, GetLastError());
		return 1;
	}

	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (!SymInitialize(proc, nullptr, TRUE)) {
		fprintf(stderr, "eip_sampler: SymInitialize failed, error %lu\n", GetLastError());
		CloseHandle(proc);
		return 1;
	}

	std::vector<ThreadRef> threads = list_threads(pid);
	if (threads.empty()) {
		fprintf(stderr, "eip_sampler: pid %lu has no readable threads\n", pid);
		SymCleanup(proc);
		CloseHandle(proc);
		return 1;
	}
	if (!all_threads) {
		threads.resize(1);   // earliest creation time == the main thread
	}

	// Open every thread up front: an OpenThread per sample would cost more than
	// the sample it pays for.
	std::vector<HANDLE> handles;
	std::vector<DWORD> ids;
	for (const ThreadRef &t : threads) {
		HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, t.id);
		if (th != nullptr) {
			handles.push_back(th);
			ids.push_back(t.id);
		}
	}
	if (handles.empty()) {
		fprintf(stderr, "eip_sampler: could not open any thread of pid %lu\n", pid);
		SymCleanup(proc);
		CloseHandle(proc);
		return 1;
	}

	printf("eip_sampler: pid %lu, %zu thread(s), %.1fs at ~1 kHz\n",
		pid, handles.size(), seconds);

	// The default 15.6 ms scheduler tick would stretch Sleep(1) to ~16 ms and
	// turn "1 kHz" into 64 Hz.
	timeBeginPeriod(1);

	std::vector<std::map<DWORD, unsigned long long>> hits(handles.size());
	std::vector<unsigned long long> totals(handles.size(), 0);
	unsigned long long missed = 0;

	LARGE_INTEGER freq = {}, start = {}, now = {};
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
	const LONGLONG deadline = start.QuadPart + static_cast<LONGLONG>(seconds * freq.QuadPart);

	for (;;) {
		QueryPerformanceCounter(&now);
		if (now.QuadPart >= deadline) {
			break;
		}

		for (size_t i = 0; i < handles.size(); ++i) {
			if (SuspendThread(handles[i]) == static_cast<DWORD>(-1)) {
				++missed;
				continue;
			}
			CONTEXT ctx = {};
			ctx.ContextFlags = CONTEXT_CONTROL;
			if (GetThreadContext(handles[i], &ctx)) {
				++hits[i][ctx.Eip];
				++totals[i];
			} else {
				++missed;
			}
			ResumeThread(handles[i]);
		}

		Sleep(1);
	}

	QueryPerformanceCounter(&now);
	const double elapsed = double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
	timeEndPeriod(1);

	unsigned long long grand_total = 0;
	for (unsigned long long t : totals) {
		grand_total += t;
	}
	printf("done: %llu samples over %.2fs (%.0f Hz per thread), %llu missed\n",
		grand_total, elapsed, grand_total / elapsed / handles.size(), missed);

	for (size_t i = 0; i < handles.size(); ++i) {
		if (totals[i] != 0) {
			char title[64];
			sprintf_s(title, "thread %lu%s", ids[i], i == 0 ? " (main)" : "");
			report(proc, hits[i], totals[i], title);
		}
		CloseHandle(handles[i]);
	}

	SymCleanup(proc);
	CloseHandle(proc);
	return 0;
}
