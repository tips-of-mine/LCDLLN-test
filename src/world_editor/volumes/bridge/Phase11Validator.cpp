#include "src/world_editor/volumes/bridge/Phase11Validator.h"

#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/overhangs/OverhangCatalog.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"

#include <cmath>
#include <unordered_set>

namespace engine::editor::world::volumes::bridge
{
	namespace
	{
		void Push(ValidationReport& report, ValidationSeverity sev,
			std::string message, uint64_t guid)
		{
			ValidationIssue issue;
			issue.severity    = sev;
			issue.message     = std::move(message);
			issue.relatedGuid = guid;
			report.issues.push_back(std::move(issue));
			switch (sev)
			{
				case ValidationSeverity::Error:   ++report.errorCount;   break;
				case ValidationSeverity::Warning: ++report.warningCount; break;
				case ValidationSeverity::Info:    ++report.infoCount;    break;
			}
		}

		bool IsFiniteVec3(const engine::math::Vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		/// true si `gltfPath` existe dans un des catalogues mesh chargés.
		bool MeshPathInAnyCatalog(const std::string& gltfPath,
			const caves::CaveCatalog* cave,
			const overhangs::OverhangCatalog* overhang,
			const arches::ArchCatalog* arch)
		{
			if (cave != nullptr)
			{
				for (const auto& e : cave->Entries())
					if (e.gltfRelativePath == gltfPath) return true;
			}
			if (overhang != nullptr)
			{
				for (const auto& e : overhang->Entries())
					if (e.gltfRelativePath == gltfPath) return true;
			}
			if (arch != nullptr)
			{
				for (const auto& e : arch->Entries())
					if (e.gltfRelativePath == gltfPath) return true;
			}
			return false;
		}
	}

	ValidationReport Phase11Validator::Validate(const MeshInsertDocument& meshDoc,
		const dungeons::DungeonPortalDocument& portalDoc) const
	{
		ValidationReport report;

		// --- Mesh inserts (LCMI : caves / overhangs / arches) ---
		std::unordered_set<uint64_t> seenMeshGuids;
		for (const auto& inst : meshDoc.All())
		{
			if (!seenMeshGuids.insert(inst.guid).second)
			{
				Push(report, ValidationSeverity::Error,
					"Mesh insert : guid dupliqué " + std::to_string(inst.guid),
					inst.guid);
			}
			if (inst.guid == 0u)
			{
				Push(report, ValidationSeverity::Error,
					"Mesh insert : guid 0 invalide (sentinelle)", 0u);
			}
			if (inst.gltfRelativePath.empty())
			{
				Push(report, ValidationSeverity::Error,
					"Mesh insert : gltfRelativePath vide", inst.guid);
			}
			if (!IsFiniteVec3(inst.worldPosition))
			{
				Push(report, ValidationSeverity::Error,
					"Mesh insert : worldPosition NaN/infinie", inst.guid);
			}
			if (inst.uniformScale <= 0.0f)
			{
				Push(report, ValidationSeverity::Error,
					"Mesh insert : uniformScale ≤ 0", inst.guid);
			}

			const bool knownCategory =
				   inst.insertCategory == "cave"
				|| inst.insertCategory == "overhang"
				|| inst.insertCategory == "arch"
				|| inst.insertCategory == "dungeon";
			if (!knownCategory)
			{
				Push(report, ValidationSeverity::Warning,
					"Mesh insert : insertCategory inconnue '" + inst.insertCategory + "'",
					inst.guid);
			}

			// Asset fantôme : path non vide mais absent de tous les catalogues.
			if (!inst.gltfRelativePath.empty()
				&& !MeshPathInAnyCatalog(inst.gltfRelativePath,
					m_caveCatalog, m_overhangCatalog, m_archCatalog))
			{
				Push(report, ValidationSeverity::Warning,
					"Mesh insert : gltf '" + inst.gltfRelativePath
						+ "' absent des catalogues chargés (asset fantôme ?)",
					inst.guid);
			}
		}

		// --- Dungeon portals (LCDP) ---
		std::unordered_set<uint64_t> seenPortalGuids;
		for (const auto& portal : portalDoc.All())
		{
			if (!seenPortalGuids.insert(portal.guid).second)
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : guid dupliqué " + std::to_string(portal.guid),
					portal.guid);
			}
			if (portal.guid == 0u)
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : guid 0 invalide", 0u);
			}
			if (portal.dungeonTemplateId.empty())
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : dungeonTemplateId vide", portal.guid);
			}
			if (!IsFiniteVec3(portal.worldPosition))
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : worldPosition NaN/infinie", portal.guid);
			}
			if (portal.triggerRadius <= 0.0f)
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : triggerRadius ≤ 0 (portail inactivable)",
					portal.guid);
			}
			if (portal.minDifficulty == 0u || portal.maxDifficulty < portal.minDifficulty)
			{
				Push(report, ValidationSeverity::Error,
					"Portail donjon : difficulty range incohérent (min="
						+ std::to_string(portal.minDifficulty) + " max="
						+ std::to_string(portal.maxDifficulty) + ")",
					portal.guid);
			}

			// Référence catalogue : le master rejettera EnterDungeon avec
			// kEnterDungeonErrorTemplateNotFound si le template est inconnu.
			if (m_dungeonCatalog != nullptr && !portal.dungeonTemplateId.empty())
			{
				const auto* entry = m_dungeonCatalog->FindById(portal.dungeonTemplateId);
				if (entry == nullptr)
				{
					Push(report, ValidationSeverity::Error,
						"Portail donjon : dungeonTemplateId '" + portal.dungeonTemplateId
							+ "' absent du catalogue (le master rejettera EnterDungeon)",
						portal.guid);
				}
				else
				{
					if (portal.minDifficulty < entry->minDifficulty
						|| portal.maxDifficulty > entry->maxDifficulty)
					{
						Push(report, ValidationSeverity::Warning,
							"Portail donjon : difficulty hors range du catalogue pour '"
								+ portal.dungeonTemplateId + "'",
							portal.guid);
					}
					if (portal.requiredLevel < entry->requiredLevel)
					{
						Push(report, ValidationSeverity::Info,
							"Portail donjon : requiredLevel ("
								+ std::to_string(portal.requiredLevel)
								+ ") sous le minimum catalogue ("
								+ std::to_string(entry->requiredLevel) + ")",
							portal.guid);
					}
				}
			}
			else if (m_dungeonCatalog == nullptr && !portal.dungeonTemplateId.empty())
			{
				Push(report, ValidationSeverity::Warning,
					"Portail donjon : catalogue de donjons non chargé — "
					"impossible de valider dungeonTemplateId",
					portal.guid);
			}
		}

		// --- Synthèse ---
		if (meshDoc.Size() == 0u && portalDoc.Size() == 0u)
		{
			Push(report, ValidationSeverity::Info,
				"Aucun volume placé — l'export VMap produira un fichier vide", 0u);
		}

		return report;
	}
}
