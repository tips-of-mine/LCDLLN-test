#pragma once

// M100.29 — Échantillonnage de spline (Catmull-Rom), auto-fit terrain, et
// redistribution splat sous route (somme=255). Pur, testable headless.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "src/client/world/spline/SplineInstances.h"
#include "src/shared/math/Math.h"

namespace engine::world::spline
{
	/// Échantillonne une Catmull-Rom uniforme passant par les nœuds.
	/// `samplesPerSegment` points par segment ; inclut les nœuds (t=0 de chaque
	/// segment) et le dernier nœud. Y des nœuds utilisé tel quel.
	std::vector<engine::math::Vec3> SampleCatmullRom(const std::vector<SplineNode>& nodes,
	                                                 bool closed, int samplesPerSegment);

	using HeightSampler = std::function<float(float worldX, float worldZ)>;

	/// Recalcule le Y de chaque point via la heightmap (auto-fit terrain).
	std::vector<engine::math::Vec3> GroundFit(const std::vector<engine::math::Vec3>& pts,
	                                          const HeightSampler& sampler);

	/// Applique un poids `target` à la couche `roadLayer` d'une cellule splat
	/// 8 couches, en redistribuant les autres pour préserver somme=255.
	void ApplyRoadWeight(std::array<uint8_t, 8>& weights, int roadLayer, uint8_t target);
}
