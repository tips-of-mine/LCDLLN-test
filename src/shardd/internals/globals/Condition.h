#pragma once
// CMANGOS.16 (Phase 1b) — types pour ConditionMgr : enum ConditionType,
// enum ConditionLogic, struct Condition, struct ConditionGroup.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::shard::globals
{
	/// Types de prédicats supportés en Phase 1b. Étendre via PR séparée.
	enum class ConditionType : uint8_t
	{
		LevelGE  = 0,  ///< value1 = niveau min joueur
		LevelLE  = 1,  ///< value1 = niveau max joueur
		HasItem  = 2,  ///< value1 = item entry, value2 = count min
		ZoneId   = 3,  ///< value1 = zone id requise
		InGroup  = 4,  ///< pas de paramètre
		// Étendre ici (HasAura, QuestState, Reputation, ...) au fil des besoins downstream.
	};

	/// Logique de composition pour ConditionGroup.
	enum class ConditionLogic : uint8_t
	{
		And = 0,
		Or  = 1,
		Not = 2,  ///< un seul membre, négation
	};

	/// Type de membre d'un ConditionGroup : 0=condition_id, 1=group_id.
	enum class ConditionMemberType : uint8_t
	{
		Condition = 0,
		Group     = 1,
	};

	/// Prédicat atomique chargé depuis la table `conditions`.
	struct Condition
	{
		uint32_t conditionId = 0;
		ConditionType type   = ConditionType::LevelGE;
		int32_t value1       = 0;
		int32_t value2       = 0;
		int32_t value3       = 0;
	};

	/// Membre d'un ConditionGroup (référence vers une condition ou un autre group).
	struct ConditionGroupMember
	{
		uint32_t memberId           = 0;
		ConditionMemberType memberType = ConditionMemberType::Condition;
	};

	/// Composition logique de plusieurs conditions/groups.
	struct ConditionGroup
	{
		uint32_t groupId      = 0;
		ConditionLogic logic  = ConditionLogic::And;
		std::vector<ConditionGroupMember> members;
	};

	/// Contexte d'évaluation (fourni par le caller : handler loot/quest/...).
	/// Permet à ConditionMgr de rester data-driven sans connaître l'archi LCDLLN
	/// (Player class, etc.).
	struct EvaluationContext
	{
		uint64_t sourceEntityId = 0;
		int32_t  sourceLevel    = 0;
		uint32_t sourceZoneId   = 0;
		bool     inGroup        = false;
		/// item_entry → count détenu. Map vide = pas d'items.
		std::unordered_map<uint32_t, uint32_t> sourceItems;
	};
}
