/// M45.4 — Mini-rasterizer CPU autonome (pas de Vulkan, pas de moteur).
///
/// Rasterise des triangles dans une tile RGBA8 (albedo + normale encodée + ORM)
/// avec z-buffer par tile. Projection orthographique fournie par l'appelant
/// sous forme d'une view-projection 4x4 (column-major, NDC Vulkan : Y vers le
/// bas, Z dans [0,1]). Le rasterizer écrit directement dans des sous-régions
/// (tiles) d'atlas plus larges.
///
/// FORMAT v2 : trois atlas sont produits (albedo, normal, orm). L'échantillonnage
/// albedo provient des TEXTURES matériau (baseColorTexture) interpolées via les UV
/// du sommet, avec alpha cutout pour le feuillage. Anti-aliasing par supersampling
/// (SS=2) géré au niveau de l'appelant qui rend une tile SS× puis box-downsample.

#pragma once

#include <cstdint>
#include <vector>

namespace tools::impostor_builder
{
	/// Sommet d'entrée du rasterizer : position monde, normale monde, couleur RGBA, UV.
	struct RasterVertex
	{
		float pos[3]    = {0.0f, 0.0f, 0.0f};
		float normal[3] = {0.0f, 1.0f, 0.0f};
		float color[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
		float uv[2]     = {0.0f, 0.0f}; ///< TEXCOORD_0 (défaut (0,0) si absent).
	};

	/// Matériau courant utilisé par le rasterizer pour échantillonner l'albedo et
	/// remplir l'atlas ORM. Pointe sur les pixels d'une texture baseColor déjà
	/// chargée (RGBA8, origine haut-gauche), ou bcW=bcH=0 si pas de texture
	/// (l'albedo se réduit alors au baseColorFactor).
	struct RasterMaterial
	{
		const uint8_t* baseColorRGBA = nullptr; ///< Pixels baseColor RGBA8 (peut être nullptr).
		int bcW = 0;                            ///< Largeur texture baseColor (0 = pas de texture).
		int bcH = 0;                            ///< Hauteur texture baseColor.
		float baseColorFactor[4] = {1, 1, 1, 1};///< Multiplie l'albedo échantillonné (RGBA).
		float metallic = 0.0f;                  ///< metallicFactor [0,1] -> atlas orm.b.
		float roughness = 1.0f;                 ///< roughnessFactor [0,1] -> atlas orm.g.
		bool  alphaCutout = false;              ///< true si BLEND/MASK : applique le cutout coverage<0.5.
	};

	/// Cible de rendu d'une tile : pointeurs vers les TROIS atlas et placement.
	/// Les atlas font (atlasWidth*atlasWidth) texels RGBA8 ; la tile occupe le
	/// carré [tileX*tileSize, tileX*tileSize+tileSize) × [tileY*tileSize, …).
	/// IMPORTANT : pour l'anti-aliasing, ces atlas sont les atlas SUPERSAMPLÉS
	/// (atlasWidth et tileSize incluent déjà le facteur SS) ; le downsample vers
	/// la résolution finale est fait par l'appelant après rasterisation.
	struct RasterTarget
	{
		uint8_t* albedo  = nullptr; ///< Atlas albedo RGBA8 : rgb=albedo, a=couverture.
		uint8_t* normal  = nullptr; ///< Atlas normal RGBA8 : rgb=normale encodée, a=depth relative.
		uint8_t* orm     = nullptr; ///< Atlas ORM RGBA8 : r=AO, g=roughness, b=metallic, a=255.
		uint32_t atlasWidth = 0;    ///< Côté de l'atlas en texels (= viewsPerAxis*tileSize).
		uint32_t tileSize   = 0;    ///< Côté d'une tile en texels.
		uint32_t tileX      = 0;    ///< Index colonne de la tile (0..viewsPerAxis-1).
		uint32_t tileY      = 0;    ///< Index ligne de la tile (0..viewsPerAxis-1).
	};

	/// Efface la tile (les trois atlas) en transparent et réinitialise le z-buffer.
	///
	/// À appeler UNE FOIS par tile, AVANT de rasteriser les sous-meshes successifs
	/// (qui partagent le même z-buffer). Sépare l'effacement de la rasterisation
	/// pour que plusieurs sous-meshes (matériaux différents) s'accumulent dans la
	/// même tile avec un test de profondeur commun.
	///
	/// \param target Tile cible (atlas + placement).
	/// \param zbuf   Z-buffer de tileSize*tileSize floats, mis à +inf.
	/// Effet de bord : écrit dans la sous-région tile des trois atlas.
	void ClearTile(const RasterTarget& target, std::vector<float>& zbuf);

	/// Rasterise une liste de triangles indexés d'UN sous-mesh dans la tile cible.
	///
	/// N'efface PAS la tile : appeler ClearTile une fois avant la série de
	/// sous-meshes. Le z-buffer est partagé entre sous-meshes (occlusion correcte).
	///
	/// \param verts     Sommets (position/normale/couleur/UV monde).
	/// \param indices   Indices de triangles du sous-mesh (multiple de 3).
	/// \param viewProj  Matrice view-projection orthographique 16 floats column-major.
	/// \param target    Tile cible (atlas + placement).
	/// \param zbuf      Z-buffer partagé (déjà initialisé par ClearTile).
	/// \param mat       Matériau du sous-mesh (texture baseColor + facteurs ORM).
	/// \param depthNear Borne min de profondeur NDC [0,1] pour normaliser la depth.
	/// \param depthFar  Borne max de profondeur NDC [0,1] pour normaliser la depth.
	///
	/// Effet de bord : écrit dans la sous-région tile des trois atlas.
	/// Atlas albedo : rgb = albedo (texAlbedo.rgb * baseColorFactor.rgb), a = 255
	///   (couverture). Alpha cutout : si la couverture interpolée < 0.5, le
	///   fragment N'EST PAS écrit (préserve le z-buffer).
	/// Atlas normal : rgb = normale monde encodée (n*0.5+0.5), a = depth relative
	///   normalisée sur [depthNear, depthFar] (0 = près du plan de vue, 255 = loin).
	/// Atlas orm   : r = AO (255), g = roughness*255, b = metallic*255, a = 255.
	void RasterizeSubMesh(const std::vector<RasterVertex>& verts,
	                      const std::vector<uint32_t>& indices,
	                      const float viewProj[16],
	                      const RasterTarget& target,
	                      std::vector<float>& zbuf,
	                      const RasterMaterial& mat,
	                      float depthNear,
	                      float depthFar);
}
