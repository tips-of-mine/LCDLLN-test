// CMANGOS.16 (Phase 1b) — Tests ObjectAccessor : Register/Unregister/Find +
// concurrence multi-readers / writer exclusif.

#include "engine/server/shard/globals/ObjectAccessor.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
	using engine::server::shard::globals::ObjectAccessor;
	using engine::server::shard::globals::EntitySnapshot;

	bool TestBasicCrud()
	{
		ObjectAccessor acc;
		if (acc.Size() != 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] empty size != 0");
			return false;
		}

		EntitySnapshot a{};
		a.entityId = 42;
		a.name = "Alice";
		a.mapId = 0;
		a.isPlayer = true;
		if (!acc.Register(a) || acc.Size() != 1)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Register Alice failed");
			return false;
		}

		auto found = acc.Find(42);
		if (!found || found->name != "Alice")
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Find(42) returned unexpected");
			return false;
		}

		auto byName = acc.FindByName("ALICE");  // case-insensitive
		if (!byName || byName->entityId != 42)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] FindByName(ALICE) failed");
			return false;
		}

		if (!acc.Unregister(42) || acc.Size() != 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Unregister failed");
			return false;
		}

		// Register avec entityId=0 doit échouer.
		EntitySnapshot zero{};
		if (acc.Register(zero))
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Register(entityId=0) should fail");
			return false;
		}

		LOG_INFO(Core, "[ObjectAccessorTests] Basic CRUD OK");
		return true;
	}

	bool TestConcurrentReadersWriter()
	{
		ObjectAccessor acc;
		// Pré-charge 100 entités.
		for (uint64_t i = 1; i <= 100; ++i)
		{
			EntitySnapshot s{};
			s.entityId = i;
			s.name = "E" + std::to_string(i);
			acc.Register(s);
		}

		std::atomic<bool> stop{false};
		std::atomic<int> readerErrors{0};

		// 8 readers : Find en boucle.
		std::vector<std::thread> readers;
		for (int t = 0; t < 8; ++t)
		{
			readers.emplace_back([&]() {
				while (!stop.load())
				{
					for (uint64_t i = 1; i <= 100; ++i)
					{
						auto s = acc.Find(i);
						if (s && s->entityId != i)
							readerErrors.fetch_add(1);
					}
				}
			});
		}

		// Writer : Unregister + Register en boucle pendant 200ms.
		std::thread writer([&]() {
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
			while (std::chrono::steady_clock::now() < deadline)
			{
				for (uint64_t i = 1; i <= 100; ++i)
				{
					acc.Unregister(i);
					EntitySnapshot s{};
					s.entityId = i;
					s.name = "E" + std::to_string(i);
					acc.Register(s);
				}
			}
		});

		writer.join();
		stop.store(true);
		for (auto& r : readers) r.join();

		if (readerErrors.load() > 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] {} reader errors (race condition?)",
				readerErrors.load());
			return false;
		}
		LOG_INFO(Core, "[ObjectAccessorTests] Concurrent readers/writer OK");
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

	const bool ok = TestBasicCrud() && TestConcurrentReadersWriter();

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
