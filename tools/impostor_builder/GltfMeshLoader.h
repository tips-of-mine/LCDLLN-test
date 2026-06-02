/// M45.4 — Chargement d'un mesh glTF statique via cgltf (outil offline).
/// Aucune dépendance moteur/Vulkan : cgltf est inclus en header-only.
/// FORMAT v2 : conserve les sous-meshes séparés (un par primitive) avec leur
/// matériau, et charge les textures baseColor (PNG voisins) via stb_image.

#pragma once

#include "Rasterizer.h" // RasterVertex

#include <cstdint>
#include <string>
#include <vector>

namespace tools::impostor_builder
{
	/// Matériau chargé : facteurs PBR + texture baseColor décodée (si présente).
	struct LoadedMaterial
	{
		std::vector<uint8_t> baseColorRGBA;       ///< Pixels RGBA8 baseColor (vide si pas de texture).
		int bcW = 0;                              ///< Largeur baseColor (0x0 si pas de texture).
		int bcH = 0;                              ///< Hauteur baseColor.
		float baseColorFactor[4] = {1, 1, 1, 1};  ///< Facteur multiplicatif RGBA.
		float metallic = 0.0f;                    ///< metallicFactor [0,1].
		float roughness = 1.0f;                   ///< roughnessFactor [0,1].
		bool  alphaBlendOrMask = false;           ///< alphaMode == BLEND ou MASK (active le cutout feuillage).
	};

	/// Sous-mesh : plage d'indices contiguë associée à un matériau.
	/// Permet de savoir quel matériau/texture s'applique à quel triangle.
	struct LoadedSubMesh
	{
		uint32_t firstIndex = 0;   ///< Offset du premier indice dans LoadedMesh::indices.
		uint32_t indexCount = 0;   ///< Nombre d'indices (multiple de 3).
		int      materialIndex = -1; ///< Index dans LoadedMesh::materials (-1 = aucun matériau).
	};

	/// Mesh chargé : sommets + indices globaux, découpés en sous-meshes par matériau.
	struct LoadedMesh
	{
		std::vector<RasterVertex>  vertices;  ///< Sommets fusionnés (toutes primitives).
		std::vector<uint32_t>      indices;   ///< Indices de triangles (multiple de 3).
		std::vector<LoadedSubMesh> subMeshes; ///< Plages d'indices + matériau par primitive.
		std::vector<LoadedMaterial> materials;///< Matériaux référencés par les sous-meshes.
		float boundsMin[3] = {0.0f, 0.0f, 0.0f}; ///< Coin min de l'AABB monde.
		float boundsMax[3] = {0.0f, 0.0f, 0.0f}; ///< Coin max de l'AABB monde.
	};

	/// Charge un fichier glTF (.gltf/.glb) en conservant les sous-meshes/matériaux.
	///
	/// Attributs lus par sommet :
	///   - POSITION   (obligatoire ; primitive ignorée si absente).
	///   - NORMAL     (défaut (0,1,0) si absent).
	///   - COLOR_0    (défaut blanc opaque si absent ; RGB étendu en RGBA si vec3).
	///   - TEXCOORD_0 (défaut (0,0) si absent).
	///
	/// Matériaux : metallic/roughness/baseColor factors + alphaMode lus depuis
	/// cgltf. Si une baseColorTexture est présente, son image (URI relative au
	/// dossier du .gltf) est chargée en RGBA8 via stbi_load. Une PNG manquante
	/// est non-fatale (warning sur stderr, matériau retombe sur baseColorFactor).
	///
	/// \param path Chemin du fichier glTF.
	/// \param out  Mesh résultant (sommets + indices + sous-meshes + matériaux + bounds).
	/// \param err  Message d'erreur lisible en cas d'échec.
	/// \return true si au moins une primitive triangle a été chargée.
	/// Effet de bord : peut écrire des warnings sur stderr (PNG introuvable).
	bool LoadGltf(const std::string& path, LoadedMesh& out, std::string& err);
}
