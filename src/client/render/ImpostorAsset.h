#pragma once

// src/client/render/ImpostorAsset.h (M45.5 — rendu RUNTIME des impostors végétation)
//
// Loader runtime du format binaire d'atlas d'impostors octaédriques produit par
// l'outil offline `tools/impostor_builder` (M45.4, voir tools/impostor_builder/
// FORMAT.md). Détient les deux textures GPU (albedo sRGB + normal linéaire)
// uploadées via `AssetRegistry::CreateTextureFromMemory`, ainsi que les
// métadonnées de grille (viewsPerAxis, tileSize, bounds) nécessaires au shader.
//
// IMPORTANT : la lecture du format est RÉPLIQUÉE ici champ-par-champ en
// little-endian (cf. ImpostorFormat.h) pour NE PAS dépendre du code de l'outil
// (`tools::impostor_builder`), qui n'est pas linké dans engine_core.

#include "src/client/render/AssetRegistry.h"

#include <cstdint>
#include <string>

namespace engine::render
{
	/// Métadonnées runtime décrivant la grille d'atlas + les bounds du mesh source.
	/// Réplique le sous-ensemble utile de `tools::impostor_builder::ImpostorAtlasInfo`.
	struct ImpostorAtlasInfoRuntime
	{
		uint32_t viewsPerAxis = 0u;   ///< N : la grille fait N×N tiles (vues).
		uint32_t tileSize     = 0u;   ///< Côté d'une tile en pixels (carrée).
		float    boundsMin[3] = {0.0f, 0.0f, 0.0f}; ///< Coin min de l'AABB monde du mesh.
		float    boundsMax[3] = {0.0f, 0.0f, 0.0f}; ///< Coin max de l'AABB monde du mesh.
	};

	/// Détenteur d'un atlas d'impostors chargé : métadonnées + 2 textures GPU.
	/// Cycle de vie : `LoadFromFile` une fois (au chargement du décor si
	/// `world.impostor.enabled`), lecture via `Albedo()`/`Normal()`/`Info()` à
	/// chaque frame de rendu. Les textures appartiennent au `AssetRegistry`
	/// passé à `LoadFromFile` (libérées au `Destroy` du registry) — cette classe
	/// ne possède donc rien à détruire elle-même (handles non-owning).
	class ImpostorAsset
	{
	public:
		ImpostorAsset() = default;

		/// Lit le fichier `.mipo` à `absOrContentPath` (chemin disque ABSOLU ou
		/// relatif au cwd — la résolution est faite par l'appelant), valide le
		/// magic/version, extrait `ImpostorAtlasInfo` + albedo[] + normal[], puis
		/// uploade les deux atlas en VkImage via `reg.CreateTextureFromMemory`
		/// (albedo en sRGB, normal en linéaire/UNORM).
		///
		/// Effet de bord : crée deux textures GPU dans `reg`. À appeler en main
		/// thread (uploads synchrones internes au registry).
		///
		/// \param absOrContentPath Chemin disque du fichier `.mipo`.
		/// \param reg              Registry cible pour l'upload des textures.
		/// \param err              Message d'erreur lisible en cas d'échec.
		/// \return true si lecture + validation + upload ont réussi.
		bool LoadFromFile(const std::string& absOrContentPath, AssetRegistry& reg, std::string& err);

		/// True si `LoadFromFile` a réussi et les deux textures sont valides.
		bool IsValid() const;

		/// Métadonnées de la grille (valides seulement si `IsValid()`).
		const ImpostorAtlasInfoRuntime& Info() const { return m_info; }

		/// Handle de la texture albedo (sRGB). Invalide si non chargé.
		TextureHandle Albedo() const { return m_albedo; }
		/// Handle de la texture normal (linéaire/UNORM). Invalide si non chargé.
		TextureHandle Normal() const { return m_normal; }

	private:
		ImpostorAtlasInfoRuntime m_info{};
		TextureHandle            m_albedo;
		TextureHandle            m_normal;
		bool                     m_valid = false;
	};
}
