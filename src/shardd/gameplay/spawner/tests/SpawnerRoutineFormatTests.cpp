// spawner_routine_format_tests — v1.0.143 : format des routines à altitude réelle dans
// zones/<zone>/spawners.json (ville-tour de Magistrya, tools/world_gen Output.cpp).
//
// Aller-retour du format : un spawners.json écrit comme le générateur (routine `tour`
// avec `realHeight: true` et `pathY`) est relu par SpawnerRuntime ; chaque point du
// chemin retrouve son altitude. Champs additifs : un `pathY` incohérent (taille) laisse
// la routine valide mais sans altitude réelle ; une routine sans ces champs est inchangée.
#include "src/shardd/gameplay/spawner/SpawnerRuntime.h"

#include "src/shared/core/Config.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace engine::server;

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	/// Spawner de la ville-tour tel que l'écrit tools/world_gen (Output.cpp, v1.0.143).
	const char* kSpawners = R"({
  "zoneId": 1,
  "spawners": [
    { "id": "tour3d", "archetypeId": 2001, "position": [10.0, 82.5, 20.0], "count": 1, "respawnSec": 30, "leashDistanceMeters": 200,
      "routine": { "kind": "tour", "schedule": "day", "pauseMinSec": 3, "pauseMaxSec": 14, "pauseChance": 0.4,
        "path": [[10, 20], [12, 20], [12, 25], [10, 20]], "realHeight": true, "pathY": [82.5, 82.5, 86.67, 82.5] } },
    { "id": "pathy_bancal", "archetypeId": 2001, "position": [0.0, 5.0, 0.0], "count": 1, "respawnSec": 30, "leashDistanceMeters": 200,
      "routine": { "kind": "tour", "schedule": "always", "path": [[0, 0], [4, 0]], "realHeight": true, "pathY": [5.0] } },
    { "id": "plat", "archetypeId": 2001, "position": [0.0, 5.0, 0.0], "count": 1, "respawnSec": 30, "leashDistanceMeters": 200,
      "routine": { "kind": "patrol", "schedule": "always", "path": [[0, 0], [4, 0]] } }
  ]
})";

	const SpawnerDefinition* Find(const SpawnerRuntime& rt, const char* id)
	{
		for (const SpawnerDefinition& d : rt.GetDefinitions())
			if (d.spawnerId == id) return &d;
		return nullptr;
	}
}

int main()
{
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "lcdlln_spawner_routine_format";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "zones" / "zone_test");
	{
		std::ofstream f(root / "zones" / "zone_test" / "spawners.json");
		f << kSpawners;
	}
	engine::core::Config cfg;
	cfg.SetValue("paths.content", root.string());
	SpawnerRuntime rt(cfg);
	const bool loaded = rt.Init();
	Check(loaded, "spawners.json relu");
	if (loaded)
	{
		const SpawnerDefinition* a = Find(rt, "tour3d");
		Check(a != nullptr && a->routine.realHeight && a->routine.path.size() == 4, "routine a altitude reelle relue");
		if (a != nullptr && a->routine.path.size() == 4)
		{
			Check(std::fabs(a->routine.path[0].y - 82.5f) < 1e-4f && std::fabs(a->routine.path[2].y - 86.67f) < 1e-4f
					&& std::fabs(a->routine.path[2].x - 12.0f) < 1e-4f && std::fabs(a->routine.path[2].z - 25.0f) < 1e-4f,
				"chaque point retrouve x, z et son altitude");
			Check(std::fabs(a->positionMetersY - 82.5f) < 1e-4f, "altitude du spawner = premier point (porte du palier)");
		}
		const SpawnerDefinition* b = Find(rt, "pathy_bancal");
		Check(b != nullptr && !b->routine.realHeight && b->routine.path.size() == 2 && b->routine.path[1].y == 0.0f,
			"pathY de mauvaise taille : routine gardee, sans altitude reelle");
		const SpawnerDefinition* c = Find(rt, "plat");
		Check(c != nullptr && !c->routine.realHeight && c->routine.path.size() == 2, "routine sans champs 3D inchangee");
	}
	rt.Shutdown();
	std::filesystem::remove_all(root);
	if (g_fail == 0) std::printf("spawner_routine_format_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
