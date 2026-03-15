#include "engine/Engine.h"

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

int main(int argc, char** argv)
{
#if defined(_WIN32)
	__try
	{
		g_result = CreateAndRun(argc, argv);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		std::fprintf(stderr, "[MAIN] SEH EXCEPTION code=0x%08X\n",
			(unsigned int)GetExceptionCode());
		std::fflush(stderr);
	}
#else
	g_result = CreateAndRun(argc, argv);
#endif
	return g_result;
}
