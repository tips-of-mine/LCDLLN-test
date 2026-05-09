#pragma once
// CMANGOS.05 (Phase 2.05c) — VMapManager : facade pour les requetes
// LOS / GetHeight / projectile contre les tiles vmap charges.
//
// Cette PR couvre :
//   - Charger un VMapTile (cf. VMapFormat) et construire la BIH des
//     triangles.
//   - IsInLineOfSight(p1, p2) : segment de droite, true si pas de
//     mesh entre les deux points.
//   - GetHeight(x, z) : hauteur Y du sol au-dessous de (x, z), via
//     raycast vertical descendant. Retourne nullopt si rien sous le
//     point ou hors tile.
//
// **Hors scope** :
//   - Multi-tile streaming (un seul tile pour cette PR — le manager
//     accepte un set de tiles mais ne les charge/decharge pas
//     automatiquement).
//   - DynamicTree (objets transformables) — viendra apres.
//   - Threading : non thread-safe pour cette PR. La BIH est immutable
//     une fois Load OK, donc lectures concurrentes sans modification
//     pourraient marcher en pratique mais on documente non-safe.

#include "src/shardd/internals/vmap/AABB.h"
#include "src/shardd/internals/vmap/BIH.h"
#include "src/shardd/internals/vmap/VMapFormat.h"

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::server::shard::vmap
{
	/// Wrapper "objet" autour d'un triangle pour la BIH. Stocke ses
	/// 3 sommets (par valeur) + une bbox precalculee. Cout : 3 Vec3 +
	/// 2 Vec3 = 60 octets par triangle.
	struct VMapTriObject
	{
		Vec3 v0{}, v1{}, v2{};
		AABB bounds{};

		AABB Bounds() const noexcept { return bounds; }
	};

	/// Manager d'un OU plusieurs tiles. Pour cette PR, charger un seul
	/// tile suffit (Load(tile) construit la BIH globale).
	class VMapManager
	{
	public:
		VMapManager() = default;

		/// Decode un buffer .vmap et construit la BIH. Retourne true sur
		/// succes. En cas d'echec (BadMagic, WrongVersion, etc.), l'etat
		/// interne reste vide.
		bool LoadTile(std::span<const uint8_t> blob);

		/// Charge directement depuis un VMapTile deja decode. Plus
		/// pratique pour les tests.
		void LoadTileDecoded(VMapTile tile);

		/// Retire le tile et libere la BIH.
		void Clear();

		/// True si un tile est charge.
		bool IsLoaded() const noexcept { return !m_triangles.empty(); }

		/// Bbox du tile charge (ou empty si vide).
		const AABB& Bounds() const noexcept { return m_bbox; }

		/// Test ligne-de-vue entre deux points world. Retourne true si
		/// AUCUN triangle n'intercepte le segment. Si pas de tile charge,
		/// retourne true (pas d'obstruction connue).
		bool IsInLineOfSight(const Vec3& p1, const Vec3& p2) const;

		/// Hauteur Y du sol au-dessous de (\p x, \p z), en raycastant
		/// depuis (\p x, maxY, \p z) vers le bas. Retourne le Y de la
		/// premiere intersection (le "sol"). nullopt si rien sous le
		/// point ou si pas de tile charge.
		///
		/// \param maxSearchHeight Hauteur de depart du raycast. Default
		///                       1000m (largement au-dessus de tout).
		std::optional<float> GetHeight(float x, float z,
			float maxSearchHeight = 1000.0f) const;

		/// Nombre de triangles indexes (utile pour metriques).
		size_t TriangleCount() const noexcept { return m_triangles.size(); }

	private:
		/// Test rayon-vs-triangle (Moller-Trumbore). Retourne true et
		/// remplit \p tHit si le rayon coupe le triangle.
		static bool IntersectRayTri(const Ray& r, const VMapTriObject& tri,
			float& tHit) noexcept;

		std::vector<VMapTriObject> m_triangles;
		BIH<VMapTriObject>         m_bih;
		AABB                       m_bbox = AABB::Empty();
	};
}
