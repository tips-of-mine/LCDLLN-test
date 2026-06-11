// Combat SP1 — tests du catalogue d'archétypes de créatures (parse + validation).

#include "src/shardd/gameplay/creature/CreatureArchetypeLibrary.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <string>

namespace
{
	using engine::server::CreatureArchetype;
	using engine::server::CreatureArchetypeLibrary;

	/// Construit une library de test sans I/O (la Config vide n'est pas utilisée
	/// par LoadFromText, seulement par Init qui n'est pas exercé ici).
	CreatureArchetypeLibrary MakeLibrary()
	{
		engine::core::Config config;
		return CreatureArchetypeLibrary(config);
	}

	const char* kValidJson = R"({
  "archetypes": [
    {
      "id": 100,
      "name": "Sanglier des collines",
      "level": 2,
      "stats": { "hp": 60, "damage": 5, "accuracy": 80.0, "rangeMeters": 2.5,
                 "critRate": 2.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 10,
      "model": { "mesh": "orcs", "scale": 0.9 }
    },
    {
      "id": 101,
      "name": "Loup gris",
      "level": 3,
      "stats": { "hp": 80, "damage": 7, "accuracy": 85.0, "rangeMeters": 2.0,
                 "critRate": 3.0, "critMult": 1.5, "attackPeriodMs": 1500 },
      "xpReward": 14,
      "model": { "mesh": "humains" }
    }
  ]
})";

	bool TestValidCatalog()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		if (!library.LoadFromText(kValidJson, error))
		{
			LOG_ERROR(Core, "[CreatureArchetypeLibraryTests] valid catalog rejected: {}", error);
			return false;
		}
		if (library.Count() != 2) return false;
		const CreatureArchetype* boar = library.Find(100);
		if (boar == nullptr) return false;
		if (boar->hp != 60 || boar->damage != 5 || boar->xpReward != 10) return false;
		if (boar->level != 2 || boar->meshKey != "orcs") return false;
		if (boar->attackPeriodMs != 2000) return false;
		if (boar->scale < 0.89f || boar->scale > 0.91f) return false;
		if (library.Find(999) != nullptr) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] valid catalog OK");
		return true;
	}

	bool TestScaleDefaultsToOne()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		if (!library.LoadFromText(kValidJson, error)) return false;
		const CreatureArchetype* wolf = library.Find(101);
		if (wolf == nullptr) return false;
		// model.scale absent → défaut 1.0.
		if (wolf->scale < 0.999f || wolf->scale > 1.001f) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] scale default OK");
		return true;
	}

	bool TestMissingArchetypesArray()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		if (library.LoadFromText(R"({ "autre": [] })", error)) return false;
		if (error.empty()) return false;
		if (library.Count() != 0) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] missing array rejected OK");
		return true;
	}

	bool TestDuplicateIdRejected()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		const char* duplicateJson = R"({
  "archetypes": [
    { "id": 100, "name": "A", "level": 1,
      "stats": { "hp": 10, "damage": 1, "accuracy": 80.0, "rangeMeters": 2.0,
                 "critRate": 0.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 1, "model": { "mesh": "orcs" } },
    { "id": 100, "name": "B", "level": 1,
      "stats": { "hp": 10, "damage": 1, "accuracy": 80.0, "rangeMeters": 2.0,
                 "critRate": 0.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 1, "model": { "mesh": "orcs" } }
  ]
})";
		if (library.LoadFromText(duplicateJson, error)) return false;
		if (library.Count() != 0) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] duplicate id rejected OK");
		return true;
	}

	bool TestZeroHpRejected()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		const char* zeroHpJson = R"({
  "archetypes": [
    { "id": 100, "name": "A", "level": 1,
      "stats": { "hp": 0, "damage": 1, "accuracy": 80.0, "rangeMeters": 2.0,
                 "critRate": 0.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 1, "model": { "mesh": "orcs" } }
  ]
})";
		if (library.LoadFromText(zeroHpJson, error)) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] zero hp rejected OK");
		return true;
	}

	bool TestZeroAttackPeriodRejected()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		const char* zeroPeriodJson = R"({
  "archetypes": [
    { "id": 100, "name": "A", "level": 1,
      "stats": { "hp": 10, "damage": 1, "accuracy": 80.0, "rangeMeters": 2.0,
                 "critRate": 0.0, "critMult": 1.5, "attackPeriodMs": 0 },
      "xpReward": 1, "model": { "mesh": "orcs" } }
  ]
})";
		if (library.LoadFromText(zeroPeriodJson, error)) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] zero attackPeriodMs rejected OK");
		return true;
	}

	bool TestMissingModelRejected()
	{
		CreatureArchetypeLibrary library = MakeLibrary();
		std::string error;
		const char* missingModelJson = R"({
  "archetypes": [
    { "id": 100, "name": "A", "level": 1,
      "stats": { "hp": 10, "damage": 1, "accuracy": 80.0, "rangeMeters": 2.0,
                 "critRate": 0.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 1 }
  ]
})";
		if (library.LoadFromText(missingModelJson, error)) return false;
		LOG_INFO(Core, "[CreatureArchetypeLibraryTests] missing model rejected OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestValidCatalog()
		&& TestScaleDefaultsToOne()
		&& TestMissingArchetypesArray()
		&& TestDuplicateIdRejected()
		&& TestZeroHpRejected()
		&& TestZeroAttackPeriodRejected()
		&& TestMissingModelRejected();

	if (ok) LOG_INFO(Core, "[CreatureArchetypeLibraryTests] ALL OK");
	else LOG_ERROR(Core, "[CreatureArchetypeLibraryTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
