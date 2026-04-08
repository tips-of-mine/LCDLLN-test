#include "engine/Engine.h"

#include "engine/core/Log.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace
{
	std::unique_ptr<engine::Engine> g_engine;
	int g_result = 1;
	std::vector<std::string> g_arg_storage;

	void PrintHelp()
	{
		std::fputs(
			"LCDLLN World Editor — outil séparé du client, même moteur Vulkan que le jeu.\n"
			"Options :\n"
			"  -log [fichier]   Journal fichier (horodaté ; voir aussi log.* dans config.json).\n"
			"  -console        Logs sur la console.\n"
			"  -h, --help       Affiche cette aide.\n",
			stdout);
	}

	bool WantsHelp(int argc, char** argv)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (!argv[i])
			{
				continue;
			}
			if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
			{
				return true;
			}
		}
		return false;
	}

	std::vector<char*> BuildArgvForEngine(int argc, char** argv)
	{
		g_arg_storage.clear();
		g_arg_storage.reserve(static_cast<size_t>(argc) + 2u);
		g_arg_storage.emplace_back(argc > 0 && argv[0] ? argv[0] : "lcdlln_world_editor");
		g_arg_storage.emplace_back("--world-editor");
		for (int i = 1; i < argc; ++i)
		{
			if (argv[i])
			{
				g_arg_storage.emplace_back(argv[i]);
			}
		}
		std::vector<char*> out;
		out.reserve(g_arg_storage.size());
		for (auto& s : g_arg_storage)
		{
			out.push_back(s.data());
		}
		return out;
	}

	int CreateAndRun(int argc, char** argv)
	{
		g_engine = std::make_unique<engine::Engine>(argc, argv);
		return g_engine->Run();
	}
} // namespace

int main(int argc, char** argv)
{
	if (WantsHelp(argc, argv))
	{
		PrintHelp();
		return 0;
	}

	std::vector<char*> av = BuildArgvForEngine(argc, argv);
	const int n = static_cast<int>(av.size());

#if defined(_WIN32)
	__try
	{
		g_result = CreateAndRun(n, av.data());
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		LOG_ERROR(Core, "[MAIN] SEH EXCEPTION code=0x{:08X}", static_cast<unsigned int>(GetExceptionCode()));
	}
#else
	g_result = CreateAndRun(n, av.data());
#endif
	return g_result;
}
