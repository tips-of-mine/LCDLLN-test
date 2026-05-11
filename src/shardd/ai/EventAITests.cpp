// CMANGOS.07 (Phase 3.07a) — Tests EventAI evaluator.

#include "src/shardd/ai/EventAI.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::ai::EvaluateEvents;
	using engine::server::ai::EventAction;
	using engine::server::ai::EventAIContext;
	using engine::server::ai::EventAIRow;
	using engine::server::ai::EventAIState;
	using engine::server::ai::EventTrigger;

	bool TestOnSpawnFires()
	{
		std::vector<EventAIRow> rows;
		EventAIRow r;
		r.eventId = 1;
		r.trigger = EventTrigger::OnSpawn;
		r.oneShot = true;
		rows.push_back(r);

		EventAIState st;
		EventAIContext ctx;
		ctx.justSpawned = true;
		std::vector<uint32_t> fired;
		EvaluateEvents(rows, ctx, st, fired);
		if (fired.size() != 1 || fired[0] != 1) return false;

		// 2eme tick sans justSpawned → aucun event.
		ctx.justSpawned = false;
		EvaluateEvents(rows, ctx, st, fired);
		if (!fired.empty()) return false;

		// Si on remet justSpawned → ne re-fire pas (oneShot).
		ctx.justSpawned = true;
		EvaluateEvents(rows, ctx, st, fired);
		if (!fired.empty()) return false;
		LOG_INFO(Core, "[EventAITests] OnSpawn one-shot OK");
		return true;
	}

	bool TestHpPctBelow()
	{
		std::vector<EventAIRow> rows;
		EventAIRow r;
		r.eventId = 10;
		r.trigger = EventTrigger::HpPctBelow;
		r.param1 = 30;  // sous 30%
		rows.push_back(r);

		EventAIState st;
		EventAIContext ctx;
		std::vector<uint32_t> fired;

		ctx.hpPct = 50;
		EvaluateEvents(rows, ctx, st, fired);
		if (!fired.empty()) return false;

		ctx.hpPct = 25;
		EvaluateEvents(rows, ctx, st, fired);
		if (fired.size() != 1) return false;
		LOG_INFO(Core, "[EventAITests] HpPctBelow OK");
		return true;
	}

	bool TestTimer()
	{
		std::vector<EventAIRow> rows;
		EventAIRow r;
		r.eventId = 20;
		r.trigger = EventTrigger::Timer;
		r.param1 = 1000; r.param2 = 1000;  // 1s pile
		rows.push_back(r);

		EventAIState st;
		st.nextTimerTickMs = 0;
		EventAIContext ctx;
		std::vector<uint32_t> fired;

		ctx.nowMs = 500;
		EvaluateEvents(rows, ctx, st, fired);
		// Premier tick : nextTimerTickMs=0 ≤ 500 → fire.
		if (fired.size() != 1) return false;
		// nextTimerTickMs reprogrammé à 500 + 1000 = 1500.

		ctx.nowMs = 1000;
		EvaluateEvents(rows, ctx, st, fired);
		if (!fired.empty()) return false;

		ctx.nowMs = 1600;
		EvaluateEvents(rows, ctx, st, fired);
		if (fired.size() != 1) return false;
		LOG_INFO(Core, "[EventAITests] Timer OK");
		return true;
	}

	bool TestMultipleEvents()
	{
		std::vector<EventAIRow> rows;
		EventAIRow a; a.eventId = 1; a.trigger = EventTrigger::OnAggro; rows.push_back(a);
		EventAIRow b; b.eventId = 2; b.trigger = EventTrigger::HpPctBelow; b.param1 = 50; rows.push_back(b);

		EventAIState st;
		EventAIContext ctx;
		ctx.justEnteredCombat = true;
		ctx.hpPct = 30;
		std::vector<uint32_t> fired;
		EvaluateEvents(rows, ctx, st, fired);
		if (fired.size() != 2) return false;
		LOG_INFO(Core, "[EventAITests] multiple events OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestOnSpawnFires() && TestHpPctBelow()
		&& TestTimer() && TestMultipleEvents();

	if (ok) LOG_INFO(Core, "[EventAITests] ALL OK");
	else LOG_ERROR(Core, "[EventAITests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
