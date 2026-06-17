/// Tests CPU du chargement de la config serveur dédiée (PR1 — éclatement config.json).
/// Vérifie que LoadServerConfig fusionne les clés serveur (db/accounts/chat) dans le
/// Config existant, et renvoie false proprement si le fichier est absent.
#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::core::Config;

	std::filesystem::path TempDir()
	{
		std::random_device rd; std::mt19937_64 rng(rd());
		auto d = std::filesystem::temp_directory_path() / ("lcdlln_srvcfg_" + std::to_string(rng()));
		std::filesystem::create_directories(d);
		return d;
	}

	void Test_LoadsServerKeys()
	{
		const auto dir = TempDir();
		const auto file = dir / "server.config.json";
		{
			std::ofstream out(file);
			out << R"({ "db": { "host": "db.example", "port": 3307, "password": "secret" },
			            "chat": { "gate": { "flood_max_messages": 9 } } })";
		}
		Config cfg;
		REQUIRE(Config::LoadServerConfig(cfg, dir.string()));
		REQUIRE(cfg.GetString("db.host", "") == "db.example");
		REQUIRE(cfg.GetInt("db.port", 0) == 3307);
		REQUIRE(cfg.GetString("db.password", "") == "secret");
		REQUIRE(cfg.GetInt("chat.gate.flood_max_messages", 0) == 9);
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}

	void Test_MissingFileReturnsFalse()
	{
		const auto dir = TempDir();
		Config cfg;
		REQUIRE(Config::LoadServerConfig(cfg, dir.string()) == false);
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}

	void Test_InvalidJsonReturnsFalse()
	{
		const auto dir = TempDir();
		{
			std::ofstream out(dir / "server.config.json");
			out << "{ invalide !!!";
		}
		Config cfg;
		REQUIRE(Config::LoadServerConfig(cfg, dir.string()) == false);
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}
}

int main()
{
	Test_LoadsServerKeys();
	Test_MissingFileReturnsFalse();
	Test_InvalidJsonReturnsFalse();
	if (g_failed == 0) { std::printf("[PASS] ConfigServerLoadTests\n"); return 0; }
	std::printf("[FAIL] ConfigServerLoadTests: %d failure(s)\n", g_failed);
	return 1;
}
