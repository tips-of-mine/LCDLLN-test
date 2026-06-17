/// Tests CPU du chargement du contenu de zone (PR2 — éclatement config.json).
/// Vérifie que LoadActiveZone lit world.active_zone et fusionne
/// <contentRoot>/zones/<zone>/{zone.json,scenery.json} avec les préfixes world.* conservés.
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

	std::filesystem::path TempRoot()
	{
		std::random_device rd; std::mt19937_64 rng(rd());
		return std::filesystem::temp_directory_path() / ("lcdlln_zone_" + std::to_string(rng()));
	}

	void Test_LoadsZoneAndScenery()
	{
		const auto root = TempRoot();
		const auto zoneDir = root / "zones" / "feyhin";
		std::filesystem::create_directories(zoneDir);
		{
			std::ofstream(zoneDir / "zone.json")
				<< R"({ "world": { "default_spawn": { "x": 12.0 }, "fog": { "start_m": 40.0 } } })";
			std::ofstream(zoneDir / "scenery.json")
				<< R"({ "world": { "scenery": { "count": 3 } } })";
		}
		Config cfg;
		cfg.SetValue("world.active_zone", Config::Value{ std::string("feyhin") });
		REQUIRE(Config::LoadActiveZone(cfg, root.string()));
		REQUIRE(cfg.GetInt("world.scenery.count", 0) == 3);
		REQUIRE(cfg.GetDouble("world.default_spawn.x", 0.0) == 12.0);
		REQUIRE(cfg.GetDouble("world.fog.start_m", 0.0) == 40.0);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}

	void Test_MissingZoneReturnsFalse()
	{
		const auto root = TempRoot();
		std::filesystem::create_directories(root);
		Config cfg;
		cfg.SetValue("world.active_zone", Config::Value{ std::string("absente") });
		REQUIRE(Config::LoadActiveZone(cfg, root.string()) == false);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}

	void Test_EmptyActiveZoneReturnsFalse()
	{
		const auto root = TempRoot();
		std::filesystem::create_directories(root);
		Config cfg; // world.active_zone non défini
		REQUIRE(Config::LoadActiveZone(cfg, root.string()) == false);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}

	// Garantie centrale : scenery.json optionnel — zone.json seul suffit à renvoyer true.
	void Test_MissingSceneryStillReturnsTrue()
	{
		const auto root = TempRoot();
		const auto zoneDir = root / "zones" / "feyhin";
		std::filesystem::create_directories(zoneDir);
		{
			std::ofstream(zoneDir / "zone.json")
				<< R"({ "world": { "default_spawn": { "x": 5.0 } } })";
			// scenery.json délibérément absent
		}
		Config cfg;
		cfg.SetValue("world.active_zone", Config::Value{ std::string("feyhin") });
		REQUIRE(Config::LoadActiveZone(cfg, root.string()) == true);
		REQUIRE(cfg.GetDouble("world.default_spawn.x", 0.0) == 5.0);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}
}

int main()
{
	Test_LoadsZoneAndScenery();
	Test_MissingZoneReturnsFalse();
	Test_EmptyActiveZoneReturnsFalse();
	Test_MissingSceneryStillReturnsTrue();
	if (g_failed == 0) { std::printf("[PASS] ConfigZoneLoadTests\n"); return 0; }
	std::printf("[FAIL] ConfigZoneLoadTests: %d failure(s)\n", g_failed);
	return 1;
}
