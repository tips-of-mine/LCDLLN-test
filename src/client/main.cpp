#include "src/client/app/Engine.h"

#include <cstdint>
#include <cstdio>
#include <memory>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static std::unique_ptr<engine::Engine> g_engine;
static int g_result = 1;

/// Runs the engine; returns exit code. Isolated so that main() can use __try/__except
/// without triggering MSVC C2712 (no C++ object unwinding in the same function as __try).
static int CreateAndRun(int argc, char** argv)
{
	g_engine = std::make_unique<engine::Engine>(argc, argv);
	return g_engine->Run();
}

#if defined(_WIN32)
/// Filtre SEH : journalise l'adresse fautive (module + décalage, utilisable avec
/// le .pdb du build) et, pour une violation d'accès, l'adresse visée et le type
/// d'accès. Appelé dans le filtre __except (pile encore intacte).
static int LogSehDetails(EXCEPTION_POINTERS* info)
{
	if (info == nullptr || info->ExceptionRecord == nullptr)
		return EXCEPTION_EXECUTE_HANDLER;
	const EXCEPTION_RECORD& rec = *info->ExceptionRecord;
	const auto addr = reinterpret_cast<uintptr_t>(rec.ExceptionAddress);
	HMODULE mod = nullptr;
	char modName[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(rec.ExceptionAddress), &mod) && mod != nullptr)
		GetModuleFileNameA(mod, modName, MAX_PATH);
	const uintptr_t rva = mod != nullptr ? addr - reinterpret_cast<uintptr_t>(mod) : addr;
	if (rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec.NumberParameters >= 2)
		LOG_ERROR(Core, "[MAIN] SEH code=0x{:08X} a {} +0x{:X} : acces {} a l'adresse 0x{:X}",
			static_cast<unsigned int>(rec.ExceptionCode), modName, rva,
			rec.ExceptionInformation[0] == 0 ? "lecture" : (rec.ExceptionInformation[0] == 1 ? "ecriture" : "execution"),
			static_cast<uintptr_t>(rec.ExceptionInformation[1]));
	else
		LOG_ERROR(Core, "[MAIN] SEH code=0x{:08X} a {} +0x{:X}",
			static_cast<unsigned int>(rec.ExceptionCode), modName, rva);
	// Pile d'appels (adresses module + décalage).
	void* frames[32] = {};
	const USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
	for (USHORT i = 0; i < n; ++i)
	{
		HMODULE fm = nullptr;
		char fn[MAX_PATH] = "?";
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(frames[i]), &fm) && fm != nullptr)
			GetModuleFileNameA(fm, fn, MAX_PATH);
		const auto fa = reinterpret_cast<uintptr_t>(frames[i]);
		LOG_ERROR(Core, "[MAIN]   #{} {} +0x{:X}", i, fn, fm != nullptr ? fa - reinterpret_cast<uintptr_t>(fm) : fa);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv)
{
#if defined(_WIN32)
	__try
	{
		g_result = CreateAndRun(argc, argv);
	}
	__except(LogSehDetails(GetExceptionInformation()))
	{
LOG_ERROR(Core, "[MAIN] SEH EXCEPTION code=0x{:08X}", static_cast<unsigned int>(GetExceptionCode()));
	}
#else
	g_result = CreateAndRun(argc, argv);
#endif
	return g_result;
}
