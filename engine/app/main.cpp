#include "engine/Engine.h"
#include <cstdio>
#include <memory>
#include <exception>

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[MAIN] avant Engine()\n");
    std::fflush(stderr);

    try
    {
        auto e = std::make_unique<engine::Engine>(argc, argv);
        std::fprintf(stderr, "[MAIN] Engine cree OK\n");
        std::fflush(stderr);
        return e->Run();
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "[MAIN] EXCEPTION std: %s\n", ex.what());
        std::fflush(stderr);
    }
    catch (...)
    {
        std::fprintf(stderr, "[MAIN] EXCEPTION inconnue\n");
        std::fflush(stderr);
    }
    return 1;
}