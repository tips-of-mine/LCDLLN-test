#pragma once
// CMANGOS.31 (Phase 5.31a) — GameEventManager : evenements saisonniers
// (Halloween, Christmas, etc.) avec activation periodique. Header-only.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::events
{
	using EventId = uint32_t;

	struct GameEventDef
	{
		EventId     id          = 0;
		std::string name;
		uint64_t    startTsMs   = 0;       ///< absolute or recurring offset
		uint64_t    durationMs  = 0;       ///< event lasts this long
		uint64_t    recurMs     = 0;       ///< 0 = one-shot ; >0 = recurring period
	};

	enum class EventState : uint8_t
	{
		Inactive = 0,
		Active   = 1,
	};

	class GameEventManager
	{
	public:
		void Register(GameEventDef def) { m_events[def.id] = std::move(def); }

		EventState GetState(EventId id, uint64_t nowMs) const
		{
			auto it = m_events.find(id);
			if (it == m_events.end()) return EventState::Inactive;
			const auto& d = it->second;
			if (d.recurMs == 0)
			{
				return (nowMs >= d.startTsMs && nowMs < d.startTsMs + d.durationMs)
					? EventState::Active : EventState::Inactive;
			}
			// Recurring : compute offset from startTsMs.
			if (nowMs < d.startTsMs) return EventState::Inactive;
			const uint64_t offset = (nowMs - d.startTsMs) % d.recurMs;
			return (offset < d.durationMs) ? EventState::Active : EventState::Inactive;
		}

		std::vector<EventId> ActiveEvents(uint64_t nowMs) const
		{
			std::vector<EventId> out;
			for (const auto& [id, def] : m_events)
				if (GetState(id, nowMs) == EventState::Active) out.push_back(id);
			return out;
		}

		size_t Size() const noexcept { return m_events.size(); }

	private:
		std::unordered_map<EventId, GameEventDef> m_events;
	};
}
