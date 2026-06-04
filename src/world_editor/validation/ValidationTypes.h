#pragma once

// M100.48 — Zone Validation Service : types partagés (sévérité, problème,
// contexte). Validation ÉDITEUR-ONLY, lecture seule sur les données de zone.
//
// Le ValidationContext expose des VUES en lecture seule sur les données de la
// zone (heightmap, splat, mesh inserts) plutôt que sur les classes Document de
// l'éditeur : cela garde le service découplé, header-only-friendly et testable
// headless. L'éditeur remplit le contexte depuis ses documents avant d'appeler
// le validateur.

#include <cstdint>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::terrain { struct TerrainChunk; struct SplatMap; }
namespace engine::editor::world::volumes { struct MeshInsertInstance; }

namespace engine::editor::world::validation
{
	/// Sévérité d'un problème de validation. Trois niveaux tranchés (cf. ticket).
	enum class Severity : uint8_t
	{
		Hint    = 0, ///< Suggestion de style/best-practice. N'empêche rien.
		Warning = 1, ///< Impacte gameplay/visuel mais n'empêche pas l'export.
		Error   = 2, ///< Contenu corrompu/incomplet : empêche l'export.
	};

	/// Un problème détecté par une règle de validation.
	struct ValidationIssue
	{
		std::string        ruleId;        ///< Ex. "heightmap.holes".
		std::string        title;         ///< Titre court.
		std::string        description;   ///< 1-2 phrases.
		Severity           severity = Severity::Hint;
		engine::math::Vec3 worldPosition{ 0.0f, 0.0f, 0.0f }; ///< Pour "Aller à".
		uint64_t           targetGuid = 0u; ///< Entité concernée (0 = aucune).
		std::string        docSectionId;  ///< Lien doc M100.47 (optionnel).
		std::string        suggestedFix;  ///< Texte court (optionnel).
	};

	/// Vue en lecture seule des données de zone à valider. Tous les pointeurs
	/// sont optionnels (null = donnée absente, les règles concernées passent).
	struct ValidationContext
	{
		/// Heightmap : un chunk par entrée (MVP mono-zone). Position monde du
		/// coin du chunk pour situer les problèmes.
		struct TerrainEntry
		{
			const engine::world::terrain::TerrainChunk* chunk = nullptr;
			engine::math::Vec3 originWorld{ 0.0f, 0.0f, 0.0f };
		};

		std::vector<TerrainEntry>                                           terrainChunks;
		const engine::world::terrain::SplatMap*                             splat = nullptr;
		engine::math::Vec3                                                  splatOriginWorld{ 0.0f, 0.0f, 0.0f };
		const std::vector<engine::editor::world::volumes::MeshInsertInstance>* meshInserts = nullptr;
	};
}
