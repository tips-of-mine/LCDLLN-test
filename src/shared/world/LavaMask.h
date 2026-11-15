#pragma once
// src/shared/world/LavaMask.h — v1.0.123 : masque de lave d'une cellule des Enfers.
//
// Écrit par tools/world_gen (zones/enfers_*/lava_mask.lvmk, cellules qui contiennent de
// la lave), lu par le serveur (mort en 3 s dans la lave, décision du 15/11/2026, LORE_BD
// §16.49). Même grille et même valeur que le canal A du splat de la cellule (rendu
// lumineux côté client) : ce que le joueur voit brûler est ce qui le tue.
//
// Format binaire (petit-boutiste) :
//   u32 magic 'LVMK' (0x4B4D564C), u32 version (1), u32 size (texels par côté),
//   f64 originX, f64 originZ (coin min GLOBAL de la grille, m), f64 texelM (côté d'un texel, m),
//   size² × u8 lava (0..255, ligne par ligne, z croissant), size² × f32 groundY (altitude du
//   sol du texel, m ; le générateur y met la plus haute du bloc).
// En-tête seul (sans dépendance) : utilisable par le client, le serveur et le générateur.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::world
{
	/// Masque de lave d'une cellule (voir l'en-tête du fichier).
	struct LavaMask
	{
		static constexpr uint32_t kMagic = 0x4B4D564Cu; ///< 'LVMK'
		static constexpr uint32_t kVersion = 1u;
		uint32_t size = 0;           ///< texels par côté
		double originX = 0.0;        ///< coin min global X (m)
		double originZ = 0.0;        ///< coin min global Z (m)
		double texelM = 1.0;         ///< côté d'un texel (m)
		std::vector<uint8_t> lava;   ///< size², 0 = pas de lave, 255 = lave pure
		std::vector<float> groundY;  ///< size², altitude du sol (m)

		/// Vrai si le masque est vide (aucune donnée chargée).
		bool Empty() const { return size == 0 || lava.size() != static_cast<size_t>(size) * size; }

		/// Lave (0..1) et altitude du sol au point global (x, z), texel le plus proche.
		/// \return false si le point est hors de la grille (\p outLava et \p outGroundY inchangés).
		bool Sample(double x, double z, float& outLava, float& outGroundY) const
		{
			if (Empty()) return false;
			const double fi = (x - originX) / texelM, fj = (z - originZ) / texelM;
			if (!(fi >= 0.0 && fj >= 0.0)) return false;
			const uint32_t i = static_cast<uint32_t>(fi), j = static_cast<uint32_t>(fj);
			if (i >= size || j >= size) return false;
			const size_t q = static_cast<size_t>(j) * size + i;
			outLava = static_cast<float>(lava[q]) / 255.0f;
			outGroundY = q < groundY.size() ? groundY[q] : 0.0f;
			return true;
		}
	};

	namespace lava_detail
	{
		inline void PutRaw(std::vector<uint8_t>& out, const void* p, size_t n)
		{
			const uint8_t* b = static_cast<const uint8_t*>(p);
			out.insert(out.end(), b, b + n);
		}
		template <typename T> void Put(std::vector<uint8_t>& out, T v) { PutRaw(out, &v, sizeof(T)); }
		template <typename T> bool Get(std::span<const uint8_t> in, size_t& at, T& v)
		{
			if (at + sizeof(T) > in.size()) return false;
			std::memcpy(&v, in.data() + at, sizeof(T));
			at += sizeof(T);
			return true;
		}
	}

	/// Sérialise \p m au format `lava_mask.lvmk`. \return octets du fichier.
	inline std::vector<uint8_t> SaveLavaMask(const LavaMask& m)
	{
		using namespace lava_detail;
		std::vector<uint8_t> out;
		const size_t n = static_cast<size_t>(m.size) * m.size;
		out.reserve(36 + n * 5);
		Put(out, LavaMask::kMagic); Put(out, LavaMask::kVersion); Put(out, m.size);
		Put(out, m.originX); Put(out, m.originZ); Put(out, m.texelM);
		PutRaw(out, m.lava.data(), n);
		PutRaw(out, m.groundY.data(), n * sizeof(float));
		return out;
	}

	/// Lit un fichier `lava_mask.lvmk`. \return false (et \p err) si invalide ; \p out vidé.
	inline bool LoadLavaMask(std::span<const uint8_t> bytes, LavaMask& out, std::string& err)
	{
		using namespace lava_detail;
		out = LavaMask{};
		size_t at = 0;
		uint32_t magic = 0, version = 0, size = 0;
		if (!Get(bytes, at, magic) || magic != LavaMask::kMagic) { err = "lava_mask : magic invalide"; return false; }
		if (!Get(bytes, at, version) || version != LavaMask::kVersion) { err = "lava_mask : version non geree"; return false; }
		LavaMask m;
		if (!Get(bytes, at, size) || size == 0 || size > 8192) { err = "lava_mask : taille invalide"; return false; }
		if (!Get(bytes, at, m.originX) || !Get(bytes, at, m.originZ) || !Get(bytes, at, m.texelM)
			|| !std::isfinite(m.originX) || !std::isfinite(m.originZ) || !(m.texelM > 0.0))
		{
			err = "lava_mask : cadrage invalide"; return false;
		}
		const size_t n = static_cast<size_t>(size) * size;
		if (bytes.size() != at + n + n * sizeof(float)) { err = "lava_mask : taille du fichier incoherente"; return false; }
		m.size = size;
		m.lava.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + n));
		m.groundY.resize(n);
		std::memcpy(m.groundY.data(), bytes.data() + at + n, n * sizeof(float));
		out = std::move(m);
		return true;
	}
}
