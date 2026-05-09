#include "engine/server/util/ServerUtil.h"
#include "engine/core/Log.h"

namespace
{
	using namespace engine::server::util;

	bool TestDeterministicSeed()
	{
		DeterministicRng a(42);
		DeterministicRng b(42);
		for (int i = 0; i < 50; ++i)
			if (a.IntRange(0, 1000) != b.IntRange(0, 1000)) return false;
		LOG_INFO(Core, "[UtilTests] deterministic RNG OK");
		return true;
	}

	bool TestUnitRange()
	{
		DeterministicRng r(7);
		for (int i = 0; i < 100; ++i)
		{
			float u = r.Unit();
			if (u < 0.0f || u >= 1.0f) return false;
		}
		LOG_INFO(Core, "[UtilTests] unit range OK");
		return true;
	}

	bool TestSplit()
	{
		auto v = Split("a,b,c", ',');
		if (v.size() != 3) return false;
		if (v[0] != "a" || v[1] != "b" || v[2] != "c") return false;

		auto v2 = Split(",a,", ',');
		if (v2.size() != 3) return false;
		if (!v2[0].empty()) return false;
		if (v2[1] != "a") return false;
		if (!v2[2].empty()) return false;
		LOG_INFO(Core, "[UtilTests] split OK");
		return true;
	}

	bool TestParseU64()
	{
		auto a = ParseU64("12345");
		if (!a || *a != 12345) return false;
		auto b = ParseU64("0");
		if (!b || *b != 0) return false;
		if (ParseU64("").has_value()) return false;
		if (ParseU64("12a3").has_value()) return false;
		if (ParseU64("-1").has_value()) return false;
		LOG_INFO(Core, "[UtilTests] parse u64 OK");
		return true;
	}

	bool TestFormatDuration()
	{
		if (FormatDurationMs(0) != "00:00:00") return false;
		if (FormatDurationMs(1500) != "00:00:01") return false;
		if (FormatDurationMs(60'000) != "00:01:00") return false;
		if (FormatDurationMs(3'661'000) != "01:01:01") return false;
		LOG_INFO(Core, "[UtilTests] format duration OK");
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

	const bool ok = TestDeterministicSeed() && TestUnitRange() && TestSplit()
	             && TestParseU64() && TestFormatDuration();
	if (ok) LOG_INFO(Core, "[UtilTests] ALL OK");
	else LOG_ERROR(Core, "[UtilTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
