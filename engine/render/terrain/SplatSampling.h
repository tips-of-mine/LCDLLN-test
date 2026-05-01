#pragma once

#include <cstdint>

namespace engine::render::terrain
{
	/// Resultat d'un sampling splat : couche dominante + poids (0..1, normalise) + flag valide.
	struct SplatSampleResult
	{
		/// Index de la couche dominante (0=herbe / R, 1=terre / G, 2=roc / B, 3=neige / A).
		/// 0 par defaut quand le sampling echoue (point hors terrain, donnees absentes...).
		uint32_t dominantLayer = 0u;
		/// Poids de la couche dominante apres normalisation des 4 channels (0..1).
		/// Permet a l'appelant de decider si le sampling est concluant (ex. seuil 0.5).
		float dominantWeight = 0.0f;
		/// Faux si la position est hors des bornes du terrain ou si les donnees CPU sont absentes.
		bool valid = false;
	};

	/// Echantillonne le splat RGBA sous la position monde (\p worldX, \p worldZ) et retourne
	/// la couche dominante. Sampling nearest (suffisant pour decider du son de pas - les
	/// transitions splat sont graduelles, le pas du joueur fait au moins 0.5 m).
	///
	/// Convention canaux : R = couche 0 (herbe), G = couche 1 (terre), B = couche 2 (roc),
	///                     A = couche 3 (neige). Identique a kSplatLayerCount = 4.
	///
	/// \param splatRgba         Buffer CPU RGBA8 (width * height * 4 octets, row-major).
	/// \param splatWidth        Largeur du splat en texels.
	/// \param splatHeight       Hauteur du splat en texels.
	/// \param terrainOriginX    Coordonnee monde X du coin (u=0, v=0) du splat.
	/// \param terrainOriginZ    Coordonnee monde Z du coin (u=0, v=0) du splat.
	/// \param terrainWorldSize  Taille monde du carre couvert par le splat (en metres).
	/// \param worldX, worldZ    Point monde a echantillonner.
	/// \return Resultat. \c valid == false si hors terrain / donnees absentes / parametres invalides.
	SplatSampleResult SampleDominantSplatLayerAtWorldXZ(const uint8_t* splatRgba,
		uint32_t splatWidth,
		uint32_t splatHeight,
		float terrainOriginX,
		float terrainOriginZ,
		float terrainWorldSize,
		float worldX,
		float worldZ);

} // namespace engine::render::terrain
