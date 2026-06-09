// Tests du moteur de stats : round-trip JSON embarqué -> tables -> calcul,
// ancres exactes (niveau 1 et 60), invariants (cap crit, mêlée pure).
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shared/core/Log.h"

#include "CharacterStatsData.h"  // kCharacterStatsJson (généré)
#include "FactionsData.h"        // kFactionsJson (généré)

#include <cmath>

namespace
{
	using namespace engine::server::gameplay;

	bool nearF(float a, double b, double tol = 0.6) { return std::fabs(static_cast<double>(a) - b) <= tol; }

	bool TestRoundTripAndAnchorsLvl1()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		auto d = ComputeStats(*t, "elfe", "voleur_tenebreux", Sex::Female, 1);
		if (!d) return false;
		if (d->hp != 81u) return false;       // 100 * 0.90 * 0.90 * 1 = 81
		if (d->damage != 11u) return false;   // 10 * 1.25 * 0.95 * 0.93 = 11.04 -> 11
		if (!nearF(d->critRate, 2.2, 0.05)) return false; // 2 * 1.00 * 1.10
		if (d->resourceKey != "reflexes") return false;
		return true;
	}

	bool TestAnchorLvl60_GuerrierNainH()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		auto d = ComputeStats(*t, "naine", "guerrier", Sex::Male, 60);
		if (!d) return false;
		// base hp(60)=2424.2424 * 1.15 * 1.20 * 0.92 ~= 3078
		if (d->hp < 3076u || d->hp > 3080u) return false;
		if (d->range != 0.0f) return false;      // mêlée pure
		if (d->accuracy != 0.0f) return false;
		return true;
	}

	bool TestCritCapAndUnknownAndLanceurLvl100()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		auto d = ComputeStats(*t, "legion", "demoniste", Sex::Female, 100);
		if (!d) return false;
		if (d->critRate > 10.0f) return false;   // cap
		if (!(d->range > 0.0f) || !(d->accuracy > 0.0f)) return false; // lanceur a une portée
		if (d->resourceKey != "corruption") return false;
		if (ComputeStats(*t, "inconnue", "guerrier", Sex::Male, 1)) return false;
		if (ComputeStats(*t, "naine", "inconnue", Sex::Male, 1)) return false;
		return true;
	}

	bool TestDeterminism()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		auto a = ComputeStats(*t, "dzorak", "pisteur", Sex::Male, 42);
		auto b = ComputeStats(*t, "dzorak", "pisteur", Sex::Male, 42);
		if (!a || !b) return false;
		return a->hp == b->hp && a->damage == b->damage && nearF(a->stealth, b->stealth, 0.0001);
	}
}

int main()
{
	engine::core::LogSettings s; s.level = engine::core::LogLevel::Info; s.console = true;
	engine::core::Log::Init(s);
	const bool ok = TestRoundTripAndAnchorsLvl1()
	             && TestAnchorLvl60_GuerrierNainH()
	             && TestCritCapAndUnknownAndLanceurLvl100()
	             && TestDeterminism();
	if (ok) LOG_INFO(Core, "[CharacterStatsEngineTests] ALL OK");
	else    LOG_ERROR(Core, "[CharacterStatsEngineTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
