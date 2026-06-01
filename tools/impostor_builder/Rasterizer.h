/// M45.4 — Mini-rasterizer CPU autonome (pas de Vulkan, pas de moteur).
///
/// Rasterise des triangles dans une tile RGBA8 (albedo + normale encodée)
/// avec z-buffer par tile. Projection orthographique fournie par l'appelant
/// sous forme d'une view-projection 4x4 (column-major, NDC Vulkan : Y vers le
/// bas, Z dans [0,1]). Le rasterizer écrit directement dans des sous-régions
/// (tiles) d'atlas plus larges.

#pragma once

#include <cstdint>
#include <vector>

namespace tools::impostor_builder
{
	/// Sommet d'entrée du rasterizer : position monde, normale monde, couleur RGBA.
	struct RasterVertex
	{
		float pos[3]    = {0.0f, 0.0f, 0.0f};
		float normal[3] = {0.0f, 1.0f, 0.0f};
		float color[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
	};

	/// Cible de rendu d'une tile : pointeurs vers les atlas et placement de la tile.
	/// Les atlas font (atlasWidth*atlasWidth) texels RGBA8 ; la tile occupe le
	/// carré [tileX*tileSize, tileX*tileSize+tileSize) × [tileY*tileSize, …).
	struct RasterTarget
	{
		uint8_t* albedo  = nullptr; ///< Atlas albedo RGBA8 (atlasWidth^2 * 4 octets).
		uint8_t* normal  = nullptr; ///< Atlas normal RGBA8 (atlasWidth^2 * 4 octets).
		uint32_t atlasWidth = 0;    ///< Côté de l'atlas en texels (= viewsPerAxis*tileSize).
		uint32_t tileSize   = 0;    ///< Côté d'une tile en texels.
		uint32_t tileX      = 0;    ///< Index colonne de la tile (0..viewsPerAxis-1).
		uint32_t tileY      = 0;    ///< Index ligne de la tile (0..viewsPerAxis-1).
	};

	/// Rasterise une liste de triangles indexés dans la tile cible.
	///
	/// \param verts     Sommets (position/normale/couleur monde).
	/// \param indices   Indices de triangles (multiple de 3).
	/// \param viewProj  Matrice view-projection orthographique 16 floats column-major.
	/// \param target    Tile cible (atlas + placement).
	/// \param zbuf      Z-buffer temporaire de tileSize*tileSize floats, réinitialisé
	///                  en interne à +inf avant rasterisation.
	///
	/// Effet de bord : écrit dans la sous-région tile des atlas albedo/normal.
	/// L'alpha de la normale vaut 255 si le texel est couvert (masque), 0 sinon.
	/// L'encodage normale est n*0.5+0.5 mappé en [0,255]. Les texels non couverts
	/// de la tile sont remis à 0 (transparent) au début.
	void RasterizeTile(const std::vector<RasterVertex>& verts,
	                   const std::vector<uint32_t>& indices,
	                   const float viewProj[16],
	                   const RasterTarget& target,
	                   std::vector<float>& zbuf);
}
