#pragma once
// CMANGOS.11 (Phase 3.11a) — ThreatList : par-creature, suit qui
// l'attaque et combien de threat chaque attaquant a accumule. Sert
// au selecteur de cible AI ("le top de threat est le tank a frapper").
//
// Pure data + algorithm, pas de wire ni de DB. Hot path : appele
// chaque tick de combat → utiliser des structures cache-friendly.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::server::combat
{
	using EntityId = uint64_t;

	struct ThreatEntry
	{
		EntityId attacker     = 0;
		float    threat       = 0.0f;
	};

	class ThreatList
	{
	public:
		ThreatList() = default;

		/// Ajoute du threat (positive ou negative). Cree l'entry si
		/// inexistante. Threat negative possible mais clamp a 0 (jamais
		/// negative cumulative — comportement classique).
		void AddThreat(EntityId attacker, float delta)
		{
			auto& v = m_byEntity[attacker];
			v += delta;
			if (v < 0.0f) v = 0.0f;
			m_dirty = true;
		}

		/// Set explicite (utile pour reset specifique a un attaquant).
		void SetThreat(EntityId attacker, float value)
		{
			m_byEntity[attacker] = (value < 0.0f) ? 0.0f : value;
			m_dirty = true;
		}

		/// Retire un attaquant de la liste (mort, fled, hors range).
		void DropAttacker(EntityId attacker)
		{
			m_byEntity.erase(attacker);
			m_dirty = true;
		}

		/// Reset complet (out of combat).
		void Reset()
		{
			m_byEntity.clear();
			m_sorted.clear();
			m_dirty = false;
		}

		/// True si la liste est non-vide → la creature est en combat.
		bool IsEngaged() const noexcept { return !m_byEntity.empty(); }

		/// Threat brut pour un attaquant. 0 si inconnu.
		float GetThreat(EntityId attacker) const
		{
			auto it = m_byEntity.find(attacker);
			return (it == m_byEntity.end()) ? 0.0f : it->second;
		}

		size_t Size() const noexcept { return m_byEntity.size(); }

		/// Cible top — `nullopt` si vide. Sort la liste par threat
		/// decroissant (cache invalide si dirty).
		std::optional<EntityId> TopTarget()
		{
			RebuildSortedIfDirty();
			if (m_sorted.empty()) return std::nullopt;
			return m_sorted.front().attacker;
		}

		/// Top N targets — utile pour AI qui split son focus, ou
		/// repassage de threat (taunt mecanique).
		std::vector<EntityId> TopN(size_t n)
		{
			RebuildSortedIfDirty();
			std::vector<EntityId> out;
			out.reserve(std::min(n, m_sorted.size()));
			for (size_t i = 0; i < std::min(n, m_sorted.size()); ++i)
				out.push_back(m_sorted[i].attacker);
			return out;
		}

	private:
		void RebuildSortedIfDirty()
		{
			if (!m_dirty) return;
			m_sorted.clear();
			m_sorted.reserve(m_byEntity.size());
			for (const auto& [id, threat] : m_byEntity)
				m_sorted.push_back({id, threat});
			std::sort(m_sorted.begin(), m_sorted.end(),
				[](const ThreatEntry& a, const ThreatEntry& b) {
					return a.threat > b.threat;
				});
			m_dirty = false;
		}

		std::unordered_map<EntityId, float> m_byEntity;
		std::vector<ThreatEntry>            m_sorted;
		bool                                m_dirty = false;
	};
}
