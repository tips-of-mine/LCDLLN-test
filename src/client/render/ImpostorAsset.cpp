// src/client/render/ImpostorAsset.cpp (M45.5)
//
// Implémentation du loader runtime du format `.mipo` v2 (M45.4b). Voir
// ImpostorAsset.h pour l'API. La lecture du header + métadonnées + blocs
// albedo/normal/orm est faite champ-par-champ en little-endian, RÉPLIQUANT la
// disposition documentée dans tools/impostor_builder/FORMAT.md, sans dépendre
// du code de l'outil offline.

#include "src/client/render/ImpostorAsset.h"

#include "src/shared/core/Log.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace engine::render
{
	namespace
	{
		/// Magic du format `.mipo` : ASCII 'M''I''P''O' en little-endian
		/// (0x4F50494D), IDENTIQUE à `tools::impostor_builder::kImpostorMagic`.
		constexpr uint32_t kImpostorMagic   = 0x4F50494Du;
		/// Version du format disque supportée (v2 : 3 atlas albedo/normal/orm).
		constexpr uint32_t kImpostorVersion = 2u;

		/// Lit un uint32 little-endian depuis `data` à `offset`, avance `offset`.
		/// \return false si dépassement de buffer.
		bool ReadU32LE(const std::vector<uint8_t>& data, size_t& offset, uint32_t& out)
		{
			if (offset + 4u > data.size()) return false;
			out = static_cast<uint32_t>(data[offset + 0]) |
			      (static_cast<uint32_t>(data[offset + 1]) << 8) |
			      (static_cast<uint32_t>(data[offset + 2]) << 16) |
			      (static_cast<uint32_t>(data[offset + 3]) << 24);
			offset += 4u;
			return true;
		}

		/// Lit un uint64 little-endian depuis `data` à `offset`, avance `offset`.
		/// \return false si dépassement de buffer.
		bool ReadU64LE(const std::vector<uint8_t>& data, size_t& offset, uint64_t& out)
		{
			if (offset + 8u > data.size()) return false;
			out = 0u;
			for (int i = 0; i < 8; ++i)
				out |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
			offset += 8u;
			return true;
		}

		/// Lit un float32 little-endian (bit-cast depuis un uint32), avance `offset`.
		/// \return false si dépassement de buffer.
		bool ReadF32LE(const std::vector<uint8_t>& data, size_t& offset, float& out)
		{
			uint32_t bits = 0u;
			if (!ReadU32LE(data, offset, bits)) return false;
			std::memcpy(&out, &bits, sizeof(out)); // bit-cast (little-endian sur nos plateformes cibles)
			return true;
		}

		/// Lit un bloc `[u64 size][size octets]` depuis `data` à `offset`. Valide
		/// que `size == expectedBytes` et que les octets tiennent dans le buffer,
		/// renvoie via `outPtr` un pointeur DANS `data` (non-owning), avance `offset`.
		/// \param label    Nom du bloc, injecté dans le message d'erreur.
		/// \param expectedBytes Taille attendue ((views*tile)^2 * 4).
		/// \return false (avec `err` renseigné) si tronqué ou taille incohérente.
		bool ReadBlock(const std::vector<uint8_t>& data, size_t& offset,
			uint64_t expectedBytes, const char* label,
			const uint8_t*& outPtr, std::string& err)
		{
			uint64_t blockSize = 0;
			if (!ReadU64LE(data, offset, blockSize))
			{ err = std::string("ImpostorAsset: taille bloc ") + label + " tronquée"; return false; }
			if (blockSize != expectedBytes || offset + blockSize > data.size())
			{
				err = std::string("ImpostorAsset: bloc ") + label + " invalide (taille="
					+ std::to_string(blockSize) + ", attendu=" + std::to_string(expectedBytes) + ")";
				return false;
			}
			outPtr = data.data() + offset;
			offset += static_cast<size_t>(blockSize);
			return true;
		}
	} // namespace

	bool ImpostorAsset::IsValid() const
	{
		return m_valid && m_albedo.IsValid() && m_normal.IsValid() && m_orm.IsValid();
	}

	bool ImpostorAsset::LoadFromFile(const std::string& absOrContentPath, AssetRegistry& reg, std::string& err)
	{
		m_valid = false;

		// 1) Lecture brute du fichier.
		std::ifstream f(absOrContentPath, std::ios::binary | std::ios::ate);
		if (!f.is_open())
		{
			err = "ImpostorAsset: fichier introuvable: '" + absOrContentPath + "'";
			return false;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0)
		{
			err = "ImpostorAsset: fichier vide: '" + absOrContentPath + "'";
			return false;
		}
		std::vector<uint8_t> data(static_cast<size_t>(size));
		f.seekg(0, std::ios::beg);
		f.read(reinterpret_cast<char*>(data.data()), size);
		if (!f)
		{
			err = "ImpostorAsset: lecture incomplète: '" + absOrContentPath + "'";
			return false;
		}

		// 2) Header 24 octets : magic, formatVersion, builderVersion, engineVersion (4×u32),
		//    contentHash (u64). Cf. FORMAT.md offsets 0..23.
		size_t off = 0;
		uint32_t magic = 0, formatVersion = 0, builderVersion = 0, engineVersion = 0;
		uint64_t contentHash = 0;
		if (!ReadU32LE(data, off, magic) || !ReadU32LE(data, off, formatVersion)
			|| !ReadU32LE(data, off, builderVersion) || !ReadU32LE(data, off, engineVersion)
			|| !ReadU64LE(data, off, contentHash))
		{
			err = "ImpostorAsset: header tronqué (<24 octets)";
			return false;
		}
		(void)builderVersion;
		(void)engineVersion;
		if (magic != kImpostorMagic)
		{
			err = "ImpostorAsset: magic invalide (attendu MIPO 0x4F50494D)";
			return false;
		}
		if (formatVersion != kImpostorVersion)
		{
			// Rejet propre (pas de crash) : l'appelant retombe sur le rendu mesh.
			err = "ImpostorAsset: version de format non supportée: " + std::to_string(formatVersion)
				+ " (attendu v" + std::to_string(kImpostorVersion) + ")";
			return false;
		}
		// contentHash stocké pour diagnostic (non vérifié en v2).
		m_contentHash = contentHash;

		// 3) ImpostorAtlasInfo : viewsPerAxis, tileSize, channels (3×u32),
		//    boundsMin[3], boundsMax[3] (6×f32). Cf. FORMAT.md offsets 24..59.
		uint32_t viewsPerAxis = 0, tileSize = 0, channels = 0;
		if (!ReadU32LE(data, off, viewsPerAxis) || !ReadU32LE(data, off, tileSize)
			|| !ReadU32LE(data, off, channels))
		{
			err = "ImpostorAsset: ImpostorAtlasInfo tronqué (grille)";
			return false;
		}
		float boundsMin[3]{}, boundsMax[3]{};
		for (int i = 0; i < 3; ++i)
			if (!ReadF32LE(data, off, boundsMin[i]))
			{ err = "ImpostorAsset: boundsMin tronqué"; return false; }
		for (int i = 0; i < 3; ++i)
			if (!ReadF32LE(data, off, boundsMax[i]))
			{ err = "ImpostorAsset: boundsMax tronqué"; return false; }

		if (viewsPerAxis == 0u || tileSize == 0u || channels != 4u)
		{
			err = "ImpostorAsset: métadonnées invalides (viewsPerAxis/tileSize/channels)";
			return false;
		}

		const uint32_t atlasSidePx = viewsPerAxis * tileSize;
		const uint64_t expectedBytes = static_cast<uint64_t>(atlasSidePx) * atlasSidePx * 4ull;

		// 4) Trois blocs `[u64 size][size octets RGBA8]` dans l'ordre VERROUILLÉ
		//    albedo (rgb=albedo a=couverture), normal (rgb=normale encodée a=profondeur
		//    relative), orm (r=AO g=roughness b=metallic a=255). Chacun de
		//    expectedBytes octets.
		const uint8_t* albedoPtr = nullptr;
		const uint8_t* normalPtr = nullptr;
		const uint8_t* ormPtr    = nullptr;
		if (!ReadBlock(data, off, expectedBytes, "albedo", albedoPtr, err)) return false;
		if (!ReadBlock(data, off, expectedBytes, "normal", normalPtr, err)) return false;
		if (!ReadBlock(data, off, expectedBytes, "orm",    ormPtr,    err)) return false;

		// 5) Upload des trois atlas en VkImage échantillonnable.
		//    Albedo en sRGB (couleur, linéarisée au sample) ; normal en UNORM
		//    (valeurs encodées n*0.5+0.5 + profondeur en alpha, lues telles quelles) ;
		//    ORM en UNORM (canaux PBR linéaires, lus tels quels).
		m_albedo = reg.CreateTextureFromMemory(albedoPtr, atlasSidePx, atlasSidePx, /*useSrgb=*/true);
		m_normal = reg.CreateTextureFromMemory(normalPtr, atlasSidePx, atlasSidePx, /*useSrgb=*/false);
		m_orm    = reg.CreateTextureFromMemory(ormPtr,    atlasSidePx, atlasSidePx, /*useSrgb=*/false);
		if (!m_albedo.IsValid() || !m_normal.IsValid() || !m_orm.IsValid())
		{
			err = "ImpostorAsset: échec upload GPU des atlas";
			return false;
		}

		m_info.viewsPerAxis = viewsPerAxis;
		m_info.tileSize     = tileSize;
		for (int i = 0; i < 3; ++i) { m_info.boundsMin[i] = boundsMin[i]; m_info.boundsMax[i] = boundsMax[i]; }
		m_valid = true;

		LOG_INFO(Render, "[ImpostorAsset] chargé '{}' (N={} tile={} atlas={}px)",
			absOrContentPath, viewsPerAxis, tileSize, atlasSidePx);
		return true;
	}
}
