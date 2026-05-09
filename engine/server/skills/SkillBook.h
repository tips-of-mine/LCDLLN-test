#pragma once
// CMANGOS.39 (Phase 4.39a) — SkillBook : skills d'un personnage (cooking,
// herbalism, mining, lockpicking, weapon skills) avec progression
// jusqu'a un cap. Header-only.

#include <cstdint>
#include <unordered_map>
#include <algorithm>

namespace engine::server::skills
{
	using SkillId = uint16_t;

	struct SkillEntry
	{
		uint16_t value = 0;  ///< niveau actuel
		uint16_t cap   = 0;  ///< cap dur (max attribuable a ce moment)
		uint16_t bonus = 0;  ///< bonus temporaire (potion, equipement)
	};

	class SkillBook
	{
	public:
		void Set(SkillId id, uint16_t value, uint16_t cap)
		{
			auto& e = m_skills[id];
			e.cap   = cap;
			e.value = std::min(value, cap);
		}

		bool Has(SkillId id) const { return m_skills.count(id) > 0; }

		const SkillEntry* Get(SkillId id) const
		{
			auto it = m_skills.find(id);
			return (it == m_skills.end()) ? nullptr : &it->second;
		}

		/// Ajoute \p delta points au skill, clampe a cap. Retourne le delta
		/// effectivement applique.
		uint16_t Gain(SkillId id, uint16_t delta)
		{
			auto it = m_skills.find(id);
			if (it == m_skills.end()) return 0;
			auto& e = it->second;
			const uint16_t before = e.value;
			const uint32_t after = std::min<uint32_t>(e.cap, static_cast<uint32_t>(e.value) + delta);
			e.value = static_cast<uint16_t>(after);
			return e.value - before;
		}

		void SetBonus(SkillId id, uint16_t bonus)
		{
			auto it = m_skills.find(id);
			if (it == m_skills.end()) return;
			it->second.bonus = bonus;
		}

		/// Valeur effective = value + bonus (clamp a 0xFFFF par securite).
		uint16_t Effective(SkillId id) const
		{
			auto it = m_skills.find(id);
			if (it == m_skills.end()) return 0;
			const uint32_t e = static_cast<uint32_t>(it->second.value) + it->second.bonus;
			return e > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(e);
		}

		size_t Count() const { return m_skills.size(); }

	private:
		std::unordered_map<SkillId, SkillEntry> m_skills;
	};
}
