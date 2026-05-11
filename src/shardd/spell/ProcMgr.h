#pragma once
// Wave 23 — ProcMgr : declencheurs conditionnels d'effets sur events
// gameplay (OnMeleeHit, OnSpellCrit, OnHpBelow30, etc.).
//
// Pattern : un proc est defini par un ProcTemplate qui specifie sur quel
// event il declenche, avec quelle chance (procChance 0-100), et quel
// spell il declenche. Au runtime, le caller appelle ProcMgr::OnEvent()
// avec le type d'event, et ProcMgr retourne la liste des spellIds a
// declencher (apres roll de chance).

#include "src/shardd/spell/SpellTemplate.h"

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace engine::server::spell
{
	/// Types d'events qui peuvent declencher un proc. Stable (wire / DB).
	enum class ProcEvent : uint8_t
	{
		OnMeleeHit       = 0,
		OnMeleeCrit      = 1,
		OnSpellHit       = 2,
		OnSpellCrit      = 3,
		OnHpBelowPercent = 4,   ///< trigger sous N% HP (param)
		OnKill           = 5,
		OnDeath          = 6,
	};

	struct ProcTemplate
	{
		uint32_t   procId      = 0;   ///< id unique
		ProcEvent  event       = ProcEvent::OnMeleeHit;
		SpellId    triggerSpell = 0;  ///< sort a declencher
		uint8_t    procChance  = 100; ///< 0-100 (%)
		uint32_t   internalCooldownMs = 0; ///< 0 = pas de ICD
	};

	class ProcMgr
	{
	public:
		ProcMgr() = default;

		/// Enregistre un proc template. Indexe par event pour lookup O(1)
		/// au moment d'OnEvent.
		void Register(ProcTemplate proc)
		{
			m_byEvent[proc.event].push_back(proc);
		}

		/// Declenche les procs eligibles pour \p event. Retourne les
		/// spellIds a caster (apres roll chance).
		///
		/// \param event       type d'event qui s'est produit
		/// \param rng         random generator (caller fournit pour reproductibilite)
		/// \param nowMs       timestamp actuel (pour ICD check)
		/// \param ownerProcCooldowns map procId → nextReadyAtMs par owner (modifiee in-place)
		/// \return liste de spellIds a caster
		std::vector<SpellId> OnEvent(
			ProcEvent event,
			std::mt19937& rng,
			uint64_t nowMs,
			std::unordered_map<uint32_t, uint64_t>& ownerProcCooldowns) const
		{
			std::vector<SpellId> out;
			auto it = m_byEvent.find(event);
			if (it == m_byEvent.end()) return out;

			std::uniform_int_distribution<uint32_t> d(0, 99);
			for (const auto& p : it->second)
			{
				// ICD check.
				auto cdIt = ownerProcCooldowns.find(p.procId);
				if (cdIt != ownerProcCooldowns.end() && nowMs < cdIt->second)
					continue;
				// Roll chance.
				if (p.procChance >= 100 || d(rng) < p.procChance)
				{
					out.push_back(p.triggerSpell);
					if (p.internalCooldownMs > 0)
						ownerProcCooldowns[p.procId] = nowMs + p.internalCooldownMs;
				}
			}
			return out;
		}

		size_t ProcCount() const noexcept
		{
			size_t total = 0;
			for (const auto& kv : m_byEvent) total += kv.second.size();
			return total;
		}

	private:
		std::unordered_map<ProcEvent, std::vector<ProcTemplate>> m_byEvent;
	};
}
