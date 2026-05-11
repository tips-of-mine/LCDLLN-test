#include "src/shared/messager/Messager.h"
#include "src/shared/core/Log.h"

#include <thread>
#include <atomic>
#include <vector>

namespace
{
	using engine::server::messager::Messager;

	bool TestSingleThreaded()
	{
		Messager<int> m;
		m.Post(1);
		m.Post(2);
		m.Post(3);
		if (m.Size() != 3) return false;

		std::vector<int> drained;
		auto n = m.Drain(drained);
		if (n != 3 || drained.size() != 3) return false;
		if (drained[0] != 1 || drained[1] != 2 || drained[2] != 3) return false;
		if (m.Size() != 0) return false;

		std::vector<int> drained2;
		if (m.Drain(drained2) != 0) return false;
		LOG_INFO(Core, "[MessagerTests] single thread OK");
		return true;
	}

	bool TestMultiProducer()
	{
		Messager<uint64_t> m;
		const int producers = 4;
		const int perProducer = 1000;

		std::vector<std::thread> threads;
		for (int p = 0; p < producers; ++p)
		{
			threads.emplace_back([&m, p]() {
				for (int i = 0; i < perProducer; ++i)
					m.Post(static_cast<uint64_t>(p) * 100000 + i);
			});
		}
		for (auto& t : threads) t.join();

		std::vector<uint64_t> drained;
		m.Drain(drained);
		if (drained.size() != static_cast<size_t>(producers * perProducer)) return false;
		LOG_INFO(Core, "[MessagerTests] multi producer OK");
		return true;
	}

	bool TestMoveOnly()
	{
		Messager<std::vector<int>> m;
		std::vector<int> v = {1, 2, 3};
		m.Post(std::move(v));
		std::vector<std::vector<int>> drained;
		m.Drain(drained);
		if (drained.size() != 1) return false;
		if (drained[0].size() != 3) return false;
		LOG_INFO(Core, "[MessagerTests] move-only payload OK");
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

	const bool ok = TestSingleThreaded() && TestMultiProducer() && TestMoveOnly();
	if (ok) LOG_INFO(Core, "[MessagerTests] ALL OK");
	else LOG_ERROR(Core, "[MessagerTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
