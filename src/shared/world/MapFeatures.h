#pragma once

// FIX.12 — Routes et bâtiments de la carte du monde (fichier `world/<monde>/map_features.bin`).
//
// La carte du monde (touche M) affiche une image du continent (world_map.png,
// ≈ 215 m par pixel) : à 2 km ou 250 m visibles, routes et bâtiments n'y sont
// pas lisibles. tools/world_gen exporte donc leur tracé VECTORIEL, que le client
// dessine par-dessus l'image (sous le voile des zones non découvertes).
//
// Format binaire (petit-boutiste, coordonnées GLOBALES en m, X = est, Z = sud) :
//   u32 magic "LMFT" | u32 version (1)
//   u32 roadCount, puis par route : u8 kind | f32 widthM | u32 pointCount | pointCount × (f32 x, f32 z)
//   u32 buildingCount, puis par bâtiment : f32 x | f32 z | f32 yawRad | f32 halfW | f32 halfD
//   (v2, FIX.15) u32 waterCount, puis par plan d'eau : u8 kind (0 rivière, 1 lac)
//                | f32 widthM | u32 pointCount | pointCount × (f32 x, f32 z)
//   Rivière = polyligne de largeur widthM ; lac = contour fermé (polygone étoilé
//   autour de son barycentre, dessiné en éventail). Un fichier v1 se lit encore
//   (aucun plan d'eau).
//
// Header-only, sans dépendance moteur : partagé par tools/world_gen (écriture),
// le client (lecture) et les tests (map_features_tests).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::world::mapfeatures
{
	/// Nature d'un tracé (épaisseur et couleur à l'affichage).
	enum class MapRoadKind : uint8_t
	{
		Major = 0,  ///< grande route entre villes importantes
		Road = 1,   ///< route inter-villes
		Path = 2,   ///< chemin local (lieu procédural → réseau)
		Street = 3, ///< rue d'agglomération
	};

	/// Un tracé (polyligne) + sa boîte englobante (calculée au chargement).
	struct MapRoad
	{
		MapRoadKind kind = MapRoadKind::Road;
		float widthM = 5.0f;          ///< largeur de chaussée (m)
		std::vector<float> xz;        ///< points (x0, z0, x1, z1, …) en m
		float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;

		size_t PointCount() const { return xz.size() / 2u; }
	};

	/// Emprise au sol d'un bâtiment : rectangle orienté.
	struct MapBuilding
	{
		float x = 0.0f, z = 0.0f;     ///< centre (m)
		float yawRad = 0.0f;          ///< orientation (même convention que tools/world_gen ForRect)
		float halfW = 3.0f;           ///< demi-largeur locale X (m)
		float halfD = 3.0f;           ///< demi-profondeur locale Z (m)
	};

	/// FIX.15 — Nature d'un plan d'eau.
	enum class MapWaterKind : uint8_t
	{
		River = 0, ///< polyligne de largeur widthM
		Lake = 1,  ///< contour fermé
	};

	/// FIX.15 — Rivière ou lac (coordonnées GLOBALES) + boîte englobante.
	struct MapWater
	{
		MapWaterKind kind = MapWaterKind::River;
		float widthM = 6.0f;          ///< largeur d'une rivière (m) ; ignorée pour un lac
		std::vector<float> xz;        ///< points (x0, z0, x1, z1, …) en m
		float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;

		size_t PointCount() const { return xz.size() / 2u; }
	};

	struct MapFeatures
	{
		std::vector<MapRoad> roads;
		std::vector<MapBuilding> buildings;
		std::vector<MapWater> waters;  ///< FIX.15 — rivières et lacs (v2)

		bool Empty() const { return roads.empty() && buildings.empty() && waters.empty(); }
	};

	inline constexpr uint32_t kMagic = 0x54464D4Cu; // "LMFT"
	/// Version écrite. v1 (FIX.12) : routes + bâtiments ; v2 (FIX.15) : + plans d'eau.
	inline constexpr uint32_t kVersion = 2u;
	inline constexpr uint32_t kMaxWaters = 1u << 20;
	/// Garde-fous de lecture (fichier corrompu → refus plutôt qu'allocation géante).
	inline constexpr uint32_t kMaxRoads = 1u << 20;
	inline constexpr uint32_t kMaxPointsPerRoad = 1u << 22;
	inline constexpr uint32_t kMaxBuildings = 1u << 22;

	/// Recalcule la boîte englobante de \p road (sert à ignorer les tracés hors vue).
	inline void ComputeBounds(MapRoad& road)
	{
		if (road.xz.size() < 2u)
		{
			road.minX = road.minZ = road.maxX = road.maxZ = 0.0f;
			return;
		}
		road.minX = road.maxX = road.xz[0];
		road.minZ = road.maxZ = road.xz[1];
		for (size_t i = 0; i + 1 < road.xz.size(); i += 2)
		{
			road.minX = std::min(road.minX, road.xz[i]);
			road.maxX = std::max(road.maxX, road.xz[i]);
			road.minZ = std::min(road.minZ, road.xz[i + 1]);
			road.maxZ = std::max(road.maxZ, road.xz[i + 1]);
		}
	}

	/// FIX.15 — Boîte englobante d'un plan d'eau (élargie de la demi-largeur
	/// d'une rivière, pour ne pas la couper au bord de la vue).
	inline void ComputeBounds(MapWater& w)
	{
		if (w.xz.size() < 2u)
		{
			w.minX = w.minZ = w.maxX = w.maxZ = 0.0f;
			return;
		}
		w.minX = w.maxX = w.xz[0];
		w.minZ = w.maxZ = w.xz[1];
		for (size_t i = 0; i + 1 < w.xz.size(); i += 2)
		{
			w.minX = std::min(w.minX, w.xz[i]);
			w.maxX = std::max(w.maxX, w.xz[i]);
			w.minZ = std::min(w.minZ, w.xz[i + 1]);
			w.maxZ = std::max(w.maxZ, w.xz[i + 1]);
		}
		const float pad = w.kind == MapWaterKind::River ? w.widthM * 0.5f : 0.0f;
		w.minX -= pad; w.minZ -= pad; w.maxX += pad; w.maxZ += pad;
	}

	/// FIX.15 — Barycentre (moyenne des sommets) d'un contour de lac.
	/// \param cx,cz reçoivent le centre ; (0, 0) si aucun point.
	inline void LakeCenter(const MapWater& w, float& cx, float& cz)
	{
		cx = cz = 0.0f;
		const size_t n = w.PointCount();
		if (n == 0u)
			return;
		double sx = 0.0, sz = 0.0;
		for (size_t i = 0; i < n; ++i)
		{
			sx += w.xz[2 * i];
			sz += w.xz[2 * i + 1];
		}
		cx = static_cast<float>(sx / static_cast<double>(n));
		cz = static_cast<float>(sz / static_cast<double>(n));
	}

	/// Allège une polyligne : garde le premier point, puis chaque point distant
	/// d'au moins \p minStepM (m) du dernier gardé, et toujours le dernier point.
	/// \param xz points (x0, z0, x1, z1, …). \return la polyligne allégée.
	inline std::vector<float> DecimatePolyline(const std::vector<float>& xz, float minStepM)
	{
		std::vector<float> out;
		const size_t n = xz.size() / 2u;
		if (n == 0u)
			return out;
		out.push_back(xz[0]);
		out.push_back(xz[1]);
		const float step2 = minStepM * minStepM;
		for (size_t i = 1; i < n; ++i)
		{
			const float x = xz[2 * i], z = xz[2 * i + 1];
			const bool last = i + 1 == n;
			const float dx = x - out[out.size() - 2], dz = z - out[out.size() - 1];
			if (last || dx * dx + dz * dz >= step2)
			{
				// Dernier point trop proche du précédent gardé : on remplace ce
				// dernier (sauf s'il s'agit du premier) pour ne pas créer de micro-segment.
				if (last && dx * dx + dz * dz < step2 && out.size() > 2u)
				{
					out[out.size() - 2] = x;
					out[out.size() - 1] = z;
				}
				else
				{
					out.push_back(x);
					out.push_back(z);
				}
			}
		}
		return out;
	}

	/// Coins monde (x, z) du rectangle d'un bâtiment, dans l'ordre
	/// (-w,-d), (+w,-d), (+w,+d), (-w,+d) locaux. Local → monde : inverse de la
	/// rotation de tools/world_gen ForRect : dx = c·lx + s·lz ; dz = -s·lx + c·lz.
	/// \param out 8 flottants (x0, z0, …, x3, z3).
	inline void BuildingCorners(const MapBuilding& b, float out[8])
	{
		const float c = std::cos(b.yawRad), s = std::sin(b.yawRad);
		const float lx[4] = { -b.halfW, b.halfW, b.halfW, -b.halfW };
		const float lz[4] = { -b.halfD, -b.halfD, b.halfD, b.halfD };
		for (int k = 0; k < 4; ++k)
		{
			out[2 * k] = b.x + c * lx[k] + s * lz[k];
			out[2 * k + 1] = b.z - s * lx[k] + c * lz[k];
		}
	}

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
		}
		inline void PutF32(std::vector<uint8_t>& b, float f)
		{
			uint32_t u = 0;
			std::memcpy(&u, &f, 4);
			PutU32(b, u);
		}

		/// Lecteur borné : toute lecture hors du tampon met `ok` à false.
		struct Reader
		{
			std::span<const uint8_t> data;
			size_t pos = 0;
			bool ok = true;

			bool Has(size_t n) const { return ok && data.size() - pos >= n && pos <= data.size(); }
			uint8_t U8()
			{
				if (!Has(1)) { ok = false; return 0; }
				return data[pos++];
			}
			uint32_t U32()
			{
				if (!Has(4)) { ok = false; return 0; }
				uint32_t v = 0;
				for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[pos + static_cast<size_t>(i)]) << (8 * i);
				pos += 4;
				return v;
			}
			float F32()
			{
				const uint32_t u = U32();
				float f = 0.0f;
				std::memcpy(&f, &u, 4);
				return f;
			}
		};
	}

	/// Sérialise \p f au format courant (kVersion, voir en-tête du fichier).
	inline std::vector<uint8_t> Serialize(const MapFeatures& f)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kMagic);
		detail::PutU32(b, kVersion);
		detail::PutU32(b, static_cast<uint32_t>(f.roads.size()));
		for (const MapRoad& r : f.roads)
		{
			b.push_back(static_cast<uint8_t>(r.kind));
			detail::PutF32(b, r.widthM);
			detail::PutU32(b, static_cast<uint32_t>(r.PointCount()));
			for (size_t i = 0; i < r.PointCount() * 2u; ++i)
				detail::PutF32(b, r.xz[i]);
		}
		detail::PutU32(b, static_cast<uint32_t>(f.buildings.size()));
		for (const MapBuilding& m : f.buildings)
		{
			detail::PutF32(b, m.x);
			detail::PutF32(b, m.z);
			detail::PutF32(b, m.yawRad);
			detail::PutF32(b, m.halfW);
			detail::PutF32(b, m.halfD);
		}
		detail::PutU32(b, static_cast<uint32_t>(f.waters.size()));
		for (const MapWater& w : f.waters)
		{
			b.push_back(static_cast<uint8_t>(w.kind));
			detail::PutF32(b, w.widthM);
			detail::PutU32(b, static_cast<uint32_t>(w.PointCount()));
			for (size_t i = 0; i < w.PointCount() * 2u; ++i)
				detail::PutF32(b, w.xz[i]);
		}
		return b;
	}

	/// Lit un fichier v1 ou v2. Les boîtes englobantes sont recalculées.
	/// Les points non finis sont rejetés (fichier refusé).
	/// \return false (et \p out vidé, \p err renseigné) si magic/version/tailles invalides.
	inline bool Deserialize(std::span<const uint8_t> bytes, MapFeatures& out, std::string& err)
	{
		out = MapFeatures{};
		detail::Reader r{ bytes };
		if (r.U32() != kMagic) { err = "map_features: magic invalide"; return false; }
		const uint32_t version = r.U32();
		if (!r.ok || version < 1u || version > kVersion) { err = "map_features: version " + std::to_string(version) + " non supportee"; return false; }
		const uint32_t roadCount = r.U32();
		if (!r.ok || roadCount > kMaxRoads) { err = "map_features: nombre de routes invalide"; out = {}; return false; }
		out.roads.reserve(roadCount);
		for (uint32_t i = 0; i < roadCount; ++i)
		{
			MapRoad road;
			road.kind = static_cast<MapRoadKind>(std::min<uint8_t>(r.U8(), static_cast<uint8_t>(MapRoadKind::Street)));
			road.widthM = r.F32();
			const uint32_t n = r.U32();
			if (!r.ok || n > kMaxPointsPerRoad || !r.Has(static_cast<size_t>(n) * 8u))
			{
				err = "map_features: route " + std::to_string(i) + " tronquee";
				out = {};
				return false;
			}
			road.xz.resize(static_cast<size_t>(n) * 2u);
			for (float& v : road.xz)
			{
				v = r.F32();
				if (!std::isfinite(v)) { err = "map_features: coordonnee non finie"; out = {}; return false; }
			}
			if (!std::isfinite(road.widthM) || road.widthM <= 0.0f)
				road.widthM = 4.0f;
			ComputeBounds(road);
			out.roads.push_back(std::move(road));
		}
		const uint32_t buildingCount = r.U32();
		if (!r.ok || buildingCount > kMaxBuildings || !r.Has(static_cast<size_t>(buildingCount) * 20u))
		{
			err = "map_features: batiments tronques";
			out = {};
			return false;
		}
		out.buildings.resize(buildingCount);
		for (MapBuilding& m : out.buildings)
		{
			m.x = r.F32();
			m.z = r.F32();
			m.yawRad = r.F32();
			m.halfW = r.F32();
			m.halfD = r.F32();
			if (!std::isfinite(m.x) || !std::isfinite(m.z) || !std::isfinite(m.yawRad) || !std::isfinite(m.halfW) || !std::isfinite(m.halfD))
			{
				err = "map_features: batiment non fini";
				out = {};
				return false;
			}
		}
		if (version < 2u)
			return true;
		const uint32_t waterCount = r.U32();
		if (!r.ok || waterCount > kMaxWaters) { err = "map_features: nombre de plans d'eau invalide"; out = {}; return false; }
		out.waters.reserve(waterCount);
		for (uint32_t i = 0; i < waterCount; ++i)
		{
			MapWater w;
			w.kind = r.U8() == 1u ? MapWaterKind::Lake : MapWaterKind::River;
			w.widthM = r.F32();
			const uint32_t n = r.U32();
			if (!r.ok || n > kMaxPointsPerRoad || !r.Has(static_cast<size_t>(n) * 8u))
			{
				err = "map_features: plan d'eau " + std::to_string(i) + " tronque";
				out = {};
				return false;
			}
			w.xz.resize(static_cast<size_t>(n) * 2u);
			for (float& v : w.xz)
			{
				v = r.F32();
				if (!std::isfinite(v)) { err = "map_features: coordonnee non finie"; out = {}; return false; }
			}
			if (!std::isfinite(w.widthM) || w.widthM <= 0.0f)
				w.widthM = 6.0f;
			ComputeBounds(w);
			out.waters.push_back(std::move(w));
		}
		return true;
	}
}
