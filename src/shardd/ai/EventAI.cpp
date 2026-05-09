#include "engine/server/ai/EventAI.h"

namespace engine::server::ai
{
	void EvaluateEvents(const std::vector<EventAIRow>& rows,
		const EventAIContext& ctx, EventAIState& state,
		std::vector<uint32_t>& outFiredEventIds)
	{
		outFiredEventIds.clear();
		if (state.oneShotFired.size() < rows.size())
			state.oneShotFired.resize(rows.size(), false);

		for (size_t i = 0; i < rows.size(); ++i)
		{
			const auto& r = rows[i];
			if (r.oneShot && state.oneShotFired[i])
				continue;

			bool fire = false;
			switch (r.trigger)
			{
				case EventTrigger::Timer:
					if (ctx.nowMs >= state.nextTimerTickMs)
					{
						fire = true;
						// Re-arme le timer : prochaine fenetre [param1, param2] ms.
						const uint32_t avg = (r.param1 + r.param2) / 2;
						state.nextTimerTickMs = ctx.nowMs + (avg > 0 ? avg : 1000);
					}
					break;
				case EventTrigger::HpPctBelow:
					fire = (ctx.hpPct <= r.param1);
					break;
				case EventTrigger::OnAggro:
					fire = ctx.justEnteredCombat;
					break;
				case EventTrigger::OnSpawn:
					fire = ctx.justSpawned;
					break;
				case EventTrigger::OnDeath:
					fire = ctx.justDied;
					break;
				case EventTrigger::OnTargetHpPctBelow:
					fire = (ctx.targetHpPct <= r.param1);
					break;
			}

			if (fire)
			{
				outFiredEventIds.push_back(r.eventId);
				if (r.oneShot)
					state.oneShotFired[i] = true;
			}
		}
	}
}
