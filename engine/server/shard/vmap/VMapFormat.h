#pragma once
// CMANGOS.05 (Phase 2.05b) — VMapFormat : sérialisation binaire d'un
// "tile vmap" (mesh triangulaire + bbox), produit par l'outil offline
// `vmap_extractor` et consommé par le shard au runtime.
//
// **Principes** :
//   - Magic number + version dans l'en-tête → le shard refuse les
//     vieux fichiers avec un log explicite (cf. audit §8 "Format .vmap
//     versioning").
//   - Pas de dépendance lib externe (pas de protobuf, etc.).
//   - Tout passe par `std::byte` / `std::span` ou `std::vector<uint8_t>`
//     pour éviter `iostream` lourds — on accepte un blob mémoire ou
//     un chemin ; la lecture disque est laissée au caller (la couche
//     `engine::platform::FileSystem` est l'API de lecture canonique).
//   - Coordonnées **locales au tile** (cf. audit §8 "Précision float").
//
// **Layout binaire** (little-endian, sans padding) :
//
//   header :
//     uint32_t   magic            // 'L', 'V', 'M', '1' = 0x314D564C
//     uint32_t   version          // bumper à chaque changement
//     float      tileMinX, MinY, MinZ
//     float      tileMaxX, MaxY, MaxZ
//     uint32_t   vertexCount
//     uint32_t   triangleCount
//   data :
//     vertexCount × { float x, y, z }
//     triangleCount × { uint32_t a, b, c }   // indices vers vertices
//
// Toutes les coordonnées sont **relatives** au tile (origine du tile
// = (0,0,0) local, mais la bbox `tileMin/Max` est en world). Les
// opérations LOS/raycast côté shard travaillent en local au tile et
// transforment au moment du `IsInLineOfSight(world_p1, world_p2)`.

#include "engine/server/shard/vmap/AABB.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace engine::server::shard::vmap
{
	/// Magic number ASCII "LVM1" en little-endian.
	inline constexpr uint32_t kVMapMagic = 0x314D564Cu;

	/// Version actuelle du format. À bumper à chaque évolution
	/// breaking. Le loader rejette les fichiers de version != celle-ci
	/// avec un log explicite.
	inline constexpr uint32_t kVMapVersion = 1u;

	/// Triangle indexé sur 3 vertex. Indices 32-bit pour supporter les
	/// gros tiles (heightmap 65×65 = 8192 tris OK ; mesh statique
	/// importé peut être plus gros).
	struct VMapTri
	{
		uint32_t a = 0, b = 0, c = 0;
	};

	/// Tile vmap décodé en mémoire. Pas thread-safe (lecture seule
	/// après chargement → caller peut partager via shared_ptr).
	struct VMapTile
	{
		AABB                  bbox;       // world coords
		std::vector<engine::math::Vec3> vertices;   // local coords (tile origin)
		std::vector<VMapTri>  triangles;
	};

	enum class VMapDecodeResult : uint8_t
	{
		OK = 0,
		BufferTooSmall  = 1,
		BadMagic        = 2,
		WrongVersion    = 3,
		IndexOutOfRange = 4,
	};

	/// Sérialise un VMapTile vers un buffer binaire (little-endian).
	/// Retourne le buffer (jamais d'erreur en encodage : la structure
	/// est forcément valide à la sortie de l'extracteur). Caller écrit
	/// ensuite sur disque.
	std::vector<uint8_t> EncodeVMapTile(const VMapTile& tile);

	/// Décode un buffer en VMapTile. \p out est rempli en cas de succès.
	/// Si \p outErrVersion != nullptr, reçoit la version trouvée même
	/// en cas de WrongVersion (utile log).
	VMapDecodeResult DecodeVMapTile(std::span<const uint8_t> in, VMapTile& out,
		uint32_t* outErrVersion = nullptr);
}
