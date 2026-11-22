#pragma once

// Monde jouable (chantier « Royaumes ») — forêt dense générée côté client (FORET.1).
//
// Problème : une vraie forêt (un arbre tous les 4 à 5 m) représente ~4 millions
// d'arbres par cellule de 10 km ; stockés dans scatter.bin, cela ferait ~120 Mo
// par cellule de forêt et autant de mémoire au client.
//
// Solution : le générateur (tools/world_gen) n'écrit plus les arbres des forêts
// et des lisières dans scatter.bin, mais une CARTE DE DENSITÉ forestière par
// cellule (`zones/<c>/terrain_forest.frms`, 1024² octets ≈ 1 Mo), calculée après
// toutes les exclusions (routes, eau, bâti, champs, pentes, neige, clairières).
// Le client pose lui-même, autour du joueur seulement, les arbres et le sous-bois
// de remplissage (GenerateForestFiller) : positions DÉTERMINISTES (treillis
// global hashé), donc identiques à chaque passage, sans stockage. Le serveur ne
// connaît pas le décor : rien ne change côté serveur.
//
// Header-only (inline), sur le patron de ScatterInstances.h : partagé par le
// client (lecture, génération) et par tools/world_gen (écriture), sans Vulkan.
//
// Disposition binaire (little-endian) :
//   [magic u32 'FRMS'][version u32][width u32][height u32]
//   [originX f64][originZ f64][sizeM f64]       coin min GLOBAL (m) et côté couvert (m)
//   width × height × [density u8]               ligne par ligne (Z puis X), 0 = rien, 255 = pleine forêt
// Le texel (i, j) couvre [originX + i·sizeM/width, +sizeM/width[ (centre au milieu).

