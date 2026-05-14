#pragma once

#include "src/shared/math/Math.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/overhangs/OverhangCatalog.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::dungeons
{
	class DungeonPortalDocument;
}

namespace engine::editor::world::volumes::bridge
{
	/// Boîte de collision proxy world-space (M100.44). Une AABB grossière
	/// par volume éditeur, exportée pour que le shard ait une collision
	/// approximative (LOS, projectiles) avant que l'extraction mesh fine
	/// glTF → triangles ne soit disponible.
	struct VolumeAabbProxy
	{
		uint64_t           sourceGuid     = 0u;   // guid de l'instance source
		uint8_t            volumeKind     = 0u;   // cf. kVolumeKind*
		engine::math::Vec3 worldMin;
		engine::math::Vec3 worldMax;
	};

	/// Discriminant `volumeKind`.
	inline constexpr uint8_t kVolumeKindCave          = 0u;
	inline constexpr uint8_t kVolumeKindOverhang      = 1u;
	inline constexpr uint8_t kVolumeKindArch          = 2u;
	inline constexpr uint8_t kVolumeKindDungeonPortal = 3u;
	inline constexpr uint8_t kVolumeKindUnknown       = 255u;

	/// Magic du format `instances/volume_collision.bin` ("LCVC" — Lcdlln
	/// Volume Collision).
	constexpr uint32_t kVolumeCollisionMagic   = 0x4356434Cu; // 'L' 'C' 'V' 'C'
	constexpr uint32_t kVolumeCollisionVersion = 1u;

	/// Transforme une AABB locale (pivot-relatif, issue d'un catalogue)
	/// en AABB world-space après application de `worldPosition`, d'un
	/// `yawDeg` (rotation autour de Y monde) et d'un `uniformScale`.
	/// Les 8 coins sont transformés puis englobés — l'AABB résultante est
	/// donc un sur-ensemble conservateur (jamais sous-estimée).
	///
	/// \param localMin/localMax  AABB pivot-relatif.
	/// \param worldPosition      translation monde du pivot.
	/// \param yawDeg             rotation Y en degrés.
	/// \param uniformScale       facteur d'échelle uniforme (> 0).
	/// \param outMin/outMax      AABB world-space résultante.
	void TransformLocalAabbToWorld(
		const engine::math::Vec3& localMin, const engine::math::Vec3& localMax,
		const engine::math::Vec3& worldPosition, float yawDeg, float uniformScale,
		engine::math::Vec3& outMin, engine::math::Vec3& outMax);

	/// Pont éditeur → collision serveur (M100.44, clôture Phase 11).
	///
	/// Lit les documents de volumes (`MeshInsertDocument` LCMI M100.40-42
	/// + `DungeonPortalDocument` LCDP M100.43) et, à l'aide des catalogues
	/// qui portent les AABB locales, produit une liste de
	/// `VolumeAabbProxy` world-space. La sortie est sérialisée dans
	/// `instances/volume_collision.bin` (LCVC v1) — un fichier que le
	/// shard pourra charger comme couche d'occludeurs grossiers en
	/// attendant l'extraction mesh fine.
	///
	/// MVP : proxy AABB uniquement (pas de triangle soup). Les portails
	/// de donjon n'ont pas de mesh associé → leur proxy est un cube de
	/// côté `2 * triggerRadius` centré sur la position.
	class VMapBridge
	{
	public:
		void SetCaveCatalog(const caves::CaveCatalog* cat)         { m_caveCatalog = cat; }
		void SetOverhangCatalog(const overhangs::OverhangCatalog* cat) { m_overhangCatalog = cat; }
		void SetArchCatalog(const arches::ArchCatalog* cat)        { m_archCatalog = cat; }

		/// Construit les proxies depuis les deux documents. Vide `Proxies()`
		/// puis le repeuple. Les volumes dont le catalogue est absent ou
		/// dont l'entrée catalogue est introuvable produisent quand même
		/// un proxy dégénéré (cube unitaire à la position) — ils sont
		/// comptés dans `outUnresolvedCount`.
		void Build(const MeshInsertDocument& meshDoc,
			const dungeons::DungeonPortalDocument& portalDoc,
			size_t& outUnresolvedCount);

		const std::vector<VolumeAabbProxy>& Proxies() const { return m_proxies; }
		size_t Size() const { return m_proxies.size(); }

		/// Sérialise les proxies courants au format LCVC v1.
		bool Serialize(std::vector<uint8_t>& outBytes, std::string& outError) const;

		/// Désérialise un buffer LCVC v1 (utilisé par les tests + un futur
		/// loader shard). Valide magic + version.
		static bool Deserialize(std::span<const uint8_t> bytes,
			std::vector<VolumeAabbProxy>& outProxies, std::string& outError);

		/// Écrit `instances/volume_collision.bin` sous le content root.
		bool WriteToDisk(const std::string& contentRoot, std::string& outError) const;

	private:
		const caves::CaveCatalog*         m_caveCatalog     = nullptr;
		const overhangs::OverhangCatalog* m_overhangCatalog = nullptr;
		const arches::ArchCatalog*        m_archCatalog     = nullptr;

		std::vector<VolumeAabbProxy> m_proxies;
	};
}
