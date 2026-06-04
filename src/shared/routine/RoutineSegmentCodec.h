#pragma once

// M101.3 — Codec du segment de chunk `routines.bin`.
//
// Encode/décode une liste de graphes en mémoire (pur, dans routine_graph, donc
// testable headless). Les wrappers fichier (zone_builder writer, client reader)
// sont de minces appels autour de ce codec. Le corps de chaque graphe est le
// JSON canonique de M101.1 (length-prefixé) : le round-trip binaire se ramène
// au round-trip JSON déjà éprouvé.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/shared/routine/RoutineGraph.h"

namespace engine::routine::codec
{
	/// "RUTN" en little-endian.
	inline constexpr uint32_t kRoutinesMagic = 0x4E545552u;
	/// Version du conteneur binaire (distincte de kRoutineGraphVersion).
	inline constexpr uint32_t kRoutinesContainerVersion = 1u;

	/// Sérialise une liste de graphes en octets `routines.bin`.
	std::vector<uint8_t> EncodeRoutinesBin(const std::vector<RoutineGraph>& graphs);

	/// Désérialise des octets `routines.bin`. Retourne std::nullopt + `outError`
	/// si l'en-tête, la version ou un graphe est invalide.
	std::optional<std::vector<RoutineGraph>> DecodeRoutinesBin(
		const std::vector<uint8_t>& bytes, std::string& outError);
}
