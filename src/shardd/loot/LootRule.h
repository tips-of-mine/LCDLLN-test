#pragma once
// Wave 22 — LootRule : interface + 4 implementations basiques pour
// distribuer un drop a un groupe selon le LootMethod configure.
//
// Pattern interface + impls header-only : pas de virtual call cost si le
// caller utilise directement les impls concretes (FreeForAllLootRule,
// RoundRobinLootRule, etc.). Le caller choisit l'impl selon le LootMethod
// du Group (cf. dispatch dans Group::CurrentLootMethod).
//
// Conception : LootRule ne SAIT PAS comment lire l'inventaire ou la DB.
// Il prend une liste d'EligibleEntity (player IDs presents au kill) et
// retourne le PlayerId gagnant pour CE drop. Le caller (LootHandler ou
// equivalent) execute ensuite la mutation persistente (insert mail / item).

#include "src/masterd/Groups/GroupTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::server::loot
{
	using engine::server::groups::PlayerId;

	/// Roll Need (priorite haute) > Greed > Pass. Utilise par
	/// NeedBeforeGreedLootRule.
	enum class RollChoice : uint8_t
	{
		Pass  = 0,
		Greed = 1,
		Need  = 2,
	};

	/// Interface abstraite : 1 methode pure virtuelle Pick(). Pas de state
	/// shared entre LootRule et le caller — le caller passe tout le contexte
	/// au moment du call.
	class ILootRule
	{
	public:
		virtual ~ILootRule() = default;

		/// \param eligible player IDs presents au kill (membres du groupe
		///                 + qualifies par la regle metier, ex : range/loot,
		///                 done par le caller avant l'appel)
		/// \param rolls    optionnel : map des choix roll par player (utilise
		///                 par NeedBeforeGreed seulement, ignore par les autres)
		/// \return PlayerId gagnant, ou nullopt si pas de gagnant determine
		///         (ex : tous Pass sur NeedBeforeGreed).
		virtual std::optional<PlayerId> Pick(
			const std::vector<PlayerId>& eligible,
			const std::unordered_map<PlayerId, RollChoice>& rolls) const = 0;
	};

	// ========================================================================
	// FreeForAll : premier dans la liste eligible. Simple et deterministe.
	// ========================================================================
	class FreeForAllLootRule final : public ILootRule
	{
	public:
		std::optional<PlayerId> Pick(
			const std::vector<PlayerId>& eligible,
			const std::unordered_map<PlayerId, RollChoice>& /*rolls*/) const override
		{
			if (eligible.empty()) return std::nullopt;
			return eligible.front();
		}
	};

	// ========================================================================
	// RoundRobin : rotation. Le compteur interne est conserve par le caller
	// (typiquement membre du Group) via SetCursor / GetCursor.
	// ========================================================================
	class RoundRobinLootRule final : public ILootRule
	{
	public:
		std::optional<PlayerId> Pick(
			const std::vector<PlayerId>& eligible,
			const std::unordered_map<PlayerId, RollChoice>& /*rolls*/) const override
		{
			if (eligible.empty()) return std::nullopt;
			const size_t idx = m_cursor % eligible.size();
			m_cursor++;
			return eligible[idx];
		}

		void SetCursor(size_t c) noexcept { m_cursor = c; }
		size_t GetCursor() const noexcept { return m_cursor; }

	private:
		mutable size_t m_cursor = 0;
	};

	// ========================================================================
	// MasterLooter : retourne toujours le leader (= masterLooter). Le caller
	// fournit le leader via le constructeur.
	// ========================================================================
	class MasterLooterLootRule final : public ILootRule
	{
	public:
		explicit MasterLooterLootRule(PlayerId masterLooter)
			: m_masterLooter(masterLooter) {}

		std::optional<PlayerId> Pick(
			const std::vector<PlayerId>& eligible,
			const std::unordered_map<PlayerId, RollChoice>& /*rolls*/) const override
		{
			if (eligible.empty()) return std::nullopt;
			// Verifier que masterLooter est dans eligible — sinon fallback au
			// premier (cas defensif : masterLooter offline, kick, etc.).
			for (auto p : eligible)
				if (p == m_masterLooter) return m_masterLooter;
			return eligible.front();
		}

	private:
		PlayerId m_masterLooter;
	};

	// ========================================================================
	// NeedBeforeGreed : si au moins 1 Need -> tiebreaker premier-arrive.
	// Sinon, si au moins 1 Greed -> idem. Sinon, nullopt (tous Pass).
	// ========================================================================
	class NeedBeforeGreedLootRule final : public ILootRule
	{
	public:
		std::optional<PlayerId> Pick(
			const std::vector<PlayerId>& eligible,
			const std::unordered_map<PlayerId, RollChoice>& rolls) const override
		{
			if (eligible.empty()) return std::nullopt;
			// Cherche un Need d'abord, dans l'ordre de eligible.
			for (auto p : eligible)
			{
				auto it = rolls.find(p);
				if (it != rolls.end() && it->second == RollChoice::Need)
					return p;
			}
			// Sinon cherche un Greed.
			for (auto p : eligible)
			{
				auto it = rolls.find(p);
				if (it != rolls.end() && it->second == RollChoice::Greed)
					return p;
			}
			// Tous Pass -> pas de gagnant.
			return std::nullopt;
		}
	};
}
