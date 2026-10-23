#include "src/client/app/Engine.h"

#include <cstdint>
#include <cstdio>
#include <memory>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
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
/// Décrit une adresse de code : module + décalage et, si le .pdb est présent à
/// côté de l'exe, fonction + fichier:ligne (dbghelp). \p symOk = SymInitialize réussi.
static void DescribeCodeAddress(uintptr_t addr, bool symOk, char* out, size_t outSize)
{
	HMODULE mod = nullptr;
	char modPath[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(addr), &mod) && mod != nullptr)
		GetModuleFileNameA(mod, modPath, MAX_PATH);
	const char* modName = modPath;
	for (const char* c = modPath; *c != '\0'; ++c)
		if (*c == '\\' || *c == '/')
			modName = c + 1;
	const uintptr_t rva = mod != nullptr ? addr - reinterpret_cast<uintptr_t>(mod) : addr;
	int len = std::snprintf(out, outSize, "%s +0x%llX", modName, static_cast<unsigned long long>(rva));
	if (!symOk || len < 0 || static_cast<size_t>(len) >= outSize)
		return;
	alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
	auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = 255;
	DWORD64 disp = 0;
	if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym))
	{
		const int n = std::snprintf(out + len, outSize - static_cast<size_t>(len), " %s+0x%llX",
			sym->Name, static_cast<unsigned long long>(disp));
		if (n > 0 && static_cast<size_t>(len + n) < outSize)
			len += n;
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(line);
		DWORD lineDisp = 0;
		if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &lineDisp, &line) && line.FileName != nullptr)
			std::snprintf(out + len, outSize - static_cast<size_t>(len), " (%s:%lu)",
				line.FileName, static_cast<unsigned long>(line.LineNumber));
	}
}

/// Filtre SEH : journalise l'adresse fautive (module + décalage, et fonction +
/// ligne si le .pdb est livré), pour une violation d'accès l'adresse visée et le
/// type d'accès, puis la pile d'appels. Appelé dans le filtre __except : la pile
/// fautive est encore intacte (le filtre s'exécute au-dessus d'elle).
static int LogSehDetails(EXCEPTION_POINTERS* info)
{
	if (info == nullptr || info->ExceptionRecord == nullptr)
		return EXCEPTION_EXECUTE_HANDLER;
	const EXCEPTION_RECORD& rec = *info->ExceptionRecord;
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	// Chemin des symboles = dossier de l'exe (le .pdb y est livré), quel que soit
	// le dossier courant.
	char exeDir[MAX_PATH] = {};
	const DWORD exeLen = GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
	for (DWORD i = exeLen; i > 0; --i)
		if (exeDir[i - 1] == '\\' || exeDir[i - 1] == '/')
		{
			exeDir[i - 1] = '\0';
			break;
		}
	const bool symOk = SymInitialize(GetCurrentProcess(), exeLen > 0 ? exeDir : nullptr, TRUE) != FALSE;
	char where[1024] = {};
	DescribeCodeAddress(reinterpret_cast<uintptr_t>(rec.ExceptionAddress), symOk, where, sizeof(where));
	if (rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec.NumberParameters >= 2)
		LOG_ERROR(Core, "[MAIN] SEH code=0x{:08X} a {} : acces {} a l'adresse 0x{:X}",
			static_cast<unsigned int>(rec.ExceptionCode), where,
			rec.ExceptionInformation[0] == 0 ? "lecture" : (rec.ExceptionInformation[0] == 1 ? "ecriture" : "execution"),
			static_cast<uintptr_t>(rec.ExceptionInformation[1]));
	else
		LOG_ERROR(Core, "[MAIN] SEH code=0x{:08X} a {}", static_cast<unsigned int>(rec.ExceptionCode), where);
	// Pile d'appels de la frame FAUTIVE : déroulée depuis le contexte de
	// l'exception (données de déroulage x64 de l'image, .pdb non requis).
#if defined(_M_X64)
	if (symOk && info->ContextRecord != nullptr)
	{
		CONTEXT ctx = *info->ContextRecord;
		STACKFRAME64 sf = {};
		sf.AddrPC.Offset = ctx.Rip;
		sf.AddrPC.Mode = AddrModeFlat;
		sf.AddrFrame.Offset = ctx.Rbp;
		sf.AddrFrame.Mode = AddrModeFlat;
		sf.AddrStack.Offset = ctx.Rsp;
		sf.AddrStack.Mode = AddrModeFlat;
		for (int i = 0; i < 48; ++i)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, &ctx,
					nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
				|| sf.AddrPC.Offset == 0)
				break;
			DescribeCodeAddress(static_cast<uintptr_t>(sf.AddrPC.Offset), symOk, where, sizeof(where));
			LOG_ERROR(Core, "[MAIN]   #{} {}", i, where);
		}
	}
	else
#endif
	{
		// Repli : pile du filtre (le répartiteur d'exceptions puis la frame fautive).
		void* frames[48] = {};
		const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
		for (USHORT i = 0; i < n; ++i)
		{
			DescribeCodeAddress(reinterpret_cast<uintptr_t>(frames[i]), symOk, where, sizeof(where));
			LOG_ERROR(Core, "[MAIN]   #{} {}", i, where);
		}
	}
	if (symOk)
		SymCleanup(GetCurrentProcess());
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
