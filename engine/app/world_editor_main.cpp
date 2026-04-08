#include "engine/app/WorldEditorApp.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <unistd.h>
#endif

namespace
{
	std::ofstream g_logFile;
	bool g_logToFile = false;

	std::string UtcTimestamp()
	{
		using clock = std::chrono::system_clock;
		const auto t = clock::now();
		const std::time_t tt = clock::to_time_t(t);
		std::tm buf{};
#if defined(_WIN32)
		if (gmtime_s(&buf, &tt) != 0)
		{
			return "?";
		}
#else
		if (gmtime_r(&tt, &buf) == nullptr)
		{
			return "?";
		}
#endif
		std::ostringstream os;
		os << std::put_time(&buf, "%Y-%m-%dT%H:%M:%SZ");
		return os.str();
	}

	void PrintLine(const char* line)
	{
		std::fputs(line, stdout);
		std::fflush(stdout);
		if (g_logToFile && g_logFile.is_open())
		{
			g_logFile << line;
			g_logFile.flush();
		}
	}

	void Log(const std::string& message)
	{
		const std::string line = "[" + UtcTimestamp() + "] " + message + "\n";
		PrintLine(line.c_str());
	}

	std::filesystem::path DefaultLogPath()
	{
#if defined(_WIN32)
		wchar_t wbuf[MAX_PATH]{};
		const DWORD n = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
		{
			return std::filesystem::path("world_editor.log");
		}
		std::filesystem::path exe(wbuf);
		return exe.parent_path() / "world_editor.log";
#else
		char self[4096]{};
		const ssize_t r = readlink("/proc/self/exe", self, sizeof(self) - 1);
		if (r > 0)
		{
			self[r] = '\0';
			return std::filesystem::path(self).parent_path() / "world_editor.log";
		}
		return std::filesystem::current_path() / "world_editor.log";
#endif
	}

	bool StartsWith(const char* s, const char* prefix)
	{
		return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
	}

	std::filesystem::path ParseArgs(int argc, char** argv, bool& outHelp)
	{
		outHelp = false;
		std::filesystem::path logPath;
		bool wantLog = false;
		for (int i = 1; i < argc; ++i)
		{
			const char* a = argv[i];
			if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0)
			{
				outHelp = true;
				continue;
			}
			if (std::strcmp(a, "-log") == 0 || std::strcmp(a, "--log") == 0)
			{
				wantLog = true;
				if (i + 1 < argc && argv[i + 1][0] != '-')
				{
					logPath = argv[i + 1];
					++i;
				}
				continue;
			}
			if (StartsWith(a, "-log=") || StartsWith(a, "--log-file="))
			{
				wantLog = true;
				const char* eq = std::strchr(a, '=');
				if (eq && eq[1] != '\0')
				{
					logPath = eq + 1;
				}
				continue;
			}
		}
		if (!wantLog)
		{
			return {};
		}
		if (logPath.empty())
		{
			logPath = DefaultLogPath();
		}
		return logPath;
	}

	void LogStartupContext(int argc, char** argv)
	{
		Log("LCDLLN World Editor — démarrage (GLFW + OpenGL + ImGui)");
		try
		{
			Log(std::string("cwd: ") + std::filesystem::current_path().string());
		}
		catch (const std::exception& e)
		{
			Log(std::string("cwd: (erreur) ") + e.what());
		}
		catch (...)
		{
			Log("cwd: (erreur inconnue)");
		}
#if defined(_WIN32)
		wchar_t wbuf[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0)
		{
			std::filesystem::path exe(wbuf);
			Log(std::string("exe: ") + exe.string());
		}
		else
		{
			Log("exe: (GetModuleFileNameW a échoué)");
		}
#else
		char self[4096]{};
		const ssize_t r = readlink("/proc/self/exe", self, sizeof(self) - 1);
		if (r > 0)
		{
			self[r] = '\0';
			Log(std::string("exe: ") + self);
		}
		else if (argc > 0)
		{
			Log(std::string("exe (argv[0]): ") + argv[0]);
		}
#endif
		std::ostringstream args;
		args << "argc=" << argc << " argv:";
		for (int i = 0; i < argc; ++i)
		{
			args << " [" << i << "]=\"" << argv[i] << '"';
		}
		Log(args.str());
	}

	void PrintHelp()
	{
		PrintLine(
			"LCDLLN World Editor\n"
			"Options :\n"
			"  -log [fichier]   Journal diagnostic (défaut : world_editor.log à côté de l'exécutable).\n"
			"                   Détails : GLFW, OpenGL, ImGui (stdout + fichier).\n"
			"  -log=chemin      Idem avec chemin immédiat.\n"
			"  --log-file=...   Idem.\n"
			"  -h, --help       Affiche cette aide.\n");
	}
} // namespace

int main(int argc, char** argv)
{
	bool showHelp = false;
	const std::filesystem::path logPath = ParseArgs(argc, argv, showHelp);

	if (showHelp)
	{
		PrintHelp();
		return 0;
	}

	if (!logPath.empty())
	{
		std::error_code ec;
		std::filesystem::create_directories(logPath.parent_path(), ec);
		g_logFile.open(logPath.string(), std::ios::out | std::ios::app);
		if (g_logFile.is_open())
		{
			g_logToFile = true;
			Log(std::string("Journal fichier : ") + logPath.string());
		}
		else
		{
			std::fprintf(stderr,
				"[world_editor] Impossible d'ouvrir le journal : %s\n",
				logPath.string().c_str());
		}
	}

	if (g_logToFile)
	{
		LogStartupContext(argc, argv);
	}
	else
	{
		std::printf("LCDLLN World Editor — fenêtre graphique (ImGui). Option -log pour journal fichier + traces détaillées.\n");
	}

	engine::world_editor::WorldEditorRunOptions opts;
	if (g_logToFile)
	{
		opts.log = [](std::string_view sv) { Log(std::string(sv)); };
	}

	const int code = engine::world_editor::RunWorldEditor(opts);

	if (g_logToFile)
	{
		Log(std::string("Fin, code=") + std::to_string(code));
	}
	else if (code != 0)
	{
		std::fprintf(stderr, "[world_editor] Sortie avec erreur (code=%i). Relancez avec -log pour diagnostic.\n", code);
	}

	g_logFile.close();
	return code;
}
