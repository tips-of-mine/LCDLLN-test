// Tests du résolveur de PV à l'enter-world : ancre Guerrier Nain H niv.60
// (maxHealth ~3078, identique à CharacterStatsEngineTests), restauration des PV
// courants (plein si 0, conservé si fourni, plafonné à maxHealth), faction
// inconnue → resolved=false. Plain-main, NDEBUG-safe (pas d'assert).
#include "src/shardd/gameplay/character/SpawnStatsResolver.h"
#include "src/shared/core/Log.h"

#include "CharacterStatsData.h"  // kCharacterStatsJson (généré)
#include "FactionsData.h"        // kFactionsJson (généré)

#include <cstdio>

namespace
{
	using namespace engine::server::gameplay;

	// PV plein quand aucun PV courant n'est persisté (persisted == 0).
	bool TestSpawnFull()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) { fprintf(stderr, "[SpawnStatsResolverTests] FromEmbedded null\n"); return false; }
		auto h = ResolveSpawnHealth(*t, "naine", "guerrier", Sex::Male, 60, /*persisted*/0u);
		if (!h.resolved) { fprintf(stderr, "[SpawnStatsResolverTests] full: resolved=false\n"); return false; }
		if (h.maxHealth < 3076u || h.maxHealth > 3080u) { fprintf(stderr, "[SpawnStatsResolverTests] full: maxHealth=%u\n", h.maxHealth); return false; }
		if (h.currentHealth != h.maxHealth) { fprintf(stderr, "[SpawnStatsResolverTests] full: current=%u != max=%u\n", h.currentHealth, h.maxHealth); return false; }
		return true;
	}

	// PV courant persisté conservé tel quel (100 < maxHealth).
	bool TestSpawnPersisted()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) { fprintf(stderr, "[SpawnStatsResolverTests] FromEmbedded null\n"); return false; }
		auto h = ResolveSpawnHealth(*t, "naine", "guerrier", Sex::Male, 60, /*persisted*/100u);
		if (!h.resolved) { fprintf(stderr, "[SpawnStatsResolverTests] persisted: resolved=false\n"); return false; }
		if (h.maxHealth < 3076u || h.maxHealth > 3080u) { fprintf(stderr, "[SpawnStatsResolverTests] persisted: maxHealth=%u\n", h.maxHealth); return false; }
		if (h.currentHealth != 100u) { fprintf(stderr, "[SpawnStatsResolverTests] persisted: current=%u != 100\n", h.currentHealth); return false; }
		return true;
	}

	// PV courant persisté absurde (> maxHealth) plafonné à maxHealth.
	bool TestSpawnClamped()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) { fprintf(stderr, "[SpawnStatsResolverTests] FromEmbedded null\n"); return false; }
		auto h = ResolveSpawnHealth(*t, "naine", "guerrier", Sex::Male, 60, /*persisted*/999999u);
		if (!h.resolved) { fprintf(stderr, "[SpawnStatsResolverTests] clamped: resolved=false\n"); return false; }
		if (h.currentHealth != h.maxHealth) { fprintf(stderr, "[SpawnStatsResolverTests] clamped: current=%u != max=%u\n", h.currentHealth, h.maxHealth); return false; }
		return true;
	}

	// Faction inconnue → resolved=false, l'appelant garde son défaut.
	bool TestUnknownFaction()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) { fprintf(stderr, "[SpawnStatsResolverTests] FromEmbedded null\n"); return false; }
		auto h = ResolveSpawnHealth(*t, "inconnue", "guerrier", Sex::Male, 1, /*persisted*/0u);
		if (h.resolved) { fprintf(stderr, "[SpawnStatsResolverTests] unknown: resolved=true (attendu false)\n"); return false; }
		return true;
	}
}

int main()
{
	engine::core::LogSettings s; s.level = engine::core::LogLevel::Info; s.console = true;
	engine::core::Log::Init(s);
	const bool ok = TestSpawnFull()
	             && TestSpawnPersisted()
	             && TestSpawnClamped()
	             && TestUnknownFaction();
	if (ok) LOG_INFO(Core, "[SpawnStatsResolverTests] ALL OK");
	else    LOG_ERROR(Core, "[SpawnStatsResolverTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