#include "src/shared/world/instances/ScatterInstances.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace engine::world::instances
{
	inline constexpr uint32_t kForestMaskMagic = 0x534D5246u; // 'F','R','M','S' en octets little-endian
	inline constexpr uint32_t kForestMaskVersion = 1u;
	/// Résolution écrite par tools/world_gen (≈ 9,8 m par texel pour 10 km).
	inline constexpr uint32_t kForestMaskResolution = 1024u;
	/// v1.0.133 — PRAIRIE : valeur écrite par tools/world_gen sur les terres ouvertes
	/// (plaines, collines, landes) hors forêt. Le client n'y pose AUCUN arbre, seulement
	/// le couvre-sol (herbes hautes, fleurs, trèfle, cailloux) et de rares buissons.
	/// Les valeurs forestières commencent au-dessus de kForestMaskMeadowCeil (25/255).
	inline constexpr uint8_t kForestMaskMeadowValue = 10u;
	/// Densité (0..1) en dessous de laquelle un texel est une prairie (pas d'arbre).
	inline constexpr float kForestMaskMeadowCeil = 14.0f / 255.0f;
	/// Plus petite valeur forestière écrite par le générateur (au-dessus de la prairie).
	inline constexpr uint8_t kForestMaskMinForestValue = 25u;

	/// Carte de densité forestière d'une cellule (coordonnées GLOBALES).
	struct ForestMask
	{
		uint32_t width = 0;
		uint32_t height = 0;
		double originX = 0.0;   ///< coin min X couvert (m, global)
		double originZ = 0.0;   ///< coin min Z couvert (m, global)
		double sizeM = 0.0;     ///< côté couvert (m)
		std::vector<uint8_t> density; ///< width × height, 0..255

		bool Empty() const { return width == 0 || height == 0 || density.empty(); }

		/// Vrai si le point global (x, z) est dans l'emprise (bord min inclus, max
		/// exclu) : un point n'appartient qu'à une seule carte (pas de doublon entre
		/// cellules voisines).
		bool Contains(double x, double z) const
		{
			return !Empty() && x >= originX && z >= originZ && x < originX + sizeM && z < originZ + sizeM;
		}

		/// Densité bilinéaire (0..1) au point global (x, z) ; 0 hors emprise.
		float DensityAt(double x, double z) const
		{
			if (!Contains(x, z)) return 0.0f;
			const double fx = (x - originX) / sizeM * width - 0.5;
			const double fz = (z - originZ) / sizeM * height - 0.5;
			const int i0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(width) - 1);
			const int j0 = std::clamp(static_cast<int>(std::floor(fz)), 0, static_cast<int>(height) - 1);
			const int i1 = std::min(i0 + 1, static_cast<int>(width) - 1);
			const int j1 = std::min(j0 + 1, static_cast<int>(height) - 1);
			const double tx = std::clamp(fx - i0, 0.0, 1.0), tz = std::clamp(fz - j0, 0.0, 1.0);
			const auto at = [this](int i, int j) {
				return static_cast<double>(density[static_cast<size_t>(j) * width + static_cast<size_t>(i)]);
			};
			const double v = (at(i0, j0) * (1 - tx) + at(i1, j0) * tx) * (1 - tz)
				+ (at(i0, j1) * (1 - tx) + at(i1, j1) * tx) * tz;
			return static_cast<float>(v / 255.0);
		}
	};

	namespace forest_detail
	{
		inline void PutF64(std::vector<uint8_t>& b, double d)
		{
			uint64_t u = 0; std::memcpy(&u, &d, 8);
			for (int k = 0; k < 8; ++k) b.push_back(uint8_t(u >> (8 * k)));
		}
		inline bool GetF64(std::span<const uint8_t> b, size_t& p, double& out)
		{
			if (p + 8 > b.size()) return false;
			uint64_t u = 0;
			for (int k = 0; k < 8; ++k) u |= uint64_t(b[p + k]) << (8 * k);
			std::memcpy(&out, &u, 8); p += 8; return true;
		}
		/// Hash 64 bits (splitmix64) : déterministe, identique sur toutes les plateformes.
		inline uint64_t Mix(uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}
		inline uint64_t Hash3(uint64_t salt, int64_t i, int64_t j)
		{
			return Mix(Mix(salt ^ Mix(static_cast<uint64_t>(i))) ^ static_cast<uint64_t>(j));
		}
		/// [0, 1[ à partir des 53 bits de poids fort.
		inline double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }
	}

	/// Sérialise au format terrain_forest.frms.
	inline std::vector<uint8_t> SaveForestMask(const ForestMask& m)
	{
		using namespace scatter_detail;
		std::vector<uint8_t> b;
		b.reserve(40 + m.density.size());
		PutU32(b, kForestMaskMagic);
		PutU32(b, kForestMaskVersion);
		PutU32(b, m.width);
		PutU32(b, m.height);
		forest_detail::PutF64(b, m.originX);
		forest_detail::PutF64(b, m.originZ);
		forest_detail::PutF64(b, m.sizeM);
		b.insert(b.end(), m.density.begin(), m.density.end());
		return b;
	}

	/// Lit un terrain_forest.frms. \return false (et \p err) si invalide.
	inline bool LoadForestMask(std::span<const uint8_t> b, ForestMask& out, std::string& err)
	{
		using namespace scatter_detail;
		out = ForestMask{};
		size_t p = 0;
		uint32_t magic = 0, version = 0, w = 0, h = 0;
		if (!GetU32(b, p, magic) || magic != kForestMaskMagic) { err = "forest mask : magic invalide"; return false; }
		if (!GetU32(b, p, version) || version != kForestMaskVersion) { err = "forest mask : version non geree"; return false; }
		if (!GetU32(b, p, w) || !GetU32(b, p, h) || w == 0 || h == 0 || w > 8192 || h > 8192)
		{ err = "forest mask : dimensions invalides"; return false; }
		double ox = 0, oz = 0, s = 0;
		if (!forest_detail::GetF64(b, p, ox) || !forest_detail::GetF64(b, p, oz) || !forest_detail::GetF64(b, p, s)
			|| !(s > 0.0) || !std::isfinite(ox) || !std::isfinite(oz))
		{ err = "forest mask : cadrage invalide"; return false; }
		const size_t n = static_cast<size_t>(w) * h;
		if (b.size() - p != n) { err = "forest mask : taille des donnees incoherente"; return false; }
		out.width = w; out.height = h; out.originX = ox; out.originZ = oz; out.sizeM = s;
		out.density.assign(b.begin() + static_cast<std::ptrdiff_t>(p), b.end());
		return true;
	}

	/// Réglages de la forêt de remplissage (valeurs par défaut = FORET.1).
	struct ForestFillerParams
	{
		double radiusM = 180.0;         ///< rayon autour du centre (m)
		/// Rayon (m) du sous-bois (≤ radiusM). Le sous-bois (classe moyenne) est coupé bien avant
		/// les arbres : inutile de le poser jusqu'à l'horizon des billboards.
		double underRadiusM = 1.0e9;
		double treeStepM = 4.2;         ///< pas du treillis des arbres : ~1 arbre tous les 4-5 m en pleine forêt
		double underStepM = 3.2;        ///< pas du treillis du sous-bois
		double treeAccept = 0.92;       ///< probabilité d'un arbre par case à densité 1
		double underAccept = 0.55;      ///< probabilité d'une plante de sous-bois par case à densité 1
		double coniferAltitudeM = 300.0;///< au-dessus : résineux (comme tools/world_gen)
		/// v1.0.133 — couvre-sol (herbes hautes, fleurs, trèfle, cailloux) partout où la
		/// carte est non nulle, prairies comprises, dans un rayon réduit autour du joueur.
		double groundStepM = 1.9;       ///< pas du treillis du couvre-sol
		double groundRadiusM = 38.0;    ///< rayon du couvre-sol (≤ radiusM ; au-delà la classe Small est coupée)
		double groundAccept = 0.62;     ///< probabilité d'une touffe par case en prairie
		double meadowUnderAccept = 0.07;///< buissons épars en prairie (par case du sous-bois)
	};

	/// Espèce de remplissage (chemin sous meshes/props/, poids, échelle, tronc).
	struct ForestFillerSpecies
	{
		const char* mesh;
		double weight;
		double scaleMin, scaleMax;
		double trunkRadius; ///< rayon de collision à l'échelle 1 (0 = traversable)
	};

	namespace forest_detail
	{
		inline const std::vector<ForestFillerSpecies>& Broadleaf()
		{
			static const std::vector<ForestFillerSpecies> v = {
				{ "CommonTree_1", 1, 0.85, 1.25, 0.55 }, { "CommonTree_2", 1, 0.85, 1.25, 0.55 },
				{ "CommonTree_3", 1, 0.85, 1.2, 0.55 }, { "CommonTree_4", 1, 0.85, 1.25, 0.55 },
				{ "CommonTree_5", 1, 0.85, 1.25, 0.55 }, { "Pine_1", 0.5, 0.85, 1.2, 0.45 },
				{ "Pine_3", 0.5, 0.85, 1.2, 0.45 }, { "TwistedTree_2", 0.12, 0.55, 0.75, 0.7 },
				{ "DeadTree_3", 0.08, 0.8, 1.1, 0.45 },
				// Atelier décors : chênes et hêtres de futaie (meshes à taille réelle).
				{ "Tree_Oak_1", 0.6, 0.9, 1.1, 0.7 }, { "Tree_Oak_2", 0.6, 0.9, 1.1, 0.7 },
				{ "Tree_Beech_1", 0.6, 0.9, 1.1, 0.5 }, { "Tree_Beech_2", 0.6, 0.9, 1.1, 0.5 },
				{ "Tree_Beech_3", 0.6, 0.9, 1.1, 0.5 },
				// Vieux chênes géants (~4 %), comme les chênes massifs de la BD-T3 pl.24.
				{ "Tree_Oak_3", 0.24, 1.15, 1.3, 0.9 } };
			return v;
		}
		inline const std::vector<ForestFillerSpecies>& Conifer()
		{
			static const std::vector<ForestFillerSpecies> v = {
				{ "Pine_1", 1, 0.85, 1.3, 0.45 }, { "Pine_2", 1, 0.85, 1.3, 0.45 }, { "Pine_3", 1, 0.85, 1.3, 0.45 },
				{ "Pine_4", 1, 0.8, 1.2, 0.45 }, { "Pine_5", 1, 0.85, 1.3, 0.45 }, { "DeadTree_1", 0.15, 0.8, 1.1, 0.45 } };
			return v;
		}
		/// v1.0.133 — couvre-sol des prairies : herbes hautes surtout, cailloux rares.
		inline const std::vector<ForestFillerSpecies>& GroundGrass()
		{
			static const std::vector<ForestFillerSpecies> v = {
				{ "Grass_Common_Tall", 3.0, 0.8, 1.3, 0 }, { "Grass_Common_Short", 2.5, 0.9, 1.4, 0 },
				{ "Grass_Wispy_Tall", 2.0, 0.8, 1.25, 0 }, { "Grass_Wispy_Short", 1.5, 0.9, 1.3, 0 },
				{ "Clover_1", 0.6, 0.8, 1.2, 0 }, { "Clover_2", 0.6, 0.8, 1.2, 0 },
				{ "Pebble_Round_2", 0.15, 0.7, 1.3, 0 }, { "Pebble_Square_3", 0.12, 0.7, 1.3, 0 } };
			return v;
		}
		/// v1.0.133 — taches de fleurs (bruit ≈ 30 m) dans les prairies.
		inline const std::vector<ForestFillerSpecies>& GroundFlowers()
		{
			static const std::vector<ForestFillerSpecies> v = {
				{ "Flower_3_Group", 1.0, 0.8, 1.2, 0 }, { "Flower_4_Group", 1.0, 0.8, 1.2, 0 },
				{ "Flower_3_Single", 0.6, 0.9, 1.3, 0 }, { "Flower_4_Single", 0.6, 0.9, 1.3, 0 },
				{ "Bush_Common_Flowers", 0.25, 0.6, 1.0, 0 } };
			return v;
		}
		/// Bruit de valeur lissé déterministe dans [0, 1] (taches de fleurs, ~\p cellM).
		inline double PatchNoise(double x, double z, double cellM, uint64_t salt)
		{
			const double fx = x / cellM, fz = z / cellM;
			const int64_t i = static_cast<int64_t>(std::floor(fx)), j = static_cast<int64_t>(std::floor(fz));
			const double tx = fx - static_cast<double>(i), tz = fz - static_cast<double>(j);
			const auto v = [salt](int64_t a, int64_t b) { return static_cast<double>(Mix(Hash3(salt, a, b)) >> 11) * (1.0 / 9007199254740992.0); };
			const double sx = tx * tx * (3.0 - 2.0 * tx), sz = tz * tz * (3.0 - 2.0 * tz);
			const double a = v(i, j) + (v(i + 1, j) - v(i, j)) * sx;
			const double b = v(i, j + 1) + (v(i + 1, j + 1) - v(i, j + 1)) * sx;
			return a + (b - a) * sz;
		}
		inline const std::vector<ForestFillerSpecies>& Undergrowth()
		{
			static const std::vector<ForestFillerSpecies> v = {
				{ "Fern_1", 3.0, 0.85, 1.2, 0 }, { "Bush_Common", 1.5, 0.8, 1.4, 0 },
				{ "Plant_1_Big", 0.6, 0.55, 0.85, 0 }, { "Plant_7_Big", 0.6, 0.7, 1.1, 0 } };
			return v;
		}
		inline const ForestFillerSpecies& Pick(const std::vector<ForestFillerSpecies>& v, double u)
		{
			double total = 0.0;
			for (const auto& s : v) total += s.weight;
			double acc = 0.0;
			for (const auto& s : v) { acc += s.weight / total; if (u <= acc) return s; }
			return v.back();
		}
		/// Index du mesh dans \p out (ajouté s'il est nouveau).
		inline uint16_t MeshIndex(ScatterFile& out, const char* mesh)
		{
			const std::string path = std::string("meshes/props/") + mesh + ".gltf";
			for (size_t k = 0; k < out.meshPaths.size(); ++k)
				if (out.meshPaths[k] == path) return static_cast<uint16_t>(k);
			out.meshPaths.push_back(path);
			return static_cast<uint16_t>(out.meshPaths.size() - 1);
		}
	}

	/// Génère dans \p out (remplacé) les arbres et le sous-bois de remplissage de la
	/// carte \p mask dans le disque de centre (\p cx, \p cz) (global, m) et de rayon
	/// params.radiusM (sous-bois : params.underRadiusM). Déterministe : une même case du treillis global donne
	/// toujours la même instance, quels que soient le centre et le rayon (pas de
	/// saut quand le joueur avance). Seuls les points DE la carte sont produits.
	/// \param groundAltitude altitude du sol au point global (choix feuillus /
	///        résineux) ; vide = feuillus partout.
	/// Effet de bord : aucun (fonction pure). Tout thread.
	inline void GenerateForestFiller(const ForestMask& mask, double cx, double cz, const ForestFillerParams& params,
		const std::function<double(double, double)>& groundAltitude, ScatterFile& out)
	{
		using namespace forest_detail;
		out = ScatterFile{};
		if (mask.Empty() || !(params.radiusM > 0.0)) return;
		const double r2 = params.radiusM * params.radiusM;
		// Couches : arbres, sous-bois, couvre-sol (v1.0.133).
		enum class Kind { Trees, Under, Ground };
		struct Layer { double step, accept, radius; uint64_t salt; Kind kind; };
		const Layer layers[] = { { params.treeStepM, params.treeAccept, params.radiusM, 0xF0E57A11ull, Kind::Trees },
			{ params.underStepM, params.underAccept, std::min(params.underRadiusM, params.radiusM), 0x5B5B0B05ull, Kind::Under },
			{ params.groundStepM, params.groundAccept, std::min(params.groundRadiusM, params.radiusM), 0x6A0D5C0Bull, Kind::Ground } };
		const float meadowCeil = kForestMaskMeadowCeil;
		for (const Layer& L : layers)
		{
			if (!(L.step > 0.5) || !(L.radius > 0.0)) continue;
			const double lr2 = std::min(r2, L.radius * L.radius);
			const int64_t i0 = static_cast<int64_t>(std::floor((cx - L.radius) / L.step));
			const int64_t i1 = static_cast<int64_t>(std::floor((cx + L.radius) / L.step));
			const int64_t j0 = static_cast<int64_t>(std::floor((cz - L.radius) / L.step));
			const int64_t j1 = static_cast<int64_t>(std::floor((cz + L.radius) / L.step));
			for (int64_t j = j0; j <= j1; ++j)
				for (int64_t i = i0; i <= i1; ++i)
				{
					const uint64_t h = Hash3(L.salt, i, j);
					// Jitter limité à 70 % de la case : deux voisins restent à ≥ 30 % du pas.
					const double x = (static_cast<double>(i) + 0.15 + 0.7 * Unit(h)) * L.step;
					const double z = (static_cast<double>(j) + 0.15 + 0.7 * Unit(Mix(h + 1))) * L.step;
					const double dx = x - cx, dz = z - cz;
					if (dx * dx + dz * dz > lr2 || !mask.Contains(x, z)) continue;
					const double d = mask.DensityAt(x, z);
					if (!(d > 0.0)) continue;
					// v1.0.133 — prairie (valeur basse) : aucun arbre, buissons épars, couvre-sol plein ;
					// en forêt : couvre-sol clairsemé sous les arbres.
					const bool meadow = d <= meadowCeil;
					double accept = 0.0;
					switch (L.kind)
					{
					case Kind::Trees: accept = meadow ? 0.0 : d * L.accept; break;
					case Kind::Under: accept = meadow ? params.meadowUnderAccept : d * L.accept; break;
					case Kind::Ground: accept = meadow ? L.accept : L.accept * 0.35; break;
					}
					if (Unit(Mix(h + 2)) >= accept) continue;
					const bool trees = L.kind == Kind::Trees;
					const bool conifer = trees && groundAltitude && groundAltitude(x, z) > params.coniferAltitudeM;
					const bool flowers = L.kind == Kind::Ground && meadow && PatchNoise(x, z, 28.0, 0xF10A3E5ull) > 0.62;
					const auto& pool = trees ? (conifer ? Conifer() : Broadleaf())
						: (L.kind == Kind::Under ? Undergrowth() : (flowers ? GroundFlowers() : GroundGrass()));
					const ForestFillerSpecies& sp = Pick(pool, Unit(Mix(h + 3)));
					ScatterInstance in;
					in.meshIndex = MeshIndex(out, sp.mesh);
					in.x = static_cast<float>(x);
					in.z = static_cast<float>(z);
					in.scale = static_cast<float>(sp.scaleMin + (sp.scaleMax - sp.scaleMin) * Unit(Mix(h + 4)));
					in.yawDeg = static_cast<float>(Unit(Mix(h + 5)) * 360.0);
					if (trees)
					{
						in.cullClass = static_cast<uint8_t>(ScatterCullClass::Large);
						in.tiltDeg = static_cast<float>(Unit(Mix(h + 6)) * 3.5);
						in.yOffset = -0.15f;
						in.flags = static_cast<uint8_t>(kScatterFlagSolid | kScatterFlagOccluder);
						in.collisionRadius = static_cast<float>(sp.trunkRadius * std::sqrt(static_cast<double>(in.scale)));
					}
					else
					{
						in.cullClass = static_cast<uint8_t>(L.kind == Kind::Ground ? ScatterCullClass::Small : ScatterCullClass::Medium);
					}
					out.instances.push_back(in);
				}
		}
	}
}
