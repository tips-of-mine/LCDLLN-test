// Combat SP3 — tests de la bibliothèque de kits de sorts (parse + validation).

#include "src/shardd/gameplay/spell/SpellKitLibrary.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <string>

namespace
{
	using engine::server::SpellDef;
	using engine::server::SpellEffectType;
	using engine::server::SpellKitLibrary;
	using engine::server::SpellTargetKind;

	/// Library de test sans I/O (LoadKitFromText seulement).
	SpellKitLibrary MakeLibrary()
	{
		engine::core::Config config;
		return SpellKitLibrary(config);
	}

	const char* kValidKit = R"({
  "profile": "melee",
  "spells": [
    { "id": "melee_frappe", "name": "Frappe", "slot": 2, "castTimeMs": 0,
      "cooldownMs": 6000, "resourceCostPercent": 15, "rangeMeters": 0,
      "targetType": "SingleEnemy", "areaRadiusMeters": 0,
      "effects": [ { "type": "DirectDamage", "mult": 1.4 } ] },
    { "id": "melee_cri", "name": "Cri", "slot": 1, "castTimeMs": 0,
      "cooldownMs": 30000, "resourceCostPercent": 25, "rangeMeters": 0,
      "targetType": "SelfOnly", "areaRadiusMeters": 0,
      "effects": [ { "type": "BuffDamagePercent", "percent": 15, "durationMs": 10000 } ] }
  ]
})";

	bool TestValidKitSortedBySlot()
	{
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		if (!library.LoadKitFromText(kValidKit, error))
		{
			LOG_ERROR(Core, "[SpellKitLibraryTests] valid kit rejected: {}", error);
			return false;
		}
		if (library.KitCount() != 1) return false;
		const auto* kit = library.FindKit("melee");
		if (kit == nullptr || kit->size() != 2) return false;
		// Tri par slot : "Cri" (slot 1) doit précéder "Frappe" (slot 2).
		if ((*kit)[0].spellId != "melee_cri" || (*kit)[1].spellId != "melee_frappe") return false;
		const SpellDef* frappe = library.FindSpell("melee", "melee_frappe");
		if (frappe == nullptr) return false;
		if (frappe->cooldownMs != 6000 || frappe->resourceCostPercent != 15) return false;
		if (frappe->targetType != SpellTargetKind::SingleEnemy) return false;
		if (frappe->effects.size() != 1 || frappe->effects[0].type != SpellEffectType::DirectDamage) return false;
		if (library.FindSpell("melee", "inconnu") != nullptr) return false;
		if (library.FindKit("tank") != nullptr) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] valid kit OK");
		return true;
	}

	bool TestZeroCooldownAccepted()
	{
		// healer « Soin rapide » : cooldownMs 0 = sort spammable, doit passer.
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "healer",
  "spells": [
    { "id": "healer_soin", "name": "Soin", "slot": 1, "castTimeMs": 1500,
      "cooldownMs": 0, "resourceCostPercent": 25, "rangeMeters": 30,
      "targetType": "SingleAlly", "areaRadiusMeters": 0,
      "effects": [ { "type": "DirectHeal", "mult": 2.0 } ] }
  ]
})";
		if (!library.LoadKitFromText(kit, error))
		{
			LOG_ERROR(Core, "[SpellKitLibraryTests] zero cooldown rejected: {}", error);
			return false;
		}
		LOG_INFO(Core, "[SpellKitLibraryTests] zero cooldown OK");
		return true;
	}

	bool TestDuplicateSlotRejected()
	{
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "melee",
  "spells": [
    { "id": "a", "name": "A", "slot": 1, "castTimeMs": 0, "cooldownMs": 1000,
      "resourceCostPercent": 10, "rangeMeters": 0, "targetType": "SelfOnly",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "BuffDamagePercent", "percent": 5, "durationMs": 5000 } ] },
    { "id": "b", "name": "B", "slot": 1, "castTimeMs": 0, "cooldownMs": 1000,
      "resourceCostPercent": 10, "rangeMeters": 0, "targetType": "SelfOnly",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "BuffDamagePercent", "percent": 5, "durationMs": 5000 } ] }
  ]
})";
		if (library.LoadKitFromText(kit, error)) return false;
		if (error.empty()) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] duplicate slot rejected OK");
		return true;
	}

	bool TestUnknownEffectTypeRejected()
	{
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "melee",
  "spells": [
    { "id": "a", "name": "A", "slot": 1, "castTimeMs": 0, "cooldownMs": 1000,
      "resourceCostPercent": 10, "rangeMeters": 0, "targetType": "SingleEnemy",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "Teleport", "mult": 1.0 } ] }
  ]
})";
		if (library.LoadKitFromText(kit, error)) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] unknown effect type rejected OK");
		return true;
	}

	bool TestDotWithoutDurationRejected()
	{
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "melee",
  "spells": [
    { "id": "a", "name": "A", "slot": 1, "castTimeMs": 0, "cooldownMs": 1000,
      "resourceCostPercent": 10, "rangeMeters": 0, "targetType": "SingleEnemy",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "DamageOverTime", "mult": 0.3, "tickPeriodMs": 2000 } ] }
  ]
})";
		if (library.LoadKitFromText(kit, error)) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] DoT without duration rejected OK");
		return true;
	}

	bool TestPercentMaxHpHotAccepted()
	{
		// tank « Second souffle » : HoT en % des PV max (mult 0).
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "tank",
  "spells": [
    { "id": "tank_souffle", "name": "Souffle", "slot": 1, "castTimeMs": 0,
      "cooldownMs": 30000, "resourceCostPercent": 30, "rangeMeters": 0,
      "targetType": "SelfOnly", "areaRadiusMeters": 0,
      "effects": [ { "type": "HealOverTime", "mult": 0, "percentMaxHpPerTick": 3,
                     "tickPeriodMs": 2000, "durationMs": 10000 } ] }
  ]
})";
		if (!library.LoadKitFromText(kit, error))
		{
			LOG_ERROR(Core, "[SpellKitLibraryTests] percentMaxHp HoT rejected: {}", error);
			return false;
		}
		LOG_INFO(Core, "[SpellKitLibraryTests] percentMaxHp HoT OK");
		return true;
	}

	bool TestMultiEffectAccepted()
	{
		// pisteur « Morsure du piege » : DoT + ralentissement sur le même sort.
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "pisteur",
  "spells": [
    { "id": "pisteur_morsure", "name": "Morsure", "slot": 1, "castTimeMs": 0,
      "cooldownMs": 15000, "resourceCostPercent": 25, "rangeMeters": 25,
      "targetType": "SingleEnemy", "areaRadiusMeters": 0,
      "effects": [
        { "type": "DamageOverTime", "mult": 0.3, "tickPeriodMs": 2000, "durationMs": 8000 },
        { "type": "SlowMobPercent", "percent": 20, "durationMs": 4000 }
      ] }
  ]
})";
		if (!library.LoadKitFromText(kit, error))
		{
			LOG_ERROR(Core, "[SpellKitLibraryTests] multi-effect rejected: {}", error);
			return false;
		}
		const SpellDef* morsure = library.FindSpell("pisteur", "pisteur_morsure");
		if (morsure == nullptr || morsure->effects.size() != 2) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] multi-effect OK");
		return true;
	}

	bool TestAreaWithoutRadiusRejected()
	{
		SpellKitLibrary library = MakeLibrary();
		std::string error;
		const char* kit = R"({
  "profile": "lanceur",
  "spells": [
    { "id": "a", "name": "A", "slot": 1, "castTimeMs": 0, "cooldownMs": 1000,
      "resourceCostPercent": 10, "rangeMeters": 0, "targetType": "AreaAroundSelf",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "DirectDamage", "mult": 0.9 } ] }
  ]
})";
		if (library.LoadKitFromText(kit, error)) return false;
		LOG_INFO(Core, "[SpellKitLibraryTests] AoE without radius rejected OK");
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

	const bool ok = TestValidKitSortedBySlot()
		&& TestZeroCooldownAccepted()
		&& TestDuplicateSlotRejected()
		&& TestUnknownEffectTypeRejected()
		&& TestDotWithoutDurationRejected()
		&& TestPercentMaxHpHotAccepted()
		&& TestMultiEffectAccepted()
		&& TestAreaWithoutRadiusRejected();

	if (ok) LOG_INFO(Core, "[SpellKitLibraryTests] ALL OK");
	else LOG_ERROR(Core, "[SpellKitLibraryTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
