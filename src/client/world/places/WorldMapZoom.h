#pragma once
// FIX.11 — Zoom de la carte du monde (touche M) exprimé en mètres visibles.
//
// Le zoom de WorldMapViewState vaut 1 pour la carte entière (taille du monde
// `sizeM`, plusieurs centaines de km) ; un zoom z montre sizeM / z mètres de
// côté. Avant FIX.11 la carte s'ouvrait à z = 1 et plafonnait à z = 8 (≈ 30 km
// visibles au plus près) : un déplacement de quelques centaines de mètres ne se
// voyait pas sur la carte (retour joueur du 30/10/2026).
//
// Fonctions pures (testées par world_map_zoom_tests), sans dépendance ImGui.

#include <algorithm>

namespace engine::client
{
	/// Zoom maximal (≥ 1) pour que la vue la plus rapprochée montre \p minViewM
	/// mètres de côté sur un monde de \p worldSizeM mètres.
	inline float WorldMapMaxZoom(float worldSizeM, float minViewM)
	{
		if (worldSizeM <= 0.0f || minViewM <= 0.0f)
			return 1.0f;
		return std::max(1.0f, worldSizeM / minViewM);
	}

	/// Zoom qui montre \p viewM mètres de côté, borné à [1, maxZoom].
	inline float WorldMapZoomForView(float worldSizeM, float viewM, float maxZoom)
	{
		if (worldSizeM <= 0.0f || viewM <= 0.0f)
			return 1.0f;
		return std::clamp(worldSizeM / viewM, 1.0f, std::max(1.0f, maxZoom));
	}

	/// Côté visible (m) au zoom \p zoom (affichage « ~2 km visibles »).
	inline float WorldMapVisibleMeters(float worldSizeM, float zoom)
	{
		return worldSizeM / std::max(1.0f, zoom);
	}

	/// Zoom servant à dimensionner les marqueurs et à décider des libellés : les
	/// règles existantes (taille ∝ zoom, libellés dès zoom 2) ont été réglées pour
	/// z ≤ 8 ; au-delà, les marqueurs gardent la taille de z = 8 au lieu de grossir
	/// jusqu'à couvrir la carte.
	inline float WorldMapMarkerZoom(float zoom)
	{
		return std::clamp(zoom, 1.0f, 8.0f);
	}
}
