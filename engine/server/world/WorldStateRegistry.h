#pragma once
// CMANGOS.28 (Phase 3.28a) — WorldStateRegistry : carte cle/valeur
// d'etats globaux du serveur (compteurs PvP, flags faction, etc.)
// avec listeners. Header-only.

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::server::world
{
	using StateId = uint32_t;

	using StateChangeListener = std::function<void(StateId, int64_t oldVal, int64_t newVal)>;

	class WorldStateRegistry
	{
	public:
		void Set(StateId id, int64_t value)
		{
			auto& v = m_state[id];
			const int64_t old = v;
			v = value;
			if (old != value) Notify(id, old, value);
		}

		int64_t Get(StateId id) const
		{
			auto it = m_state.find(id);
			return (it == m_state.end()) ? 0 : it->second;
		}

		int64_t Increment(StateId id, int64_t delta = 1)
		{
			const int64_t old = Get(id);
			Set(id, old + delta);
			return old + delta;
		}

		void Subscribe(StateChangeListener cb)
		{
			m_listeners.push_back(std::move(cb));
		}

		size_t StateCount() const noexcept { return m_state.size(); }

		void Clear() { m_state.clear(); m_listeners.clear(); }

	private:
		void Notify(StateId id, int64_t oldVal, int64_t newVal) const
		{
			for (const auto& cb : m_listeners) cb(id, oldVal, newVal);
		}

		std::unordered_map<StateId, int64_t> m_state;
		std::vector<StateChangeListener>     m_listeners;
	};
}
