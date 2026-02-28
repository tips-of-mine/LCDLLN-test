/**
 * @file main.cpp
 * @brief Engine entry point — delegates to engine::Engine.
 */

#include "engine/core/Engine.h"

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, const char* const* argv) {
    return engine::Engine::Run(argc, argv);
}
