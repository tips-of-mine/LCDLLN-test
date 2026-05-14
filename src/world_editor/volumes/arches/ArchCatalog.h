#pragma once

#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::editor::world::volumes::arches
{
	/// Entrée du catalogue d'arches naturelles (M100.42). L'arche est
	/// définie par deux pieds (`archAnchorA`, `archAnchorB`, pivot-relatifs)
	/// et une hauteur (`archHeight`, distance entre la corde des deux
	/// pieds et le sommet de l'arc). Le tool calcule yaw + scale uniforme
	/// à partir de deux points monde cliqués pour aligner l'asset avec
	/// le span demandé.
	struct ArchCatalogEntry
	{
		std::string        id;                  // ex: "arch_small_01"
		std::string        gltfRelativePath;
		std::string        displayName;
		std::string        thumbnailPath;
		engine::math::Vec3 aabbMin;
		engine::math::Vec3 aabbMax;
		engine::math::Vec3 archAnchorA;         // pied A, pivot-relatif
		engine::math::Vec3 archAnchorB;         // pied B, pivot-relatif
		float              archHeight = 4.0f;   // hauteur clé d'arc (m)
		/// Span natif du mesh = distance XZ entre archAnchorA et archAnchorB.
		/// Sert de référence pour calculer le scale uniforme nécessaire
		/// à matcher le span monde cliqué.
		float NativeSpanMeters() const
		{
			const float dx = archAnchorB.x - archAnchorA.x;
			const float dz = archAnchorB.z - archAnchorA.z;
			return std::sqrt(dx * dx + dz * dz);
		}
	};

	/// Loader JSON minimal pour `game/data/meshes/arches/catalog.json`
	/// (M100.42). Pattern hand-rolled identique à CaveCatalog/OverhangCatalog.
	class ArchCatalog
	{
	public:
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);
		bool ParseJson(const std::string& jsonText, std::string& outError);

		const std::vector<ArchCatalogEntry>& Entries() const { return m_entries; }
		size_t Size() const { return m_entries.size(); }

		const ArchCatalogEntry* FindById(const std::string& id) const;

	private:
		std::vector<ArchCatalogEntry> m_entries;
	};
}
