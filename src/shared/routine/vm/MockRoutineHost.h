#pragma once

// M101.2 — Hôte de test : enregistre une trace ordonnée des appels de la VM.
// Header-only. Sert aux tests headless (déterminisme = traces identiques).

#include <string>
#include <vector>

#include "src/shared/routine/vm/IRoutineHost.h"

namespace engine::routine::vm
{
	class MockRoutineHost : public IRoutineHost
	{
	public:
		// Valeurs de capteurs réglables par le test.
		float timeOfDayHours = 12.0f;
		bool  playerInRange = false;

		// Trace ordonnée des évènements/actions (preuve de déterminisme).
		std::vector<std::string> trace;
		// Détail des appels d'action (vérifications fines).
		std::vector<std::pair<uint64_t, bool>> openCalls;
		std::vector<int> seasonCalls;
		std::vector<int> weatherCalls;

		float GetTimeOfDayHours() const override { return timeOfDayHours; }
		bool  IsPlayerInRange(uint64_t, float) const override { return playerInRange; }

		void OpenInteractive(uint64_t id, bool open) override
		{
			openCalls.emplace_back(id, open);
		}
		void BroadcastSeason(int idx) override { seasonCalls.push_back(idx); }
		void BroadcastWeather(int idx) override { weatherCalls.push_back(idx); }

		void Trace(std::string_view label) override { trace.emplace_back(label); }
	};
}
