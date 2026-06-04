#pragma once

// M101.6 — Validation structurelle d'un graphe de routine.
//
// Fonction PURE et DÉTERMINISTE (dans la lib routine_graph) : utilisable en CI
// headless ET par l'éditeur (la couche UI ne fait que présenter les issues).

#include <string>
#include <vector>

#include "src/shared/routine/RoutineGraph.h"

namespace engine::routine::validation
{
	enum class IssueSeverity : uint8_t { Warning = 0, Error = 1 };

	enum class IssueKind : uint8_t
	{
		ExecCycle = 0,
		IncompatiblePins = 1,
		OrphanNode = 2,
		SchemaMismatch = 3,
		RootCardinality = 4,
		UnknownEntityRef = 5
	};

	struct ValidationIssue
	{
		IssueSeverity severity = IssueSeverity::Error;
		IssueKind     kind = IssueKind::SchemaMismatch;
		uint32_t      nodeId = 0;   ///< 0 si global.
		uint32_t      linkId = 0;   ///< 0 si non pertinent.
		std::string   message;
	};

	/// Valide le graphe. Issues triées de façon stable (kind, nodeId, linkId).
	std::vector<ValidationIssue> Validate(const RoutineGraph& graph);

	/// True si au moins une issue de sévérité Error est présente.
	bool HasError(const std::vector<ValidationIssue>& issues);
}
