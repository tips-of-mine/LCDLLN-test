/// Tests unitaires CPU pour Config::SaveToFile (sous-projet 1, bloc H).
/// Vérifie le round-trip SaveToFile -> LoadFromFile pour les 4 types scalaires
/// (int, bool, string, double), y compris des clés pointées multi-niveaux.
/// Pur CPU + E/S disque, ctest Linux.

#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
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

	std::filesystem::path TempFile()
	{
		std::random_device rd;
		std::mt19937_64 rng(rd());
		return std::filesystem::temp_directory_path()
			/ ("lcdlln_config_roundtrip_" + std::to_string(rng()) + ".json");
	}

	void Test_RoundTripScalars()
	{
		const std::filesystem::path path = TempFile();

		Config a;
		a.SetValue("editor.world.undo.capacity", Config::Value{ static_cast<int64_t>(42) });
		a.SetValue("editor.world.show_grid",     Config::Value{ true });
		a.SetValue("editor.world.last_zone",     Config::Value{ std::string("demo_zone") });
		a.SetValue("editor.world.grid_cell_m",   Config::Value{ 2.5 });

		REQUIRE(a.SaveToFile(path.string()));

		Config b;
		REQUIRE(b.LoadFromFile(path.string()));
		REQUIRE(b.GetInt("editor.world.undo.capacity", 0) == 42);
		REQUIRE(b.GetBool("editor.world.show_grid", false) == true);
		REQUIRE(b.GetString("editor.world.last_zone", "") == "demo_zone");
		REQUIRE(b.GetDouble("editor.world.grid_cell_m", 0.0) == 2.5);

		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	void Test_StringWithSpecialChars()
	{
		const std::filesystem::path path = TempFile();
		Config a;
		a.SetValue("editor.note", Config::Value{ std::string("a\"b\\c") });
		REQUIRE(a.SaveToFile(path.string()));
		Config b;
		REQUIRE(b.LoadFromFile(path.string()));
		REQUIRE(b.GetString("editor.note", "") == "a\"b\\c");
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}

int main()
{
	Test_RoundTripScalars();
	Test_StringWithSpecialChars();

	if (g_failed == 0)
	{
		std::printf("[PASS] ConfigSaveToFileTests\n");
		return 0;
	}
	std::printf("[FAIL] ConfigSaveToFileTests: %d failure(s)\n", g_failed);
	return 1;
}
