#pragma once

// M100.21 — Collecte des influences d'entités (joueur/PNJ) pour la flexion de
// la végétation. Logique PURE (filtre portée + troncature 32 + flexion miroir
// du shader). L'upload SSBO et le shader GPU sont gérés ailleurs (différés).

#include <cstdint>
#include <vector>

namespace engine::world::foliage
{
	/// Influence uploadée vers le SSBO (miroir de la struct GLSL EntityInfluence).
	struct EntityInfluence
	{
		float positionX = 0.0f;
		float positionZ = 0.0f;
		float radiusMeters = 1.0f;
		float falloffPower = 1.5f;
	};

	/// Candidat brut : position monde XZ d'une entité + son rayon de capsule.
	struct EntityCandidate
	{
		float x = 0.0f;
		float z = 0.0f;
		float radiusMeters = 1.0f;
		float falloffPower = 1.5f;
	};

	constexpr int   kMaxEntityInfluences = 32;
	constexpr float kInfluenceRangeMeters = 30.0f;

	/// Collecte les influences : garde les entités à < 30 m de la caméra,
	/// triées par distance croissante, tronquées à 32. Pur et déterministe.
	std::vector<EntityInfluence> CollectEntityInfluences(
		float camX, float camZ, const std::vector<EntityCandidate>& entities);

	/// Magnitude de flexion d'un brin à (worldX,worldZ) sous une influence —
	/// miroir exact du loop de foliage.vert. `heightWeight` ∈ [0,1].
	/// Retourne 0 hors du rayon.
	float ComputeFlexionMagnitude(const EntityInfluence& inf, float worldX, float worldZ, float heightWeight);
}
