#include "engine/Engine.h"
#include <cstdio>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::unique_ptr<engine::Engine> g_engine;
static int g_result = 1;

static void CreateAndRun(int argc, char** argv)
{
    g_engine = std::make_unique<engine::Engine>(argc, argv);
    std::fprintf(stderr, "[MAIN] Engine cree OK\n");
    std::fflush(stderr);
    g_result = g_engine->Run();
}

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[MAIN] avant Engine()\n");
    std::fflush(stderr);

    __try
    {
        CreateAndRun(argc, argv);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        std::fprintf(stderr, "[MAIN] SEH EXCEPTION code=0x%08X\n",
            (unsigned int)GetExceptionCode());
        std::fflush(stderr);
    }
    return g_result;
}