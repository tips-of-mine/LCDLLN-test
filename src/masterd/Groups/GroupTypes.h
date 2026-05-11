#pragma once
// Wave 22 — GroupTypes : enums communes pour Group (party / raid),
// LootMethod (FFA / round-robin / master-looter / need-before-greed),
// GroupRole (tank / heal / DPS).
//
// Header-only, valeurs stables (wire format + DB en dependent).

#include <cstdint>

namespace engine::server::groups
{
	using GroupId  = uint64_t;
	using PlayerId = uint64_t;

	/// Type de groupe. Party = max 5, Raid = max 10/25/40.
	enum class GroupType : uint8_t
	{
		Party = 0,
		Raid  = 1,
	};

	/// Methode de loot configuree pour le groupe. Determine quelle LootRule
	/// est appliquee aux drops collectes ensemble.
	enum class LootMethod : uint8_t
	{
		FreeForAll      = 0,  ///< Premier arrive premier servi (loot independant)
		RoundRobin      = 1,  ///< Rotation au prochain drop
		MasterLooter    = 2,  ///< Un membre (leader) attribue manuellement
		NeedBeforeGreed = 3,  ///< Roll Need (priorite) > Greed > Pass
	};

	/// Role optionnel (utilise par LFG / matchmaker). Conserve par membre.
	enum class GroupRole : uint8_t
	{
		Unknown = 0,
		Tank    = 1,
		Heal    = 2,
		Dps     = 3,
	};

	inline const char* LootMethodToString(LootMethod m) noexcept
	{
		switch (m)
		{
			case LootMethod::FreeForAll:      return "FreeForAll";
			case LootMethod::RoundRobin:      return "RoundRobin";
			case LootMethod::MasterLooter:    return "MasterLooter";
			case LootMethod::NeedBeforeGreed: return "NeedBeforeGreed";
		}
		return "Unknown";
	}

	/// Capacite max selon le type (utilise par Group::AddMember pour rejeter).
	inline constexpr uint32_t kPartyMaxMembers = 5;
	inline constexpr uint32_t kRaidMaxMembers  = 40;
}
