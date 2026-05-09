// CMANGOS.13 (Phase 1a) — Tests SQLStorage<T> : Load + Find + Size + iteration.
// Pattern DbLayerTests : skip silencieux si db.host non configuré.

#include "src/shared/db/SQLStorage.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	/// Struct de test mappant `phase_1a_test_storage`.
	struct TestEntry
	{
		uint32_t entry = 0;
		std::string name;
		int32_t value = 0;
	};

	/// Mapper MYSQL_ROW → TestEntry. row[0]=entry, row[1]=name, row[2]=value.
	TestEntry MapRow(char** row)
	{
		TestEntry e{};
		e.entry = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
		e.name  = row[1] ? row[1] : "";
		e.value = std::atoi(row[2]);
		return e;
	}

	bool CheckLoadFindIterate(engine::server::db::ConnectionPool& pool)
	{
		engine::server::db::SQLStorage<TestEntry> storage;
		const bool ok = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (!ok)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Load() returned false");
			return false;
		}
		if (storage.Size() != 3u)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Size expected 3, got {}", storage.Size());
			return false;
		}
		const TestEntry* alpha = storage.Find(1);
		if (!alpha || alpha->name != "alpha" || alpha->value != 100)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(1) failed");
			return false;
		}
		const TestEntry* gamma = storage.Find(3);
		if (!gamma || gamma->name != "gamma" || gamma->value != 300)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(3) failed");
			return false;
		}
		if (storage.Find(999) != nullptr)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(999) should return nullptr");
			return false;
		}
		// Iteration : les 3 entrées doivent toutes être visitées.
		size_t count = 0;
		for (const auto& [pk, entry] : storage)
		{
			(void)pk; (void)entry;
			++count;
		}
		if (count != 3u)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Iteration count expected 3, got {}", count);
			return false;
		}
		LOG_INFO(Core, "[SQLStorageTests] Load+Find+Size+Iterate OK");
		return true;
	}

	bool CheckDoubleLoadRejected(engine::server::db::ConnectionPool& pool)
	{
		engine::server::db::SQLStorage<TestEntry> storage;
		const bool ok1 = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (!ok1)
		{
			LOG_ERROR(Core, "[SQLStorageTests] First Load failed");
			return false;
		}
		const bool ok2 = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (ok2)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Second Load should have returned false");
			return false;
		}
		LOG_INFO(Core, "[SQLStorageTests] Double-load rejected OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[SQLStorageTests] db.host not set, skipping (smoke test optional without DB)");
		engine::core::Log::Shutdown();
		return 0;
	}

	engine::server::db::ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SQLStorageTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	const bool ok = CheckLoadFindIterate(pool) && CheckDoubleLoadRejected(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
