#pragma once
// CMANGOS.24 (Phase 3.24a) — ReputationManager : faction reputation
// par account avec spillover bitmask (changement sur faction A
// propage X% sur factions liees via mask). Pure data + algorithm,
// pas de wire ni DB persistence.

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server::reputation
{
	using FactionId = uint32_t;
	using AccountId = uint64_t;

	/// Standings cmangos : Hated -42000 → Exalted +41999.
	enum class ReputationStanding : int8_t
	{
		Hated      = -6,
		Hostile    = -5,
		Unfriendly = -4,
		Neutral    = -3,
		Friendly   = -2,
		Honored    = -1,
		Revered    = 0,
		Exalted    = 1,
	};

	struct ReputationRange
	{
		int32_t            minValue;
		int32_t            maxValue;
		ReputationStanding standing;
	};

	/// Bornes par standing (alignees sur cmangos / WoW).
	inline constexpr int32_t kReputationMin = -42000;
	inline constexpr int32_t kReputationMax = 41999;

	/// Spillover : un changement sur \p sourceFaction propage `factor`
	/// (0..1) sur \p targetFaction. Maximum kMaxSpilloverPerFaction par
	/// source.
	struct SpilloverRule
	{
		FactionId sourceFaction;
		FactionId targetFaction;
		float     factor;  ///< multiplier 0..1
	};

	inline constexpr size_t kMaxSpilloverPerFaction = 8;

	class ReputationManager
	{
	public:
		ReputationManager() = default;

		/// Ajoute une regle de spillover. Pas d'erreur si on depasse
		/// kMaxSpilloverPerFaction — les regles supplementaires sont
		/// silencieusement ignorees.
		void AddSpilloverRule(const SpilloverRule& rule)
		{
			if (rule.factor <= 0.0f) return;
			auto& v = m_spillover[rule.sourceFaction];
			if (v.size() < kMaxSpilloverPerFaction)
				v.push_back(rule);
		}

		/// Modifie la reputation. Le delta est applique a la faction
		/// source + (delta * factor) sur chaque faction spillover.
		/// Resultat clamp a [kReputationMin, kReputationMax].
		void GainReputation(AccountId accountId, FactionId faction, int32_t delta)
		{
			ApplyDelta(accountId, faction, delta);

			auto it = m_spillover.find(faction);
			if (it == m_spillover.end()) return;
			for (const auto& rule : it->second)
			{
				const int32_t spilloverDelta = static_cast<int32_t>(
					static_cast<float>(delta) * rule.factor);
				if (spilloverDelta != 0)
					ApplyDelta(accountId, rule.targetFaction, spilloverDelta);
			}
		}

		int32_t GetReputation(AccountId accountId, FactionId faction) const
		{
			auto itAcc = m_reputation.find(accountId);
			if (itAcc == m_reputation.end()) return 0;
			auto itFac = itAcc->second.find(faction);
			return (itFac == itAcc->second.end()) ? 0 : itFac->second;
		}

		/// Retourne le standing pour la valeur courante.
		static ReputationStanding StandingFor(int32_t value)
		{
			if (value <= -42000) return ReputationStanding::Hated;
			if (value <= -6000)  return ReputationStanding::Hostile;
			if (value <= -3000)  return ReputationStanding::Unfriendly;
			if (value <= 0)      return ReputationStanding::Neutral;
			if (value <= 3000)   return ReputationStanding::Friendly;
			if (value <= 9000)   return ReputationStanding::Honored;
			if (value <= 21000)  return ReputationStanding::Revered;
			return ReputationStanding::Exalted;
		}

		/// Reset (utile en tests).
		void Clear()
		{
			m_reputation.clear();
			m_spillover.clear();
		}

	private:
		void ApplyDelta(AccountId accountId, FactionId faction, int32_t delta)
		{
			auto& v = m_reputation[accountId][faction];
			int64_t newVal = static_cast<int64_t>(v) + static_cast<int64_t>(delta);
			if (newVal < kReputationMin) newVal = kReputationMin;
			if (newVal > kReputationMax) newVal = kReputationMax;
			v = static_cast<int32_t>(newVal);
		}

		std::unordered_map<AccountId, std::unordered_map<FactionId, int32_t>> m_reputation;
		std::unordered_map<FactionId, std::vector<SpilloverRule>> m_spillover;
	};
}
