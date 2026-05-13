#pragma once

#include "src/shared/math/Math.h"

#include <vector>

namespace engine::editor::world
{
	/// Simplification d'une polyline 3D par l'algorithme Douglas-Peucker
	/// (M100.36, réutilisé en M100.37+). Garde les points 3D tels quels —
	/// la distance perpendiculaire mesurée est dans le plan XZ uniquement
	/// (pour les rivières, l'altitude suit la heightmap et n'est pas un
	/// critère de simplification).
	///
	/// Algorithme classique récursif : sur la polyline `[a..b]`, trouve le
	/// point le plus éloigné du segment `(a, b)`. Si sa distance ≤
	/// `toleranceMeters`, on garde seulement les extrémités. Sinon, on
	/// récurse sur `[a..pivot]` et `[pivot..b]`. Le résultat est garanti
	/// d'avoir une déviation max ≤ `toleranceMeters` par rapport à la
	/// polyline originale.
	///
	/// \param points           Polyline source (≥ 2 points).
	/// \param toleranceMeters  Tolérance de simplification en mètres.
	/// \return                 Polyline simplifiée. Identique à la source
	///                         si < 3 points ou si tolerance ≤ 0.
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	std::vector<engine::math::Vec3> SimplifyPolylineDouglasPeucker(
		const std::vector<engine::math::Vec3>& points,
		float toleranceMeters);
}
