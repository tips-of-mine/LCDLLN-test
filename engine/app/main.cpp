#include "engine/Engine.h"

int main(int argc, char** argv)
{
	// main minimal: boot + run.
	// Logging is initialized by the engine boot sequence (via config).
	// The loop is owned by `engine::Engine::Run()`.
	engine::Engine e(argc, argv);
	return e.Run();
}

