#include "engine/Engine.h"
#include <cstdio>

int main(int argc, char** argv)
{
    // TEST 1 : est-ce que main() est atteint ?
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    // TEST 2 : écriture directe sur disque depuis main()
    FILE* f = std::fopen("C:/temp/test_main.txt", "w");
    if (f) { std::fprintf(f, "main atteint\n"); std::fclose(f); }

    // TEST 3 : le constructeur Engine
    std::fprintf(stderr, "[MAIN] avant Engine()\n");
    std::fflush(stderr);

    engine::Engine e(argc, argv);
    return e.Run();
}