#include "engine/Engine.h"
#include <cstdio>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[MAIN] avant Engine()\n");
    std::fflush(stderr);

    __try
    {
        auto e = std::make_unique<engine::Engine>(argc, argv);
        std::fprintf(stderr, "[MAIN] Engine cree OK\n");
        std::fflush(stderr);
        return e->Run();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        std::fprintf(stderr, "[MAIN] SEH EXCEPTION code=0x%08X\n",
            (unsigned int)GetExceptionCode());
        std::fflush(stderr);

        FILE* f = std::fopen("C:/temp/crash_code.txt", "w");
        if (f)
        {
            std::fprintf(f, "SEH exception code: 0x%08X\n",
                (unsigned int)GetExceptionCode());
            std::fclose(f);
        }
    }
    return 1;
}