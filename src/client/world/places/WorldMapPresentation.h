#pragma once
// Carte du monde (touche M) — règles de présentation : libellés au survol et
// déroulé du parchemin à l'ouverture.
//
// Demande du 23/11/2026 :
//  - plus aucun nom affiché en permanence sur la carte : villes, hameaux, lieux
//    remarquables et lieux secrets n'ont plus de libellé dessiné, leur nom n'est
//    montré que par l'info-bulle de survol (WorldMapImGuiRenderer) ; le
//    nom d'une région n'apparaît (en filigrane, à son centre) que lorsque le
//    curseur est dans la région, et seulement en vue éloignée comme avant ;
//  - la fenêtre s'ouvre comme un parchemin qu'on déroule (hauteur animée) ;
//  - 25/11/2026 : bord de la zone découverte en fondu (voile progressif).
//
// Fonctions pures (testées par world_map_presentation_tests), sans dépendance ImGui.

#include <algorithm>

namespace engine::client
{
	/// Zoom au-delà duquel le nom de région (filigrane) n'est jamais dessiné :
	/// en vue rapprochée il couvrirait la zone regardée (valeur reprise de FIX.12).
	inline constexpr float kWorldMapRegionLabelMaxZoom = 2.5f;

	/// Durée (s) du déroulé du parchemin à l'ouverture de la carte.
	inline constexpr float kWorldMapUnrollSeconds = 0.35f;

	/// Vrai si le nom d'une région doit être dessiné en filigrane.
	/// \param zoom zoom courant de la carte (1 = carte entière).
	/// \param cursorInRegion le curseur est sur la carte, à l'intérieur de la région.
	/// \param centerDiscovered le centre de la région a été découvert (carte à découvrir).
	inline bool WorldMapRegionNameVisible(float zoom, bool cursorInRegion, bool centerDiscovered)
	{
		return cursorInRegion && centerDiscovered && zoom <= kWorldMapRegionLabelMaxZoom;
	}

	/// Bord flou de la zone découverte (25/11/2026) : couverture lissée basse
	/// (0) à haute (1) sur laquelle le voile passe de opaque à transparent.
	/// L'ancien contour net était le seuil 0,5 ; la bande l'encadre largement.
	inline constexpr float kWorldMapFogFadeLow = 0.02f;
	inline constexpr float kWorldMapFogFadeHigh = 0.95f;
	/// Nombre de niveaux d'opacité du fondu (regroupe les sous-cases voisines de
	/// même niveau en une seule série : peu de quads de plus qu'avant).
	inline constexpr int kWorldMapFogLevels = 16;

	/// Opacité du voile (0 = transparent, 1 = opaque) en un point du bord, à
	/// partir de sa couverture lissée \p coverage dans [0, 1] (moyenne gaussienne
	/// des cases découvertes, cf. dsc::IsPointRevealedSmooth). Interpolation
	/// « smoothstep » entre kWorldMapFogFadeLow et kWorldMapFogFadeHigh.
	inline float WorldMapFogAlpha(float coverage)
	{
		const float t = std::clamp((coverage - kWorldMapFogFadeLow) / (kWorldMapFogFadeHigh - kWorldMapFogFadeLow), 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	/// Niveau quantifié 0..kWorldMapFogLevels-1 de l'opacité \p alpha (0 = pas de voile).
	inline int WorldMapFogLevel(float alpha)
	{
		return std::clamp(static_cast<int>(alpha * static_cast<float>(kWorldMapFogLevels - 1) + 0.5f), 0, kWorldMapFogLevels - 1);
	}

	/// Avancement du déroulé dans [0, 1] après \p elapsedS secondes, pour une durée
	/// \p durationS (courbe « ease-out » cubique : départ vif, fin amortie comme un
	/// rouleau qui s'arrête). Durée nulle ou négative : déroulé immédiat (1).
	inline float WorldMapUnrollFraction(float elapsedS, float durationS)
	{
		if (durationS <= 0.0f)
			return 1.0f;
		const float t = std::clamp(elapsedS / durationS, 0.0f, 1.0f);
		const float inv = 1.0f - t;
		return 1.0f - inv * inv * inv;
	}

	/// Hauteur (px) de la fenêtre pendant le déroulé : part de \p minH (en-tête et
	/// rouleaux seuls) et atteint \p fullH à fraction 1. \p minH est borné à \p fullH.
	inline float WorldMapUnrollHeight(float fraction, float minH, float fullH)
	{
		const float lo = std::min(minH, fullH);
		return lo + (fullH - lo) * std::clamp(fraction, 0.0f, 1.0f);
	}
}
