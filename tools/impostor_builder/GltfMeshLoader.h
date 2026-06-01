/// M45.4 — Chargement d'un mesh glTF statique via cgltf (outil offline).
/// Aucune dépendance moteur/Vulkan : cgltf est inclus en header-only.

#pragma once

#include "Rasterizer.h" // RasterVertex

#include <cstdint>
#include <string>
#include <vector>

namespace tools::impostor_builder
{
	/// Mesh chargé et aplati en un seul buffer de sommets + indices.
	struct LoadedMesh
	{
		std::vector<RasterVertex> vertices; ///< Sommets fusionnés (toutes primitives).
		std::vector<uint32_t>     indices;  ///< Indices de triangles (multiple de 3).
		float boundsMin[3] = {0.0f, 0.0f, 0.0f}; ///< Coin min de l'AABB monde.
		float boundsMax[3] = {0.0f, 0.0f, 0.0f}; ///< Coin max de l'AABB monde.
	};

	/// Charge un fichier glTF (.gltf/.glb) et fusionne ses primitives triangles.
	///
	/// Attributs lus par sommet :
	///   - POSITION  (obligatoire ; échec si absent).
	///   - NORMAL    (défaut (0,1,0) si absent).
	///   - COLOR_0   (défaut blanc opaque si absent ; RGB étendu en RGBA si vec3).
	/// Les textures et UV ne sont PAS échantillonnés en v1 (cf. FORMAT.md).
	///
	/// \param path Chemin du fichier glTF.
	/// \param out  Mesh résultant (sommets + indices + bounds).
	/// \param err  Message d'erreur lisible en cas d'échec.
	/// \return true si au moins une primitive triangle a été chargée.
	bool LoadGltf(const std::string& path, LoadedMesh& out, std::string& err);
}
