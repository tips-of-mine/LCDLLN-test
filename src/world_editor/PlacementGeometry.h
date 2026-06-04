#pragma once

// M100.17 — Géométrie de placement (fonctions PURES, déterministes).
//
// Cœur calculatoire de l'outil de placement, indépendant de l'UI/rendu : drag-
// line, scatter (seedé → déterministe), random yaw/scale, orientation (align à
// la normale terrain ou world-up), helpers quaternion. Testable headless.

#include <cstdint>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::editor::world::placement
{
	using engine::math::Vec3;

	/// Génère les positions d'une drag-line de `start` à `end`, espacées de
	/// ~`spacing` m (start et end inclus). Au moins 1 point.
	std::vector<Vec3> GenerateDragLine(const Vec3& start, const Vec3& end, float spacing);

	/// Génère `count` positions dans un disque horizontal (rayon `radius`)
	/// centré sur `center`. Déterministe pour un `seed` donné.
	std::vector<Vec3> GenerateScatter(const Vec3& center, float radius, uint32_t count, uint64_t seed);

	struct YawScale
	{
		float yawDeg = 0.0f;
		float scale = 1.0f;
	};

	/// Tire un yaw (deg) et une échelle uniforme déterministes depuis `seed`.
	YawScale RandomYawScale(uint64_t seed, float rotMinDeg, float rotMaxDeg,
	                        float scaleMin, float scaleMax);

	/// Construit le quaternion (x,y,z,w) d'orientation : rotation de yaw autour
	/// de Y, alignée optionnellement à `terrainNormal` (sinon world-up).
	void BuildOrientation(float yawDeg, const Vec3& terrainNormal, bool alignToNormal,
	                      float outQuat[4]);

	/// Applique un quaternion (x,y,z,w) à un vecteur.
	Vec3 RotateVectorByQuat(const float q[4], const Vec3& v);
}
