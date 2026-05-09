#pragma once
// CMANGOS.07 (Phase 3.07a) — EventAI : machine a etats data-driven pour
// l'IA des creatures (cmangos pattern). Liste d'EventAIRow declenches
// par event types (TIMER / HP_PCT / ON_AGGRO / ON_SPAWN / etc.) chacun
// associe a une action (CAST / SAY / FLEE / SUMMON / ...).
//
// Cette PR : data structures + evaluateur des conditions de declenchement.
// Pas encore de side-effects runtime (action dispatcher viendra avec
// l'integration AI complete + CMANGOS.14 DBScripts).

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::ai
{
	using CreatureEntry = uint32_t;

	enum class EventTrigger : uint8_t
	{
		Timer       = 0,   ///< Toutes les param1..param2 ms (random in range).
		HpPctBelow  = 1,   ///< HP% tombe sous param1 (event one-shot).
		OnAggro     = 2,   ///< Premiere fois en combat.
		OnSpawn     = 3,
		OnDeath     = 4,
		OnTargetHpPctBelow = 5,
	};

	enum class EventAction : uint8_t
	{
		Say         = 0,
		Cast        = 1,
		Flee        = 2,
		Summon      = 3,
		Despawn     = 4,
		Custom      = 5,
	};

	struct EventAIRow
	{
		uint32_t      eventId      = 0;
		EventTrigger  trigger      = EventTrigger::Timer;
		uint32_t      param1       = 0;       ///< depend du trigger
		uint32_t      param2       = 0;
		EventAction   action       = EventAction::Custom;
		uint32_t      actionParam1 = 0;       ///< depend de l'action
		uint32_t      actionParam2 = 0;
		std::string   actionString;            ///< pour Say
		bool          oneShot      = false;
	};

	/// Etat runtime d'une creature : timers + flags one-shot.
	struct EventAIState
	{
		uint64_t                  nextTimerTickMs = 0;
		std::vector<bool>         oneShotFired;       ///< par eventId index
	};

	/// Tester si \p row doit se declencher selon \p ctx (HP%, target HP%,
	/// state d'aggro, etc.). Pure : pas de side-effect.
	struct EventAIContext
	{
		uint32_t hpPct           = 100;     ///< 0..100
		uint32_t targetHpPct     = 100;
		bool     justEnteredCombat = false;
		bool     justSpawned       = false;
		bool     justDied          = false;
		uint64_t nowMs             = 0;
	};

	/// Retourne les eventIds qui ont match dans \p out. State updated
	/// pour les one-shot (marque comme fired) et pour les timers (next
	/// tick decale).
	void EvaluateEvents(const std::vector<EventAIRow>& rows,
		const EventAIContext& ctx, EventAIState& state,
		std::vector<uint32_t>& outFiredEventIds);
}
