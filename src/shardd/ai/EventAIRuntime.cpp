#include "src/shardd/ai/EventAIRuntime.h"

namespace engine::server::ai
{
	/// Charge deux scripts V1 hardcodes :
	///   - eventId 1 : OnSpawn one-shot (fire au premier Tick), action Say
	///   - eventId 2 : Timer 30s..30s (fire periodiquement), action Say
	/// Pas de loader DB pour cette PR : on prouve juste que le path tick
	/// EventAI -> EvaluateEvents -> mise a jour d'etat est exerce en prod.
	void EventAIRuntime::SeedV1Events()
	{
		m_rows.clear();
		m_state = EventAIState{};
		m_totalFires = 0;
		m_firstTick = true;

		EventAIRow onSpawn;
		onSpawn.eventId      = 1;
		onSpawn.trigger      = EventTrigger::OnSpawn;
		onSpawn.action       = EventAction::Say;
		onSpawn.actionString = "Greetings, traveler.";
		onSpawn.oneShot      = true;
		m_rows.push_back(onSpawn);

		EventAIRow periodicSay;
		periodicSay.eventId      = 2;
		periodicSay.trigger      = EventTrigger::Timer;
		periodicSay.param1       = 30000;   // 30s min
		periodicSay.param2       = 30000;   // 30s max
		periodicSay.action       = EventAction::Say;
		periodicSay.actionString = "I keep an eye on these woods.";
		periodicSay.oneShot      = false;
		m_rows.push_back(periodicSay);
	}

	/// Construit un EventAIContext minimal (HP=100, hors combat) et delegue
	/// a EvaluateEvents(). Le flag justSpawned n'est vrai qu'au premier
	/// tick, pour declencher l'event OnSpawn one-shot.
	std::size_t EventAIRuntime::Tick(uint64_t nowMs)
	{
		if (m_rows.empty())
			return 0;

		EventAIContext ctx;
		ctx.hpPct             = 100;
		ctx.targetHpPct       = 100;
		ctx.justEnteredCombat = false;
		ctx.justSpawned       = m_firstTick;
		ctx.justDied          = false;
		ctx.nowMs             = nowMs;

		std::vector<std::uint32_t> firedIds;
		EvaluateEvents(m_rows, ctx, m_state, firedIds);

		m_firstTick = false;
		m_totalFires += firedIds.size();
		return firedIds.size();
	}
}
