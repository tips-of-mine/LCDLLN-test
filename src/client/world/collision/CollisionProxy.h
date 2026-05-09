// src/client/world/collision/CollisionProxy.h
#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::world::collision
{
	/// Type discriminé du proxy de collision (M100.12). Trois niveaux selon
	/// la complexité du mesh source et le contexte d'usage.
	enum class ProxyType : uint32_t
	{
		Capsule    = 0,
		ConvexHull = 1,
		TriMesh    = 2,
	};

	/// Magic du fichier `<asset>.collision.bin` ("COLL" little-endian).
	/// Distinct des autres formats versionnés du projet (terrain.bin = TRRN,
	/// splat.bin = SLAT, zone.meta = ZONE, etc.).
	constexpr uint32_t kCollisionMagic   = 0x4C4C4F43u;
	/// Version courante du payload `.collision.bin` (M100.12).
	constexpr uint32_t kCollisionVersion = 1u;

	/// Proxy de collision pour un asset mesh (M100.12). Utilise un sous-ensemble
	/// des champs selon `type` :
	///   - Capsule    : capsuleA, capsuleB, capsuleRadius
	///   - ConvexHull : vertices (4-N points)
	///   - TriMesh    : vertices + indices (3 indices par triangle)
	///
	/// Le format binaire `<asset>.collision.bin` utilise `OutputVersionHeader`
	/// (24 bytes : magic, formatVersion, builderVersion, engineVersion,
	/// contentHash xxhash64) suivi du payload type-dependent.
	struct CollisionProxy
	{
		ProxyType type = ProxyType::Capsule;

		// Capsule (utilisé si type == Capsule)
		engine::math::Vec3 capsuleA{ 0.0f, -0.5f, 0.0f };
		engine::math::Vec3 capsuleB{ 0.0f,  0.5f, 0.0f };
		float              capsuleRadius = 0.5f;

		// ConvexHull / TriMesh (utilisés selon type)
		std::vector<engine::math::Vec3> vertices;
		std::vector<uint32_t>           indices;     // TriMesh seulement

		/// Désérialise depuis disque. Valide magic == kCollisionMagic, version
		/// == kCollisionVersion, contentHash xxhash64 du payload post-header.
		/// Reset `vertices` et `indices` avant désérialisation.
		/// \return true si OK ; sinon `outError` renseigné.
		bool LoadFromFile(const std::filesystem::path& path, std::string& outError);

		/// Sérialise sur disque. Écrit `OutputVersionHeader` (24 bytes) puis
		/// le payload type-dependent. Calcule le `contentHash` xxhash64 du
		/// payload avant écriture du header.
		/// \return true si OK ; sinon `outError` renseigné.
		bool SaveToFile(const std::filesystem::path& path, std::string& outError) const;
	};
}
