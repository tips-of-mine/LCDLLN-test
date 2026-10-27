// ETAPE4.3 — tests du cycle de vie des régions (chaude, froide, déchargée), sans réseau.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/RegionLifecycle.h"

#include <cstdio>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using engine::server::GridState;
	using engine::server::GridStateConfig;
	using engine::server::RegionLifecycle;
	using engine::world::cells::CellCoord;

	CellCoord Region(int n, int e)
	{
		CellCoord c{};
		c.n = n;
		c.e = e;
		return c;
	}
}

int main()
{
	GridStateConfig cfg{};
	cfg.idleTimeout = std::chrono::seconds{ 60 };
	cfg.unloadTimeout = std::chrono::seconds{ 300 };
	RegionLifecycle life(cfg);
	const RegionLifecycle::TimePoint t0 = RegionLifecycle::Clock::now();
	auto at = [&](int seconds) { return t0 + std::chrono::seconds{ seconds }; };
	const CellCoord a = Region(3, -7);
	const CellCoord b = Region(-120, 45);

	// 1. Région jamais visitée : froide d'office, non suivie.
	Check(!life.IsWarm(a) && life.TrackedCount() == 0u, "jamais visitée : froide");

	// 2. Active, puis encore chaude 59 s après le départ, froide à 60 s.
	(void)life.Update({ a }, at(0));
	Check(life.IsWarm(a) && life.StateOf(a) == GridState::Active, "joueur présent : active");
	(void)life.Update({}, at(1));
	Check(life.IsWarm(a) && life.StateOf(a) == GridState::Loaded, "départ : encore chaude (Loaded)");
	Check(life.Update({}, at(60)).empty() && life.IsWarm(a), "59 s après le départ : chaude");
	const std::vector<CellCoord> cold = life.Update({}, at(61));
	Check(cold.size() == 1u && cold[0] == a, "60 s : signalée froide une fois");
	Check(!life.IsWarm(a) && life.StateOf(a) == GridState::Idle, "froide (Idle)");
	Check(life.Update({}, at(62)).empty(), "pas signalée deux fois");

	// 3. Retour d'un joueur : chaude à nouveau, le délai repart de zéro.
	(void)life.Update({ a }, at(100));
	Check(life.IsWarm(a) && life.StateOf(a) == GridState::Active, "retour : active");
	(void)life.Update({}, at(101));
	Check(life.Update({}, at(150)).empty() && life.IsWarm(a), "nouveau départ : délai repart de zéro");

	// 4. Déchargement après 300 s : région oubliée.
	(void)life.Update({}, at(401));
	const std::vector<CellCoord> unloaded = life.DrainUnloaded();
	Check(unloaded.size() == 1u && unloaded[0] == a, "300 s : déchargée");
	Check(life.TrackedCount() == 0u && !life.IsWarm(a), "déchargée : oubliée");
	Check(life.DrainUnloaded().empty(), "rien à décharger deux fois");

	// 5. Plusieurs régions, doublons, coordonnées négatives.
	(void)life.Update({ a, b, b }, at(500));
	Check(life.IsWarm(a) && life.IsWarm(b) && life.TrackedCount() == 2u && life.WarmCount() == 2u, "deux régions actives");
	(void)life.Update({ b }, at(501));
	Check(life.StateOf(a) == GridState::Loaded && life.StateOf(b) == GridState::Active, "une seule reste active");
	(void)life.Update({ b }, at(600));
	Check(!life.IsWarm(a) && life.IsWarm(b) && life.WarmCount() == 1u, "a froide, b chaude");

	// 6. Délais incohérents corrigés : déchargement jamais avant la froideur.
	{
		GridStateConfig bad{};
		bad.idleTimeout = std::chrono::seconds{ 120 };
		bad.unloadTimeout = std::chrono::seconds{ 30 };
		RegionLifecycle l2(bad);
		Check(l2.Config().unloadTimeout == std::chrono::seconds{ 120 }, "déchargement relevé à la froideur");
		(void)l2.Update({ a }, at(0));
		(void)l2.Update({}, at(1));
		const std::vector<CellCoord> c2 = l2.Update({}, at(122));
		Check(c2.size() == 1u && !l2.IsWarm(a), "froide et déchargeable au même délai : signalée froide");
		Check(l2.DrainUnloaded().size() == 1u, "puis déchargée");
	}

	if (s_failCount == 0)
		std::puts("[OK] region_lifecycle_tests");
	return s_failCount == 0 ? 0 : 1;
}
