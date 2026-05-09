#include "src/masterd/quests/QuestState.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::quests::QuestOpResult;
	using engine::server::quests::QuestStateTracker;
	using engine::server::quests::QuestStatus;

	bool TestHappyPath()
	{
		QuestStateTracker t;
		if (t.Get(1, 100) != QuestStatus::None) return false;
		if (t.Accept(1, 100) != QuestOpResult::OK) return false;
		if (t.Get(1, 100) != QuestStatus::Accepted) return false;
		if (t.Complete(1, 100) != QuestOpResult::OK) return false;
		if (t.Reward(1, 100) != QuestOpResult::OK) return false;
		if (t.Get(1, 100) != QuestStatus::Rewarded) return false;
		LOG_INFO(Core, "[QuestStateTests] happy path OK");
		return true;
	}

	bool TestCannotCompleteNotAccepted()
	{
		QuestStateTracker t;
		if (t.Complete(1, 100) != QuestOpResult::WrongStatus) return false;
		t.Accept(1, 100);
		if (t.Reward(1, 100) != QuestOpResult::WrongStatus) return false;  // skip Complete
		LOG_INFO(Core, "[QuestStateTests] state machine guards OK");
		return true;
	}

	bool TestFailFromAccepted()
	{
		QuestStateTracker t;
		t.Accept(1, 100);
		if (t.Fail(1, 100) != QuestOpResult::OK) return false;
		if (t.Get(1, 100) != QuestStatus::Failed) return false;
		LOG_INFO(Core, "[QuestStateTests] fail OK");
		return true;
	}

	bool TestPerAccount()
	{
		QuestStateTracker t;
		t.Accept(1, 100);
		t.Accept(2, 100);
		if (t.Get(1, 100) != QuestStatus::Accepted) return false;
		if (t.Get(2, 100) != QuestStatus::Accepted) return false;
		t.Complete(1, 100);
		if (t.Get(1, 100) != QuestStatus::Completed) return false;
		if (t.Get(2, 100) != QuestStatus::Accepted) return false;  // isolation
		LOG_INFO(Core, "[QuestStateTests] per-account isolation OK");
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

	const bool ok = TestHappyPath() && TestCannotCompleteNotAccepted()
		&& TestFailFromAccepted() && TestPerAccount();
	if (ok) LOG_INFO(Core, "[QuestStateTests] ALL OK");
	else LOG_ERROR(Core, "[QuestStateTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
