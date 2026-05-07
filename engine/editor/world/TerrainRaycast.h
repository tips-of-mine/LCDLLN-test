#pragma once

#include "engine/render/Camera.h"

#include <functional>

namespace engine::editor::world
{
	/// Résultat d'un raycast caméra → heightmap (M100.6). Si `hit==false`, les
	/// autres champs sont indéfinis.
	struct TerrainHit
	{
		bool  hit = false;
		float worldX = 0.0f;
		float worldY = 0.0f;
		float worldZ = 0.0f;
	};

	/// Lance un rayon depuis le pixel écran `(sx, sy)` dans la viewport `vw x vh`
	/// vers le terrain et retourne le point d'impact. Pas d'allocation, pas
	/// d'IO. L'algorithme :
	///   1. Reconstruit l'origine et la direction du rayon en repère monde
	///      depuis la caméra (basis right/up/forward + position) — mode
	///      perspective ou ortho.
	///   2. Marche sur le rayon avec un pas de 1 m jusqu'à ce que la hauteur
	///      du rayon descende sous la hauteur sampled, puis raffine par 4
	///      itérations de Newton-Raphson sur `f(t) = ray.y(t) - sample(...)`.
	///
	/// \param cam        Caméra source (lecture seule, ne mute pas).
	/// \param sx, sy     Pixel viewport (origine top-left).
	/// \param vw, vh     Taille de la viewport en pixels.
	/// \param sampleHeightAt  Callback qui retourne la hauteur monde (mètres)
	///                        au point monde `(worldX, worldZ)`. Doit être
	///                        rapide (sera appelé ~50× par cast).
	/// \param maxRangeMeters  Distance max parcourue par le rayon. Défaut 2000m.
	/// \return Point d'impact ou `{hit=false}` si le rayon ne touche pas le sol.
	TerrainHit RaycastTerrain(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		const std::function<float(float, float)>& sampleHeightAt,
		float maxRangeMeters = 2000.0f);
}
