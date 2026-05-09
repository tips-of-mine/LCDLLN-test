#pragma once

#include "src/world_editor/ui/WorldMapEditDocument.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core
{
	class Config;
}

namespace engine::editor
{
	/// Une variante de forme (glTF relatif au content) pour une espèce d’arbre (ticket **013**).
	struct TreeSpeciesShapeSpec
	{
		std::string gltfContentRelativePath;
	};

	/// Espèce : identifiant, plage d’échelle, au moins une forme glTF.
	struct TreeSpeciesSpec
	{
		std::string id;
		double scaleMin = 0.8;
		double scaleMax = 1.2;
		std::vector<TreeSpeciesShapeSpec> shapes;
	};

	/// Catalogue `world_editor/tree_species_catalog.json` — espèces invalides ou fichiers manquants sont ignorées (log).
	class TreeSpeciesCatalog final
	{
	public:
		/// Charge et valide les chemins glTF sous \c paths.content. \p relativePath ex. \c "world_editor/tree_species_catalog.json".
		bool LoadFromFile(const engine::core::Config& cfg, std::string_view relativeCatalogPath, std::string& outError);

		[[nodiscard]] const std::vector<TreeSpeciesSpec>& Species() const { return m_species; }
		[[nodiscard]] size_t SpeciesCount() const { return m_species.size(); }

		[[nodiscard]] const TreeSpeciesSpec* FindById(std::string_view id) const;
		[[nodiscard]] int IndexOfId(std::string_view id) const;

		/// Si \c inst.speciesId est connu : clamp \c uniform_scale et \c shape_variant ; resynchronise \c gltf avec la forme.
		void SanitizeLayoutInstance(WorldMapEditLayoutInstance& inst) const;

	private:
		std::vector<TreeSpeciesSpec> m_species;
	};

} // namespace engine::editor
