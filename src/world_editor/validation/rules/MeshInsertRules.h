#pragma once

// M100.48 — Règles de validation des mesh inserts (caves/overhangs/arches/
// dungeons, M100.40-43). Opèrent sur le vecteur d'instances exposé par le
// ValidationContext.

#include "src/world_editor/validation/IValidationRule.h"

namespace engine::editor::world::validation
{
	/// `mesh_inserts.gltf_missing` (Error) : instance dont le chemin glTF est
	/// vide (référence d'asset invalide → le runtime échouerait au chargement).
	class MeshInsertGltfMissingRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "mesh_inserts.gltf_missing"; }
		const char* GetCategory() const override { return "mesh_inserts"; }
		const char* GetDescription() const override { return "Mesh insert sans chemin glTF (asset manquant)."; }
		Severity GetDefaultSeverity() const override { return Severity::Error; }
		std::vector<std::string> GetDocumentTags() const override { return { "mesh_inserts" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};

	/// `mesh_inserts.duplicate_guid` (Error) : deux instances partagent le même
	/// guid (ex. donjons à ID dupliqué → résolution d'instance ambiguë).
	class MeshInsertDuplicateGuidRule : public IValidationRule
	{
	public:
		const char* GetRuleId() const override { return "mesh_inserts.duplicate_guid"; }
		const char* GetCategory() const override { return "mesh_inserts"; }
		const char* GetDescription() const override { return "Deux mesh inserts avec le même guid."; }
		Severity GetDefaultSeverity() const override { return Severity::Error; }
		std::vector<std::string> GetDocumentTags() const override { return { "mesh_inserts" }; }
		void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const override;
	};
}
