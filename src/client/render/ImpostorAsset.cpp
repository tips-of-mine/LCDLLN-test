// src/client/render/ImpostorAsset.cpp (M45.5)
//
// Implémentation du loader runtime du format `.mipo` (M45.4). Voir
// ImpostorAsset.h pour l'API. La lecture du header + métadonnées + blocs
// albedo/normal est faite champ-par-champ en little-endian, RÉPLIQUANT la
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
		/// Version du format disque supportée (cf. kImpostorVersion outil).
		constexpr uint32_t kImpostorVersion = 1u;

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
	} // namespace

	bool ImpostorAsset::IsValid() const
	{
		return m_valid && m_albedo.IsValid() && m_normal.IsValid();
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
		(void)contentHash;
		if (magic != kImpostorMagic)
		{
			err = "ImpostorAsset: magic invalide (attendu MIPO 0x4F50494D)";
			return false;
		}
		if (formatVersion != kImpostorVersion)
		{
			err = "ImpostorAsset: version de format non supportée: " + std::to_string(formatVersion);
			return false;
		}

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

		// 4) Bloc albedo : u64 albedoSize + albedoSize octets RGBA8.
		uint64_t albedoSize = 0;
		if (!ReadU64LE(data, off, albedoSize))
		{ err = "ImpostorAsset: albedoSize tronqué"; return false; }
		if (albedoSize != expectedBytes || off + albedoSize > data.size())
		{
			err = "ImpostorAsset: bloc albedo invalide (taille=" + std::to_string(albedoSize)
				+ ", attendu=" + std::to_string(expectedBytes) + ")";
			return false;
		}
		const uint8_t* albedoPtr = data.data() + off;
		off += static_cast<size_t>(albedoSize);

		// 5) Bloc normal : u64 normalSize + normalSize octets RGBA8.
		uint64_t normalSize = 0;
		if (!ReadU64LE(data, off, normalSize))
		{ err = "ImpostorAsset: normalSize tronqué"; return false; }
		if (normalSize != expectedBytes || off + normalSize > data.size())
		{
			err = "ImpostorAsset: bloc normal invalide (taille=" + std::to_string(normalSize)
				+ ", attendu=" + std::to_string(expectedBytes) + ")";
			return false;
		}
		const uint8_t* normalPtr = data.data() + off;

		// 6) Upload des deux atlas en VkImage échantillonnable.
		//    Albedo en sRGB (couleur, linéarisée au sample), normal en UNORM
		//    (valeurs encodées n*0.5+0.5 lues telles quelles, + masque alpha).
		m_albedo = reg.CreateTextureFromMemory(albedoPtr, atlasSidePx, atlasSidePx, /*useSrgb=*/true);
		m_normal = reg.CreateTextureFromMemory(normalPtr, atlasSidePx, atlasSidePx, /*useSrgb=*/false);
		if (!m_albedo.IsValid() || !m_normal.IsValid())
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
