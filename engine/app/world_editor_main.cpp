#include "engine/Engine.h"

#include "engine/core/Log.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#elif defined(__linux__)
#	include <unistd.h>
#	include <vector>
#endif

namespace
{
	std::unique_ptr<engine::Engine> g_engine;
	int g_result = 1;
	std::vector<std::string> g_arg_storage;

	/// Répertoire contenant l’exécutable (vide si inconnu).
	static std::filesystem::path GetExecutableDirectory()
	{
#if defined(_WIN32)
		wchar_t buf[MAX_PATH];
		const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
		{
			return {};
		}
		return std::filesystem::path(buf).parent_path();
#elif defined(__linux__)
		std::vector<char> buf(512);
		for (;;)
		{
			const ssize_t len = ::readlink("/proc/self/exe", buf.data(), buf.size());
			if (len < 0)
			{
				return {};
			}
			if (static_cast<size_t>(len) < buf.size())
			{
				return std::filesystem::path(std::string_view(buf.data(), static_cast<size_t>(len))).parent_path();
			}
			buf.resize(buf.size() * 2u);
		}
#else
		return {};
#endif
	}

	/// Si le cwd ne contient pas déjà le content, remonte depuis l’exe jusqu’à trouver `config.json` + `game/data/`.
	static void EnsureWorkingDirectoryForContentPaths()
	{
		namespace fs = std::filesystem;
		const fs::path cwd = fs::current_path();
		if (fs::is_regular_file(cwd / "config.json") && fs::is_directory(cwd / "game" / "data"))
		{
			return;
		}

		fs::path probe = GetExecutableDirectory();
		if (probe.empty())
		{
			return;
		}

		for (int depth = 0; depth < 16; ++depth)
		{
			if (fs::is_regular_file(probe / "config.json") && fs::is_directory(probe / "game" / "data"))
			{
				std::error_code ec;
				fs::current_path(probe, ec);
				if (!ec)
				{
					std::fprintf(stdout,
						"[lcdlln_world_editor] Repertoire de travail -> %s (detection config.json + game/data)\n",
						probe.string().c_str());
				}
				return;
			}
			if (!probe.has_parent_path())
			{
				break;
			}
			probe = probe.parent_path();
		}
	}

	void PrintHelp()
	{
		std::fputs(
			"LCDLLN World Editor — outil séparé du client, même moteur Vulkan que le jeu.\n"
			"Les chemins heightmap sont relatifs à paths.content (souvent game/data) : le cwd doit\n"
			"pointer vers la racine du dépôt (config.json + dossier game/data), ou l’exe remonte\n"
			"automatiquement depuis pkg/lcdlln_world_editor/ vers cette racine au démarrage.\n"
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

	/// Hors de \c main() : MSVC C2712 interdit \c __try dans une fonction qui a du déroulement C++ (ex. \c std::vector).
	int RunWorldEditorBody(int argc, char** argv)
	{
		if (WantsHelp(argc, argv))
		{
			PrintHelp();
			return 0;
		}
		EnsureWorkingDirectoryForContentPaths();
		std::vector<char*> av = BuildArgvForEngine(argc, argv);
		const int n = static_cast<int>(av.size());
		return CreateAndRun(n, av.data());
	}
} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
	__try
	{
		g_result = RunWorldEditorBody(argc, argv);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		LOG_ERROR(Core, "[MAIN] SEH EXCEPTION code=0x{:08X}", static_cast<unsigned int>(GetExceptionCode()));
	}
#else
	g_result = RunWorldEditorBody(argc, argv);
#endif
	return g_result;
}
