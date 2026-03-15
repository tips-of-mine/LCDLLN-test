#include "engine/Engine.h"

#include <cstdio>
#include <memory>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static std::unique_ptr<engine::Engine> g_engine;
static int g_result = 1;

int main(int argc, char** argv)
{
#if defined(_WIN32)
	__try
	{
		g_engine = std::make_unique<engine::Engine>(argc, argv);
		g_result = g_engine->Run();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		std::fprintf(stderr, "[MAIN] SEH EXCEPTION code=0x%08X\n",
			(unsigned int)GetExceptionCode());
		std::fflush(stderr);
	}
#else
	g_engine = std::make_unique<engine::Engine>(argc, argv);
	g_result = g_engine->Run();
#endif
	return g_result;
}
