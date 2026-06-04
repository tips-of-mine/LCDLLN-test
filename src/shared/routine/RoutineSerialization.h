#pragma once

// M101.1 — Sérialisation JSON déterministe d'un RoutineGraph.
//
// `ToJson` produit un JSON canonique STABLE octet pour octet pour un même
// graphe (ordre de clés fixe, nœuds/liens triés par id, floats formatés de
// façon locale-indépendante). `FromJson` est l'inverse. Invariant garanti :
// FromJson(ToJson(g)) reconstruit g à l'identique (cf. round-trip M101.3/.10).

#include <optional>
#include <string>
#include <string_view>

#include "src/shared/routine/RoutineGraph.h"

namespace engine::routine::serialization
{
	/// Sérialise un graphe en JSON canonique déterministe.
	std::string ToJson(const RoutineGraph& graph);

	/// Erreur de parsing (message lisible).
	struct ParseError
	{
		std::string message;
	};

	/// Désérialise un graphe. Retourne std::nullopt et renseigne `outError` en
	/// cas d'échec (JSON invalide, version inconnue, enum inconnu…).
	std::optional<RoutineGraph> FromJson(std::string_view json, ParseError& outError);
}
