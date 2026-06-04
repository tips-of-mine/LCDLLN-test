#pragma once

// M101.1 — Schéma de bornage des types de nœuds.
//
// Table STATIQUE (en dur, pas de données externes) décrivant, pour chaque
// `RoutineNodeType` : son libellé, les cibles `RoutineGraphKind` où il est
// valide, et les gabarits de pins/propriétés. Source de vérité unique pour la
// palette (M101.5), la validation (M101.6) et la doc (M101.11) — l'UI ne code
// AUCUN type de nœud en dur.

#include <vector>

#include "src/shared/routine/RoutineGraph.h"

namespace engine::routine
{
	/// Descripteur statique d'un type de nœud.
	struct RoutineNodeSchema
	{
		RoutineNodeType type = RoutineNodeType::Comment;
		const char*     displayName = "";
		/// Masque des cibles valides : bit (1u << int(RoutineGraphKind)).
		uint8_t         validKindsMask = 0;
		std::vector<RoutinePin>      pinTemplate;
		std::vector<RoutineProperty> propertyTemplate;
	};

	/// Masque de cible pour un `RoutineGraphKind`.
	inline uint8_t KindMask(RoutineGraphKind k)
	{
		return static_cast<uint8_t>(1u << static_cast<int>(k));
	}

	/// True si le schéma est utilisable dans un graphe de cette cible.
	inline bool SchemaValidForKind(const RoutineNodeSchema& s, RoutineGraphKind k)
	{
		return (s.validKindsMask & KindMask(k)) != 0;
	}

	/// Table immuable de tous les schémas connus (ordre stable).
	const std::vector<RoutineNodeSchema>& AllSchemas();

	/// Schéma d'un type donné, ou nullptr si inconnu.
	const RoutineNodeSchema* FindSchema(RoutineNodeType type);
}
