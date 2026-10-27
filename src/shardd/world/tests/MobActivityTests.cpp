// ETAPE4.1 — tests du sommeil et des classes de fréquence des mobs (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/MobActivity.h"

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

	using namespace engine::server::mob_activity;
	constexpr uint32_t kTickHz = 20u;
}

int main()
{
	const MobActivityConfig cfg = Sanitize(MobActivityConfig{}, kTickHz);

	// 1. Classes et intervalles : combat 10 Hz (2 ticks), repos et ambiance 5 Hz (4 ticks).
	Check(Classify(false, true) == MobSimClass::Combat, "mob occupé = combat");
	Check(Classify(true, true) == MobSimClass::Combat, "PNJ occupé = combat");
	Check(Classify(false, false) == MobSimClass::Calm, "mob hostile au repos = calme");
	Check(Classify(true, false) == MobSimClass::Ambient, "PNJ au repos = ambiance");
	Check(IntervalTicks(MobSimClass::Combat, cfg, kTickHz) == 2u, "combat : 2 ticks");
	Check(IntervalTicks(MobSimClass::Calm, cfg, kTickHz) == 4u, "calme : 4 ticks");
	Check(IntervalTicks(MobSimClass::Ambient, cfg, kTickHz) == 4u, "ambiance : 4 ticks");
	Check(IsBusy(true, false, false) && IsBusy(false, true, false) && IsBusy(false, false, true)
		&& !IsBusy(false, false, false), "occupé = cible, menace ou aggro/retour");

	// 2. Bornes : fréquences jamais au-dessus du tick ni du combat, valeurs absurdes corrigées.
	{
		MobActivityConfig wild{};
		wild.combatHz = 500u;
		wild.calmHz = 900u;
		wild.ambientHz = 0u;
		wild.wakeRadiusMeters = std::nanf("");
		wild.sleepDelaySeconds = -5.0f;
		const MobActivityConfig s = Sanitize(wild, kTickHz);
		Check(s.combatHz == kTickHz && s.calmHz == kTickHz && s.ambientHz == 1u, "fréquences bornées");
		Check(s.wakeRadiusMeters == 0.0f && s.sleepDelaySeconds == 0.0f, "rayon NaN → 0, délai ≥ 0");
		Check(IntervalTicks(MobSimClass::Ambient, s, kTickHz) == kTickHz, "1 Hz = 20 ticks");
	}

	// 3. Rattrapage : le pas couvre le temps réellement écoulé, borné.
	{
		const uint32_t calm = IntervalTicks(MobSimClass::Calm, cfg, kTickHz);
		Check(std::fabs(CatchUpSeconds(0u, 100u, calm, kTickHz, cfg) - 0.2f) < 1e-5f, "jamais traité : intervalle nominal");
		Check(std::fabs(CatchUpSeconds(96u, 100u, calm, kTickHz, cfg) - 0.2f) < 1e-5f, "à l'heure : 0,2 s");
		Check(std::fabs(CatchUpSeconds(90u, 100u, calm, kTickHz, cfg) - 0.5f) < 1e-5f, "en retard : 0,5 s couverts");
		Check(std::fabs(CatchUpSeconds(10u, 1000u, calm, kTickHz, cfg) - 1.0f) < 1e-5f, "longue attente : 1 s au plus");
		// Vitesse au sol identique : 10 Hz × 0,1 s = 5 Hz × 0,2 s = 1 s couverte par seconde.
		const uint32_t combat = IntervalTicks(MobSimClass::Combat, cfg, kTickHz);
		const float perSecondCombat = CatchUpSeconds(98u, 100u, combat, kTickHz, cfg) * 10.0f;
		const float perSecondCalm = CatchUpSeconds(96u, 100u, calm, kTickHz, cfg) * 5.0f;
		Check(std::fabs(perSecondCombat - 1.0f) < 1e-5f && std::fabs(perSecondCalm - 1.0f) < 1e-5f,
			"même distance parcourue par seconde quelle que soit la fréquence");
	}

	// 4. Sommeil : 3 s sans joueur → endormi ; un joueur proche → réveil.
	{
		SleepState st{};
		const uint32_t spawn = 1000u;
		Check(UpdateSleep(st, false, false, spawn + 20u, spawn, kTickHz, cfg) == SleepTransition::None, "1 s après l'apparition : éveillé");
		Check(UpdateSleep(st, false, false, spawn + 60u, spawn, kTickHz, cfg) == SleepTransition::FellAsleep, "3 s sans joueur : endormi");
		Check(st.asleep, "état endormi");
		Check(UpdateSleep(st, false, false, spawn + 80u, spawn, kTickHz, cfg) == SleepTransition::None, "reste endormi");
		Check(UpdateSleep(st, true, false, spawn + 100u, spawn, kTickHz, cfg) == SleepTransition::WokeUp, "joueur proche : réveil");
		Check(!st.asleep && st.lastPlayerNearTick == spawn + 100u, "éveillé, dernier joueur noté");
		Check(UpdateSleep(st, false, false, spawn + 140u, spawn, kTickHz, cfg) == SleepTransition::None, "2 s après le départ : encore éveillé");
		Check(UpdateSleep(st, false, false, spawn + 160u, spawn, kTickHz, cfg) == SleepTransition::FellAsleep, "3 s après le départ : endormi");
	}

	// 4b. Retour à zéro du compteur de ticks : le délai reste juste.
	{
		SleepState st{};
		const uint32_t spawn = 0xFFFFFFF0u;
		Check(UpdateSleep(st, false, false, spawn + 70u, spawn, kTickHz, cfg) == SleepTransition::FellAsleep,
			"compteur repassé par zéro : endormi après 3,5 s");
	}

	// 5. Un mob occupé ne dort jamais et se réveille sur-le-champ.
	{
		SleepState st{};
		Check(UpdateSleep(st, false, true, 5000u, 0u, kTickHz, cfg) == SleepTransition::None && !st.asleep, "en combat : jamais endormi");
		st.asleep = true;
		Check(WakeIfBusy(st, true) && !st.asleep, "dégâts reçus endormi : réveil immédiat");
		Check(!WakeIfBusy(st, true), "déjà éveillé : rien");
		st.asleep = true;
		Check(!WakeIfBusy(st, false) && st.asleep, "pas occupé : reste endormi");
	}

	// 6. Rayon 0 : sommeil désactivé.
	{
		MobActivityConfig off = cfg;
		off.wakeRadiusMeters = 0.0f;
		SleepState st{};
		Check(UpdateSleep(st, false, false, 100000u, 0u, kTickHz, off) == SleepTransition::None && !st.asleep, "rayon 0 : jamais endormi");
		st.asleep = true;
		Check(UpdateSleep(st, false, false, 100001u, 0u, kTickHz, off) == SleepTransition::WokeUp, "rayon 0 : un mob endormi se réveille");
	}

	// 7. Vérification du voisinage étalée : chaque entité une fois par seconde, pas toutes au même tick.
	{
		uint32_t perTick[kTickHz] = {};
		for (uint64_t id = 1; id <= 200; ++id)
		{
			uint32_t hits = 0;
			for (uint32_t t = 0; t < kTickHz; ++t)
			{
				if (IsWakeCheckTick(id, 7000u + t, kTickHz))
				{
					++hits;
					++perTick[t];
				}
			}
			Check(hits == 1u, "une vérification par seconde et par entité");
		}
		bool even = true;
		for (uint32_t t = 0; t < kTickHz; ++t)
			even = even && perTick[t] == 10u;
		Check(even, "200 entités : 10 vérifications par tick, charge répartie");
	}

	if (s_failCount == 0)
		std::puts("[OK] mob_activity_tests");
	return s_failCount == 0 ? 0 : 1;
}
