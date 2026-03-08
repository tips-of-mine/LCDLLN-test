#include "engine/Engine.h"
#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    auto e = std::make_unique<engine::Engine>(argc, argv);
    return e->Run();
}