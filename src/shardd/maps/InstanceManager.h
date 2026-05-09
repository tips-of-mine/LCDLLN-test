#pragma once
// CMANGOS.19 (Phase 3.19a) — InstanceManager : ID per-map + lifecycle
// (Created / Active / Idle / Despawned). Header-only, in-memory.

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace engine::server::maps
{
	using MapId      = uint32_t;
	using InstanceId = uint64_t;

	enum class InstanceState : uint8_t
	{
		Created    = 0,
		Active     = 1,
		Idle       = 2,
		Despawned  = 3,
	};

	struct InstanceInfo
	{
		InstanceId    instanceId  = 0;
		MapId         mapId       = 0;
		InstanceState state       = InstanceState::Created;
		uint64_t      createdTsMs = 0;
		uint64_t      lastActiveTsMs = 0;
	};

	class InstanceManager
	{
	public:
		InstanceManager() = default;

		/// Cree une nouvelle instance pour \p mapId. Retourne l'ID alloue
		/// (monotone strictement croissant).
		InstanceId Create(MapId mapId, uint64_t nowMs)
		{
			const InstanceId id = m_nextId++;
			InstanceInfo info;
			info.instanceId  = id;
			info.mapId       = mapId;
			info.state       = InstanceState::Active;
			info.createdTsMs = nowMs;
			info.lastActiveTsMs = nowMs;
			m_instances.emplace(id, info);
			return id;
		}

		void TouchActivity(InstanceId id, uint64_t nowMs)
		{
			auto it = m_instances.find(id);
			if (it == m_instances.end()) return;
			it->second.lastActiveTsMs = nowMs;
			it->second.state          = InstanceState::Active;
		}

		void MarkIdle(InstanceId id)
		{
			auto it = m_instances.find(id);
			if (it != m_instances.end() && it->second.state == InstanceState::Active)
				it->second.state = InstanceState::Idle;
		}

		void Despawn(InstanceId id)
		{
			auto it = m_instances.find(id);
			if (it != m_instances.end()) it->second.state = InstanceState::Despawned;
		}

		std::optional<InstanceInfo> Find(InstanceId id) const
		{
			auto it = m_instances.find(id);
			return (it == m_instances.end()) ? std::nullopt : std::optional<InstanceInfo>(it->second);
		}

		size_t Size() const noexcept { return m_instances.size(); }

		/// Garbage collect : supprime les instances Despawned ET les
		/// instances Idle dont lastActiveTsMs < nowMs - idleUnloadMs.
		size_t GarbageCollect(uint64_t nowMs, uint64_t idleUnloadMs)
		{
			size_t freed = 0;
			for (auto it = m_instances.begin(); it != m_instances.end(); )
			{
				if (it->second.state == InstanceState::Despawned)
				{
					it = m_instances.erase(it);
					++freed;
					continue;
				}
				if (it->second.state == InstanceState::Idle
					&& nowMs > it->second.lastActiveTsMs + idleUnloadMs)
				{
					it = m_instances.erase(it);
					++freed;
					continue;
				}
				++it;
			}
			return freed;
		}

	private:
		InstanceId m_nextId = 1;
		std::unordered_map<InstanceId, InstanceInfo> m_instances;
	};
}
