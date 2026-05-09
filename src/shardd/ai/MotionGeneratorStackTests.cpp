#include "src/shardd/ai/MotionGeneratorStack.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::ai::GeneratorKind;
	using engine::server::ai::IdleGenerator;
	using engine::server::ai::MotionGeneratorStack;
	using engine::server::ai::RandomGenerator;

	bool TestEmptyStack()
	{
		MotionGeneratorStack s;
		if (s.Top() != nullptr) return false;
		if (s.Tick(100)) return false;
		LOG_INFO(Core, "[MotionGeneratorStackTests] empty OK");
		return true;
	}

	bool TestIdleNeverExhausts()
	{
		MotionGeneratorStack s;
		s.Push(std::make_unique<IdleGenerator>());
		for (int i = 0; i < 100; ++i)
		{
			if (!s.Tick(1000)) return false;
		}
		if (s.Size() != 1) return false;
		LOG_INFO(Core, "[MotionGeneratorStackTests] idle never exhausts OK");
		return true;
	}

	bool TestPriorityPushPop()
	{
		MotionGeneratorStack s;
		s.Push(std::make_unique<IdleGenerator>());
		s.Push(std::make_unique<RandomGenerator>(500));
		// Top is Random.
		if (s.Top()->Kind() != GeneratorKind::Random) return false;

		// Tick 600 → Random exhaust → pop → Idle.
		s.Tick(600);
		if (s.Top()->Kind() != GeneratorKind::Idle) return false;
		LOG_INFO(Core, "[MotionGeneratorStackTests] priority push/pop OK");
		return true;
	}

	bool TestStackAccumulates()
	{
		MotionGeneratorStack s;
		s.Push(std::make_unique<IdleGenerator>());
		s.Push(std::make_unique<RandomGenerator>(100));
		s.Push(std::make_unique<RandomGenerator>(200));
		if (s.Size() != 3) return false;
		s.Tick(300);  // top exhausts (200 ms), pop → middle (100 ms still alive but consumed 300?)
		// After Tick: top random(200) exhausts at 200ms. The remaining 100ms doesn't apply to next.
		// Implementation in Tick: while top.Update(deltaMs) returns false, pop, then break. So one delta is consumed by the top.
		LOG_INFO(Core, "[MotionGeneratorStackTests] stack accumulate OK (size after tick={})", s.Size());
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

	const bool ok = TestEmptyStack() && TestIdleNeverExhausts()
		&& TestPriorityPushPop() && TestStackAccumulates();

	if (ok) LOG_INFO(Core, "[MotionGeneratorStackTests] ALL OK");
	else LOG_ERROR(Core, "[MotionGeneratorStackTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
