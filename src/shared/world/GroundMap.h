#pragma once
// src/shared/world/GroundMap.h — SND.2b : carte des sols d'une cellule (sons de pas).
//
// Le splat (terrain_splat.slap) ne connaît que 4 couches de RENDU (herbe, terre,
// roche, neige) : une route, une rue, une plage, un marais et le marbre de l'Oracle y
// sont « terre » ou « neige ». Pour le son des pas, tools/world_gen écrit en plus,
// par cellule, la NATURE du sol de chaque texel du splat (même grille, même
// emprise) : zones/<c>/terrain_ground.gndm, référencé par zone.json
// `render.terrain.ground_map`. Le client la lit à la place de la couche dominante
// du splat ; sans ce fichier (monde généré avant SND.2b, zone de l'éditeur), il
// revient au splat. Ni le serveur ni l'éditeur ne la lisent.
//
// Format binaire (petit-boutiste), versionné, n'évolue que par ajout :
//   u32 magic 'GNDM' (0x4D444E47), u32 version (1), u32 size (texels par côté),
//   f64 originX, f64 originZ (coin min GLOBAL de la grille, m), f64 texelM (côté d'un texel, m),
//   size² × u8 GroundClass (ligne par ligne, z croissant).
// Une valeur inconnue d'un lecteur ancien (classe ajoutée plus tard) est lue comme
// Unknown : le client joue alors le son de pas par défaut (terre).
// En-tête seul (sans dépendance) : utilisable par le client et le générateur.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::world
{
	/// Nature du sol pour les sons de pas. Valeurs FIGÉES (écrites dans les fichiers) :
	/// on n'ajoute qu'à la fin, avant kGroundClassCount.
	enum class GroundClass : uint8_t
	{
		Unknown = 0,      ///< pas d'information : le client utilise la couche du splat
		Grass = 1,        ///< herbe, prairie, sous-bois
		Dirt = 2,         ///< terre meuble
		PackedEarth = 3,  ///< terre battue : routes, rues, places
		Gravel = 4,       ///< gravier, éboulis, terres arides
		Rock = 5,         ///< pierre, roche nue (et croûte de lave refroidie aux Enfers)
		Snow = 6,         ///< neige
		Sand = 7,         ///< sable (plages, déserts)
		Mud = 8,          ///< boue (marais, berges)
		Marble = 9,       ///< marbre blanc (Camp de l'Oracle)
	};

	/// Nombre de classes connues de ce lecteur (valeurs 0..kGroundClassCount-1).
	inline constexpr uint8_t kGroundClassCount = 10u;

	/// Carte des sols d'une cellule (voir l'en-tête du fichier).
	struct GroundMap
	{
		static constexpr uint32_t kMagic = 0x4D444E47u; ///< 'GNDM'
		static constexpr uint32_t kVersion = 1u;
		uint32_t size = 0;            ///< texels par côté
		double originX = 0.0;         ///< coin min global X (m)
		double originZ = 0.0;         ///< coin min global Z (m)
		double texelM = 1.0;          ///< côté d'un texel (m)
		std::vector<uint8_t> classes; ///< size², GroundClass

		/// Vrai si la carte est vide (aucune donnée chargée).
		bool Empty() const { return size == 0 || classes.size() != static_cast<size_t>(size) * size; }

		/// Classe du sol au point global (x, z), texel le plus proche.
		/// \return Unknown hors de la grille, carte vide ou valeur inconnue de ce lecteur.
		GroundClass At(double x, double z) const
		{
			if (Empty() || texelM <= 0.0) return GroundClass::Unknown;
			const double fi = (x - originX) / texelM, fj = (z - originZ) / texelM;
			if (!(fi >= 0.0 && fj >= 0.0)) return GroundClass::Unknown;
			const uint32_t i = static_cast<uint32_t>(fi), j = static_cast<uint32_t>(fj);
			if (i >= size || j >= size) return GroundClass::Unknown;
			const uint8_t v = classes[static_cast<size_t>(j) * size + i];
			return v < kGroundClassCount ? static_cast<GroundClass>(v) : GroundClass::Unknown;
		}
	};

	namespace ground_detail
	{
		template <typename T> void Put(std::vector<uint8_t>& out, T v)
		{
			const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
			out.insert(out.end(), b, b + sizeof(T));
		}
		template <typename T> bool Get(std::span<const uint8_t> in, size_t& at, T& v)
		{
			if (at + sizeof(T) > in.size()) return false;
			std::memcpy(&v, in.data() + at, sizeof(T));
			at += sizeof(T);
			return true;
		}
	}

	/// Sérialise \p m au format `terrain_ground.gndm`. \return octets du fichier.
	inline std::vector<uint8_t> SaveGroundMap(const GroundMap& m)
	{
		using namespace ground_detail;
		std::vector<uint8_t> out;
		out.reserve(36 + m.classes.size());
		Put(out, GroundMap::kMagic); Put(out, GroundMap::kVersion); Put(out, m.size);
		Put(out, m.originX); Put(out, m.originZ); Put(out, m.texelM);
		out.insert(out.end(), m.classes.begin(), m.classes.end());
		return out;
	}

	/// Lit un fichier `terrain_ground.gndm`. \p out est vidé en cas d'échec.
	/// \return false (et \p err renseigné) si le fichier est tronqué, d'une autre
	/// nature, d'une version plus récente ou de taille incohérente.
	inline bool LoadGroundMap(std::span<const uint8_t> in, GroundMap& out, std::string& err)
	{
		using namespace ground_detail;
		out = GroundMap{};
		size_t at = 0;
		uint32_t magic = 0, version = 0, size = 0;
		GroundMap m;
		if (!Get(in, at, magic) || !Get(in, at, version) || !Get(in, at, size)
			|| !Get(in, at, m.originX) || !Get(in, at, m.originZ) || !Get(in, at, m.texelM))
		{ err = "en-tete tronque"; return false; }
		if (magic != GroundMap::kMagic) { err = "magic invalide"; return false; }
		if (version == 0 || version > GroundMap::kVersion) { err = "version non geree : " + std::to_string(version); return false; }
		if (size == 0 || size > 16384u || !(m.texelM > 0.0)) { err = "dimensions invalides"; return false; }
		const size_t n = static_cast<size_t>(size) * size;
		if (in.size() - at != n) { err = "taille de donnees incoherente"; return false; }
		m.size = size;
		m.classes.assign(in.begin() + static_cast<std::ptrdiff_t>(at), in.end());
		out = std::move(m);
		return true;
	}
}
