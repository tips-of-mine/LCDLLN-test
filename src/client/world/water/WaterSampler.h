// src/client/world/water/WaterSampler.h
#pragma once

#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/math/Math.h"

#include <optional>

namespace engine::world::water
{
	/// Sampler géométrique stateless qui interroge un `WaterScene` (lacs + rivières)
	/// pour répondre à : « ce point monde est-il dans un volume d'eau, et si oui à
	/// quelle profondeur ? ». Aucun état runtime. Thread-safe pour les lectures
	/// concurrentes (lecture seule sur la scène référencée).
	///
	/// Algorithme :
	/// - Lac : point-in-polygon 2D dans XZ.
	/// - Rivière : projection orthogonale de `worldPos.xz` sur chaque segment,
	///   hit si distance latérale <= widthLocal/2 (interpolation linéaire de la
	///   largeur entre les deux nodes).
	/// - Multi-overlap : retourne le premier hit (la sélection deepest-wins
	///   sera ajoutée en Task 4 de M100.15).
	/// - Filtre `depth > 0` : pas de hit si pieds au-dessus de la surface.
	class WaterSampler
	{
	public:
		/// Mémorise la référence vers la scène. Aucune copie. La scène doit
		/// survivre au sampler.
		bool Init(const WaterScene& scene) noexcept;

		/// Retourne `{surfaceY, depth}` ou `nullopt` si hors eau.
		/// `worldPos.y` = position monde des pieds du joueur.
		std::optional<WaterSample> Sample(engine::math::Vec3 worldPos) const noexcept;

	private:
		const WaterScene* m_scene = nullptr;
	};
}
