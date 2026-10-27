// ETAPE4.5 — tests du délestage avec hystérésis (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/LoadShedding.h"

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

	using namespace engine::server;
	constexpr double kBudget = 0.05; // 20 Hz

	/// Fait tourner \p seconds de ticks à la charge \p load ; renvoie le nombre de bascules.
	int Run(LoadShedder& s, double& now, double seconds, double load, SheddingTransition* last = nullptr)
	{
		int switches = 0;
		const int ticks = static_cast<int>(std::lround(seconds / kBudget));
		for (int i = 0; i < ticks; ++i)
		{
			now += kBudget;
			const SheddingTransition t = s.OnTick(load * kBudget, kBudget, now);
			if (t != SheddingTransition::None)
			{
				++switches;
				if (last != nullptr) *last = t;
			}
		}
		return switches;
	}
}

int main()
{
	// 1. Charge normale : jamais de délestage.
	{
		LoadShedder s;
		double now = 1000.0;
		Check(Run(s, now, 60.0, 0.4) == 0 && !s.Active(), "40 % du budget : normal");
		Check(std::fabs(s.SmoothedLoad() - 0.4) < 0.01, "charge lissée ≈ 40 %");
	}

	// 2. Surcharge soutenue : entrée après le maintien de 2 s (plus le lissage).
	{
		LoadShedder s;
		double now = 1000.0;
		Check(Run(s, now, 1.0, 1.2) == 0 && !s.Active(), "1 s de surcharge : pas encore");
		SheddingTransition t{};
		Check(Run(s, now, 3.0, 1.2, &t) == 1 && t == SheddingTransition::Entered && s.Active(), "surcharge soutenue : délestage");
		Check(s.EnterCount() == 1u, "une entrée comptée");
	}

	// 3. Pic isolé (un tick lent) : absorbé par le lissage, aucune bascule.
	{
		LoadShedder s;
		double now = 1000.0;
		(void)Run(s, now, 5.0, 0.4);
		now += kBudget;
		(void)s.OnTick(10.0 * kBudget, kBudget, now); // un tick de 500 ms
		Check(Run(s, now, 10.0, 0.4) == 0 && !s.Active(), "pic isolé ignoré");
	}

	// 4. Hystérésis : entre les deux seuils, l'état ne change pas ; sortie après 10 s sous 60 %.
	{
		LoadShedder s;
		double now = 1000.0;
		(void)Run(s, now, 5.0, 1.2);
		Check(s.Active(), "en délestage");
		Check(Run(s, now, 60.0, 0.75) == 0 && s.Active(), "75 % (entre les seuils) : reste en délestage");
		SheddingTransition t{};
		Check(Run(s, now, 9.0, 0.3) == 0 && s.Active(), "9 s sous le seuil de sortie : pas encore");
		Check(Run(s, now, 5.0, 0.3, &t) == 1 && t == SheddingTransition::Exited && !s.Active(), "sortie après le maintien");
		Check(Run(s, now, 60.0, 0.75) == 0 && !s.Active(), "75 % après la sortie : reste normal");
	}

	// 5. Délai minimal entre bascules : une rechute immédiate attend 15 s.
	{
		LoadSheddingConfig cfg{};
		cfg.exitHoldSeconds = 0.0;
		LoadShedder s(cfg);
		double now = 1000.0;
		double entered = -1.0;
		double exited = -1.0;
		for (int i = 0; i < 25 * 20; ++i) // 25 s
		{
			now += kBudget;
			const double load = i < 60 ? 1.5 : 0.1; // 3 s de surcharge puis presque rien
			const SheddingTransition t = s.OnTick(load * kBudget, kBudget, now);
			if (t == SheddingTransition::Entered) entered = now;
			if (t == SheddingTransition::Exited) exited = now;
		}
		Check(entered > 0.0 && exited > 0.0, "entrée puis sortie");
		Check(exited - entered >= 15.0 && exited - entered < 15.2, "sortie exactement au délai minimal (15 s)");
	}

	// 6. Désactivé, entrées invalides, réglages absurdes.
	{
		LoadSheddingConfig off{};
		off.enabled = false;
		LoadShedder s(off);
		double now = 0.0;
		Check(Run(s, now, 30.0, 5.0) == 0 && !s.Active(), "désactivé : jamais de délestage");
		Check(s.SmoothedLoad() > 3.9, "désactivé : la charge reste mesurée (bornée à 4 budgets)");
		LoadShedder v;
		Check(v.OnTick(std::nan(""), kBudget, 1.0) == SheddingTransition::None
			&& v.OnTick(0.01, 0.0, 1.0) == SheddingTransition::None, "entrées invalides ignorées");
		LoadSheddingConfig bad{};
		bad.enterLoad = 0.5;
		bad.exitLoad = 0.9;
		bad.smoothing = -3.0;
		const LoadSheddingConfig fixed = SanitizeLoadShedding(bad);
		Check(fixed.exitLoad < fixed.enterLoad && fixed.smoothing > 0.0, "sortie ramenée sous l'entrée, lissage borné");
	}

	// 7. Premier tick très long (démarrage) : pas de délestage à tort.
	{
		LoadShedder s;
		double now = 1000.0;
		now += 2.0;
		(void)s.OnTick(2.0, kBudget, now); // 2 s = 40 budgets, compté pour 4
		Check(Run(s, now, 20.0, 0.3) == 0 && !s.Active(), "long premier tick absorbé");
	}

	if (s_failCount == 0)
		std::puts("[OK] load_shedding_tests");
	return s_failCount == 0 ? 0 : 1;
}
