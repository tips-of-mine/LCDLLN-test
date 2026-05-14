#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::caves    { class CaveCatalog; }
namespace engine::editor::world::volumes::overhangs { class OverhangCatalog; }
namespace engine::editor::world::volumes::arches   { class ArchCatalog; }
namespace engine::editor::world::volumes::dungeons
{
	class DungeonPortalDocument;
	class DungeonCatalog;
}

namespace engine::editor::world::volumes::bridge
{
	/// Sévérité d'un problème détecté par le validateur Phase 11.
	enum class ValidationSeverity : uint8_t
	{
		Info    = 0,  ///< informatif, n'empêche pas l'export
		Warning = 1,  ///< suspect, à vérifier — l'export reste possible
		Error   = 2,  ///< bloquant, l'export VMap doit être refusé
	};

	/// Un problème unitaire remonté par le validateur.
	struct ValidationIssue
	{
		ValidationSeverity severity = ValidationSeverity::Info;
		std::string        message;        ///< description lisible (FR)
		uint64_t           relatedGuid = 0u; ///< instance concernée (0 = global)
	};

	/// Rapport agrégé d'une passe de validation Phase 11.
	struct ValidationReport
	{
		std::vector<ValidationIssue> issues;
		size_t errorCount   = 0u;
		size_t warningCount = 0u;
		size_t infoCount    = 0u;

		bool HasBlockingErrors() const { return errorCount > 0u; }
	};

	/// Validateur de cohérence Phase 11 (M100.44, clôture). Vérifie
	/// l'intégrité de tous les fichiers de volumes (LCMI mesh inserts +
	/// LCDP dungeon portals) avant l'export VMap. Détecte :
	///   - guids dupliqués au sein d'un document,
	///   - mesh insert avec `gltfRelativePath` vide,
	///   - mesh insert dont la catégorie ne correspond à aucun catalogue
	///     chargé (référence orpheline),
	///   - mesh insert dont le `gltfRelativePath` n'existe dans aucun
	///     catalogue (asset fantôme),
	///   - portail de donjon dont le `dungeonTemplateId` est absent du
	///     `DungeonCatalog` (le master rejettera EnterDungeon),
	///   - portail avec `minDifficulty > maxDifficulty` ou hors range
	///     du catalogue,
	///   - `triggerRadius` ≤ 0 (portail inactivable),
	///   - positions monde NaN / infinies.
	///
	/// Le validateur est pur (pas d'I/O) — les documents et catalogues
	/// sont fournis déjà chargés. Le panel éditeur (M100.44) appelle
	/// `Validate` et affiche le rapport ; l'export VMap est gardé
	/// derrière `!report.HasBlockingErrors()`.
	class Phase11Validator
	{
	public:
		void SetCaveCatalog(const caves::CaveCatalog* cat)            { m_caveCatalog = cat; }
		void SetOverhangCatalog(const overhangs::OverhangCatalog* cat) { m_overhangCatalog = cat; }
		void SetArchCatalog(const arches::ArchCatalog* cat)           { m_archCatalog = cat; }
		void SetDungeonCatalog(const dungeons::DungeonCatalog* cat)   { m_dungeonCatalog = cat; }

		/// Lance la passe de validation. Renvoie un rapport agrégé.
		ValidationReport Validate(const MeshInsertDocument& meshDoc,
			const dungeons::DungeonPortalDocument& portalDoc) const;

	private:
		const caves::CaveCatalog*         m_caveCatalog     = nullptr;
		const overhangs::OverhangCatalog* m_overhangCatalog = nullptr;
		const arches::ArchCatalog*        m_archCatalog     = nullptr;
		const dungeons::DungeonCatalog*   m_dungeonCatalog  = nullptr;
	};
}
