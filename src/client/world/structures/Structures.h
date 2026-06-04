#pragma once

// M100.30 — Génération de structures le long d'une spline (ponts/murs) +
// surface marchable de pont. Fonctions PURES, testables headless.

#include <cstdint>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::structures
{
	/// Longueur d'une polyline (somme des segments, XZ+Y).
	float PolylineLength(const std::vector<engine::math::Vec3>& pts);

	/// Nombre de segments de pont pour une longueur donnée (>= 1).
	int BridgeSegmentCount(float lengthMeters, float segmentLengthMeters);

	/// Distances (le long de la polyline) des poteaux d'un mur, espacés de
	/// `postSpacingMeters` (inclut 0 et la fin).
	std::vector<float> WallPostDistances(float lengthMeters, float postSpacingMeters);

	/// Indices des nœuds intérieurs formant un coin dont l'angle de virage
	/// (entre segments consécutifs) est >= `thresholdDeg`.
	std::vector<int> DetectSharpCorners(const std::vector<engine::math::Vec3>& nodes, float thresholdDeg);

	/// Surface marchable d'un pont : segment central [a,b] + largeur + hauteur.
	struct BridgeWalkable
	{
		engine::math::Vec3 a{ 0, 0, 0 };
		engine::math::Vec3 b{ 0, 0, 0 };
		float widthMeters = 6.0f;
		float bridgeY = 0.0f;
	};

	/// True si `pos` est sur la surface marchable du pont (projection XZ sur le
	/// segment central à <= width/2, et |y - bridgeY| <= 0.3 m).
	bool QueryBridgeSurface(const BridgeWalkable& bridge, const engine::math::Vec3& pos);
}
