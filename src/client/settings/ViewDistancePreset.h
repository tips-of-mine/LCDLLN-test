#pragma once

// Distance de vue pilotée par le préréglage « Qualité graphique » des options
// (render.quality_preset : 0 = Basse, 1 = Moyenne, 2 = Haute, 3 = Ultra).
//
// Le coût de rendu dépend d'abord de la distance de vue : plan lointain de la
// caméra (terrain, bâtiments, ombres en cascades réparties jusqu'à lui), nombre
// de tuiles de terrain résidentes, distance de culling du décor instancié. Un
// seul préréglage règle le tout, de façon cohérente.

#include <algorithm>
#include <cstdint>

namespace engine::client
{
	/// Préréglage par défaut (premier lancement) : Moyenne.
	inline constexpr int kDefaultQualityPreset = 1;

	/// Distances résolues pour un préréglage.
	struct ViewDistanceSettings
	{
		/// Plan lointain de la caméra de jeu (m).
		float cameraFarM = 500.0f;
		/// Distance de dessin des patches de terrain des cellules (m).
		float terrainDrawM = 500.0f;
		/// Rayon de chargement des tuiles de rendu des cellules (m, au BORD de la cellule).
		float cellRenderRadiusM = 800.0f;
		/// Facteur appliqué aux distances de culling du décor instancié et des props.
		float scatterScale = 0.8f;
	};

	/// Distances du préréglage \p preset (borné à [0, 3]).
	inline ViewDistanceSettings ResolveViewDistancePreset(int64_t preset)
	{
		static constexpr ViewDistanceSettings kTable[4] = {
			// v1.0.145 : vues lointaines allongées (une capitale se voit en entier ; le bâti
			// au-delà de cull_large_m est rendu en silhouettes, FarProxyRules.h).
			//  far      terrain  rendu    décor
			{  500.0f,  500.0f,   700.0f, 0.6f },  // Basse
			{  900.0f,  900.0f,  1100.0f, 0.8f },  // Moyenne
			{ 1400.0f, 1400.0f,  1600.0f, 1.0f },  // Haute
			{ 2200.0f, 2200.0f,  2600.0f, 1.25f }, // Ultra
		};
		return kTable[std::clamp<int64_t>(preset, 0, 3)];
	}
}
