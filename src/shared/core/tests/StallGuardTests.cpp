// ETAPE5.2 — tests de la garde contre les jugements après un calage (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/core/StallGuard.h"

#include <cmath>
#include <cstdio>

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

	using engine::core::StallGuard;
	using engine::core::StallGuardConfig;
}

int main()
{
	// 1. Boucle régulière (100 ms) : jamais de calage, contrôles toujours permis.
	{
		StallGuard g;
		double now = 500.0;
		bool any = false;
		for (int i = 0; i < 1000; ++i)
		{
			now += 0.1;
			any = g.OnLoop(now) || any;
			Check(g.ChecksAllowed(now), "boucle régulière : contrôles permis");
		}
		Check(!any && g.StallCount() == 0u, "aucun calage");
	}

	// 2. Calage de 5 s : détecté, contrôles suspendus 30 s puis rétablis.
	{
		StallGuard g;
		double now = 500.0;
		(void)g.OnLoop(now);
		now += 0.1;
		(void)g.OnLoop(now);
		now += 5.0;
		Check(g.OnLoop(now), "écart de 5 s : calage détecté");
		Check(g.StallCount() == 1u && std::fabs(g.LastStallSeconds() - 5.0) < 1e-9, "calage compté avec sa durée");
		Check(!g.ChecksAllowed(now), "juste après le calage : contrôles suspendus");
		Check(!g.ChecksAllowed(now + 29.9), "29,9 s après : toujours suspendus");
		Check(g.ChecksAllowed(now + 30.0), "30 s après : contrôles rétablis");
	}

	// 3. Seuil : 2,9 s n'est pas un calage, 3 s exactement en est un.
	{
		StallGuard g;
		(void)g.OnLoop(0.0);
		Check(!g.OnLoop(2.9), "2,9 s : pas de calage");
		StallGuard h;
		(void)h.OnLoop(0.0);
		Check(h.OnLoop(3.0), "3 s exactement : calage");
	}

	// 3 bis. Calages répétés : la suspension est plafonnée à 120 s depuis le premier.
	{
		StallGuard g;
		double now = 0.0;
		(void)g.OnLoop(now);
		for (int i = 0; i < 10; ++i) // un calage de 4 s toutes les ~24 s pendant 4 min
		{
			for (int k = 0; k < 200; ++k)
			{
				now += 0.1;
				(void)g.OnLoop(now);
			}
			now += 4.0;
			(void)g.OnLoop(now);
		}
		Check(g.StallCount() == 10u, "dix calages");
		Check(g.ChecksAllowed(now) && g.SuspensionCapped(now), "série de calages : suspension plafonnée (contrôles permis)");
		// Fin de série puis nouveau calage isolé : nouvelle suspension complète.
		now += 60.0;
		Check(g.OnLoop(now) && !g.ChecksAllowed(now + 1.0) && !g.SuspensionCapped(now + 1.0), "nouvelle série : suspension rétablie");
	}

	// 3 ter. Resync : une attente mesurée seule ne cale pas après un long travail.
	{
		StallGuard g;
		(void)g.OnLoop(0.0);
		g.Resync(10.0); // 10 s de travail normal, puis attente
		Check(!g.OnLoop(10.1), "Resync : le travail ne compte pas comme calage");
		g.Resync(10.2);
		Check(g.OnLoop(50.0), "gel pendant l'attente : calage");
	}

	// 4. Deux calages rapprochés : la suspension repart du second.
	{
		StallGuard g;
		double now = 0.0;
		(void)g.OnLoop(now);
		now += 4.0;
		(void)g.OnLoop(now);
		now += 20.0;
		(void)g.OnLoop(now); // second calage (20 s)
		Check(g.StallCount() == 2u, "deux calages");
		Check(!g.ChecksAllowed(now + 29.0), "suspension prolongée par le second calage");
	}

	// 5. Premier appel, horloge qui recule, valeurs invalides, garde désactivée.
	{
		StallGuard g;
		Check(!g.OnLoop(100.0), "premier appel : référence seulement");
		Check(!g.OnLoop(50.0), "horloge qui recule : pas un calage");
		Check(!g.OnLoop(std::nan("")), "NaN ignoré");
		StallGuardConfig off{};
		off.enabled = false;
		StallGuard d(off);
		(void)d.OnLoop(0.0);
		Check(d.OnLoop(10.0) && d.ChecksAllowed(10.0), "désactivée : calage mesuré, contrôles jamais suspendus");
		StallGuardConfig bad{};
		bad.stallSeconds = -4.0;
		bad.graceSeconds = std::nan("");
		const StallGuardConfig fixed = engine::core::SanitizeStallGuard(bad);
		Check(fixed.stallSeconds >= 0.2 && fixed.graceSeconds == 30.0, "réglages bornés");
		StallGuardConfig big{};
		big.stallSeconds = 1.0e9;
		big.graceSeconds = 1.0e9;
		big.maxSuspendSeconds = 1.0;
		const StallGuardConfig b = engine::core::SanitizeStallGuard(big);
		Check(b.stallSeconds == 600.0 && b.graceSeconds == 3600.0 && b.maxSuspendSeconds == 3600.0,
			"bornes hautes ; suspension max jamais sous la suspension");
	}

	if (s_failCount == 0)
		std::puts("[OK] stall_guard_tests");
	return s_failCount == 0 ? 0 : 1;
}
