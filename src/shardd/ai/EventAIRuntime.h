#pragma once
// Wave 6 — Wrapper runtime EventAI : agrege l'evaluateur header-only
// (EventAI.h / EvaluateEvents) avec un etat de creature seede au boot
// du shardd. V1 minimaliste : 1 a 2 EventAIRow hardcodes (creature qui
// dit une ligne toutes les N secondes) pour prouver que le systeme
// tourne en production. Les futurs PR brancheront un loader DB-driven
// (table event_ai_scripts) qui remplira m_rows par creature.
//
// L'objectif principal de cette PR : instanciation + tick periodique
// dans la boucle main_linux.cpp pour que le path "EventAI evaluator"
// soit reellement exerce a chaque seconde, plutot que de rester un
// header-only orphelin.

#include "src/shardd/ai/EventAI.h"

#include <cstdint>
#include <vector>

namespace engine::server::ai
{
	/// Wrapper minimaliste autour de EvaluateEvents() : detient les rows
	/// seedes au boot + l'etat (timers + flags one-shot) pour une creature
	/// fictive V1. Future iteration : map<CreatureGuid, EventAIState> +
	/// rows charges depuis la DB.
	class EventAIRuntime
	{
	public:
		EventAIRuntime() = default;

		/// Charge 1-2 scripts EventAI hardcodes (V1) : verifie que le wiring
		/// est fonctionnel cote shardd. Doit etre appele avant le premier
		/// Tick(). Idempotent : peut etre rappele pour reset l'etat (utile
		/// en tests, jamais en prod).
		void SeedV1Events();

		/// Evalue les rows seedes contre un contexte minimal (creature
		/// fictive, HP=100, hors combat). Retourne le nombre d'events firees
		/// ce tick. Effet de bord : met a jour m_state (timers + flags
		/// one-shot) et incremente m_totalFires.
		///
		/// \param nowMs Horloge wall-clock en millisecondes (system_clock
		///   typiquement). Utilisee pour evaluer EventTrigger::Timer.
		std::size_t Tick(uint64_t nowMs);

		/// Nombre total d'events firees depuis le boot (cumul sur toute la
		/// duree de vie du runtime). Util pour le log periodique
		/// "[EventAI] tick : N events fired".
		std::uint64_t TotalFires() const noexcept { return m_totalFires; }

		/// Nombre de rows seedes (V1 : 1 ou 2).
		std::size_t RowCount() const noexcept { return m_rows.size(); }

	private:
		std::vector<EventAIRow> m_rows;
		EventAIState            m_state;
		std::uint64_t           m_totalFires   = 0;
		bool                    m_firstTick    = true;   ///< Pour declencher OnSpawn une fois.
	};
}
