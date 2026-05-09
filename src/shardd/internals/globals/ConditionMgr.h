#pragma once
// CMANGOS.16 (Phase 1b) — ConditionMgr : moteur d'évaluation data-driven
// chargé une fois au boot depuis 2 tables SQL (conditions + condition_groups).

#include "src/shardd/internals/globals/Condition.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Singleton chargé au boot, lookup O(1), évaluation O(profondeur du group).
	/// Thread-safety : Load() une seule fois (assertion sinon). Post-load,
	/// Evaluate() est lock-free (read-only).
	class ConditionMgr
	{
	public:
		ConditionMgr() = default;
		~ConditionMgr() = default;
		ConditionMgr(const ConditionMgr&) = delete;
		ConditionMgr& operator=(const ConditionMgr&) = delete;

		/// Charge `conditions` + `condition_groups` depuis la DB.
		/// Retourne false si requête échoue ou si cycle détecté dans les groups.
		/// \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool);

		/// Évalue une condition atomique par ID. Retourne `true` si le prédicat
		/// passe, `false` sinon (incluant condition inexistante).
		bool EvaluateCondition(uint32_t conditionId, const EvaluationContext& ctx) const;

		/// Évalue un groupe par ID. Retourne `true` si le group passe.
		bool EvaluateGroup(uint32_t groupId, const EvaluationContext& ctx) const;

		/// Helper unifié : si l'ID est un group_id, évalue le group ; sinon évalue
		/// comme condition_id.
		/// Note : il y a une zone d'overlap potentielle si un condition_id et un
		/// group_id partagent la même valeur. Convention LCDLLN : condition_id
		/// ∈ [1, 9999], group_id ∈ [10000, ∞) — voir doc db_sql_guidelines.md.
		bool Evaluate(uint32_t id, const EvaluationContext& ctx) const;

		size_t ConditionCount() const { return m_conditions.size(); }
		size_t GroupCount() const     { return m_groups.size(); }

	private:
		/// DFS détection cycle dans les groupes. Retourne true si cycle trouvé.
		bool DetectCycles() const;
		bool DetectCyclesFrom(uint32_t groupId,
			std::unordered_map<uint32_t, int>& color) const;

		/// Évaluation d'un atom Condition (switch sur type, pas de virtual).
		bool EvaluateAtom(const Condition& cond, const EvaluationContext& ctx) const;

		std::unordered_map<uint32_t, Condition> m_conditions;
		std::unordered_map<uint32_t, ConditionGroup> m_groups;
		bool m_loaded = false;
	};
}
