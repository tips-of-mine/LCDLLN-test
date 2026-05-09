// CMANGOS.05 (Phase 2.05d) — Tests VMapStreamer + ManagedModel.
// Loader callback simule en RAM (un dict in-memory de blobs).

#include "engine/server/shard/vmap/VMapFormat.h"
#include "engine/server/shard/vmap/VMapStreamer.h"
#include "engine/core/Log.h"

#include <chrono>
#include <unordered_map>
#include <vector>

namespace
{
	using engine::math::Vec3;
	using engine::server::shard::vmap::AABB;
	using engine::server::shard::vmap::ManagedModel;
	using engine::server::shard::vmap::VMapStreamer;
	using engine::server::shard::vmap::VMapStreamerConfig;
	using engine::server::shard::vmap::VMapTile;
	using engine::server::shard::vmap::VMapTri;
	using namespace std::chrono_literals;
	using TP = ManagedModel::TimePoint;

	/// Fabrique un tile minimal (1 triangle au plan z=0).
	VMapTile MakeMiniTile()
	{
		VMapTile t;
		t.bbox.min = Vec3(0, 0, 0);
		t.bbox.max = Vec3(1, 0, 1);
		t.vertices = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1) };
		t.triangles = { VMapTri{0, 1, 2} };
		return t;
	}

	/// Loader in-memory : map<key, blob>.
	struct InMemoryStore
	{
		std::unordered_map<std::string, std::vector<uint8_t>> blobs;
		void Add(std::string key, const VMapTile& tile)
		{
			blobs[std::move(key)] = engine::server::shard::vmap::EncodeVMapTile(tile);
		}
		auto Loader()
		{
			return [this](std::string_view key) -> std::vector<uint8_t> {
				auto it = blobs.find(std::string(key));
				return (it == blobs.end()) ? std::vector<uint8_t>{} : it->second;
			};
		}
	};

	bool TestAcquireLoadsAndIncrefs()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());

		auto* m = s.Acquire("a");
		if (!m) { LOG_ERROR(Core, "[VMapStreamerTests] Acquire returned null"); return false; }
		if (m->RefCount() != 1) return false;
		if (s.LoadedTileCount() != 1) return false;
		if (!m->Manager().IsLoaded()) return false;
		LOG_INFO(Core, "[VMapStreamerTests] Acquire loads + refcount=1 OK");
		return true;
	}

	bool TestAcquireSecondTimeJustBumps()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());

		s.Acquire("a");
		auto* m = s.Acquire("a");
		if (m->RefCount() != 2)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] expected refcount=2, got {}", m->RefCount());
			return false;
		}
		if (s.LoadedTileCount() != 1)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] should still be 1 tile loaded");
			return false;
		}
		LOG_INFO(Core, "[VMapStreamerTests] Acquire 2x increfs without reload OK");
		return true;
	}

	bool TestUnknownKeyFails()
	{
		InMemoryStore store;
		VMapStreamer s;
		s.SetLoader(store.Loader());
		auto* m = s.Acquire("bogus");
		if (m != nullptr)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] expected nullptr for unknown key");
			return false;
		}
		LOG_INFO(Core, "[VMapStreamerTests] unknown key returns nullptr OK");
		return true;
	}

	bool TestReleaseStartsTimerThenTickUnloads()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());

		VMapStreamerConfig cfg;
		cfg.releaseDelay = 5s;
		s.SetConfig(cfg);

		const TP t0{};
		s.Acquire("a");
		s.Release("a", t0);
		// Timer demarre a t0. Tick a t0+3s : pas encore expire.
		if (s.Tick(t0 + 3s) != 0) return false;
		if (s.LoadedTileCount() != 1) return false;

		// Tick a t0+10s : expire → unload.
		const auto freed = s.Tick(t0 + 10s);
		if (freed != 1)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] expected 1 freed, got {}", freed);
			return false;
		}
		if (s.LoadedTileCount() != 0) return false;
		LOG_INFO(Core, "[VMapStreamerTests] Release+Tick unloads after delay OK");
		return true;
	}

	bool TestReAcquireBeforeUnloadCancelsTimer()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());

		VMapStreamerConfig cfg;
		cfg.releaseDelay = 5s;
		s.SetConfig(cfg);

		const TP t0{};
		s.Acquire("a");
		s.Release("a", t0);
		// 3s plus tard : pas encore expire. Re-acquire → timer reset.
		s.Acquire("a");
		// Tick 100s plus tard : ne doit RIEN decharger (refcount=1).
		if (s.Tick(t0 + 100s) != 0)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] re-Acquire should cancel unload timer");
			return false;
		}
		if (s.LoadedTileCount() != 1) return false;
		LOG_INFO(Core, "[VMapStreamerTests] re-Acquire cancels timer OK");
		return true;
	}

	bool TestReleaseUnknownKeyNoOp()
	{
		VMapStreamer s;
		const TP t0{};
		// Release sur cle inconnue → silent no-op.
		s.Release("nope", t0);
		if (s.LoadedTileCount() != 0) return false;
		LOG_INFO(Core, "[VMapStreamerTests] Release unknown key no-op OK");
		return true;
	}

	bool TestFindReadOnly()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());
		s.Acquire("a");
		const auto* m = s.Find("a");
		if (!m) return false;
		if (m->RefCount() != 1)
		{
			LOG_ERROR(Core, "[VMapStreamerTests] Find should not bump refcount");
			return false;
		}
		LOG_INFO(Core, "[VMapStreamerTests] Find read-only OK");
		return true;
	}

	bool TestClear()
	{
		InMemoryStore store;
		store.Add("a", MakeMiniTile());
		store.Add("b", MakeMiniTile());
		VMapStreamer s;
		s.SetLoader(store.Loader());
		s.Acquire("a");
		s.Acquire("b");
		s.Clear();
		if (s.LoadedTileCount() != 0) return false;
		LOG_INFO(Core, "[VMapStreamerTests] Clear OK");
		return true;
	}

	bool TestNoLoaderFails()
	{
		VMapStreamer s;
		// Pas de SetLoader → Acquire retourne nullptr.
		auto* m = s.Acquire("a");
		if (m != nullptr) return false;
		LOG_INFO(Core, "[VMapStreamerTests] no loader → nullptr OK");
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

	const bool ok = TestAcquireLoadsAndIncrefs()
		&& TestAcquireSecondTimeJustBumps()
		&& TestUnknownKeyFails()
		&& TestReleaseStartsTimerThenTickUnloads()
		&& TestReAcquireBeforeUnloadCancelsTimer()
		&& TestReleaseUnknownKeyNoOp()
		&& TestFindReadOnly()
		&& TestClear()
		&& TestNoLoaderFails();

	if (ok)
		LOG_INFO(Core, "[VMapStreamerTests] ALL OK");
	else
		LOG_ERROR(Core, "[VMapStreamerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
