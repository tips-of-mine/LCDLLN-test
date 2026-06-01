/// M45.4 — Implémentation lecture/écriture du format d'atlas d'impostors.
/// Sérialisation explicite champ par champ en little-endian (portable).

#include "ImpostorFormat.h"

#include <cstring>
#include <fstream>

namespace tools::impostor_builder
{
	namespace
	{
		/// Écrit un uint32 en little-endian dans le flux.
		void WriteU32(std::ostream& os, uint32_t v)
		{
			uint8_t b[4] = {
				static_cast<uint8_t>(v & 0xFFu),
				static_cast<uint8_t>((v >> 8) & 0xFFu),
				static_cast<uint8_t>((v >> 16) & 0xFFu),
				static_cast<uint8_t>((v >> 24) & 0xFFu)};
			os.write(reinterpret_cast<const char*>(b), 4);
		}

		/// Écrit un uint64 en little-endian dans le flux.
		void WriteU64(std::ostream& os, uint64_t v)
		{
			uint8_t b[8];
			for (int i = 0; i < 8; ++i)
				b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
			os.write(reinterpret_cast<const char*>(b), 8);
		}

		/// Écrit un float (bit-pattern IEEE-754) en little-endian.
		void WriteF32(std::ostream& os, float f)
		{
			uint32_t bits;
			std::memcpy(&bits, &f, sizeof(bits));
			WriteU32(os, bits);
		}

		/// Lit un uint32 little-endian. Met outOk à false en cas d'échec flux.
		uint32_t ReadU32(std::istream& is, bool& outOk)
		{
			uint8_t b[4] = {0, 0, 0, 0};
			is.read(reinterpret_cast<char*>(b), 4);
			if (!is) { outOk = false; return 0; }
			return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
			       (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
		}

		/// Lit un uint64 little-endian. Met outOk à false en cas d'échec flux.
		uint64_t ReadU64(std::istream& is, bool& outOk)
		{
			uint8_t b[8] = {0};
			is.read(reinterpret_cast<char*>(b), 8);
			if (!is) { outOk = false; return 0; }
			uint64_t v = 0;
			for (int i = 0; i < 8; ++i)
				v |= static_cast<uint64_t>(b[i]) << (8 * i);
			return v;
		}

		/// Lit un float little-endian. Met outOk à false en cas d'échec flux.
		float ReadF32(std::istream& is, bool& outOk)
		{
			uint32_t bits = ReadU32(is, outOk);
			float f;
			std::memcpy(&f, &bits, sizeof(f));
			return f;
		}
	}

	bool WriteImpostorFile(const std::string& path,
	                       const ImpostorAtlasInfo& info,
	                       const std::vector<uint8_t>& albedoAtlas,
	                       const std::vector<uint8_t>& normalAtlas,
	                       std::string& outError)
	{
		// Vérification de cohérence des tailles avant écriture.
		const uint64_t atlasDim = static_cast<uint64_t>(info.viewsPerAxis) * info.tileSize;
		const uint64_t expected = atlasDim * atlasDim * info.channels;
		if (albedoAtlas.size() != expected || normalAtlas.size() != expected)
		{
			outError = "WriteImpostorFile: taille d'atlas incohérente avec viewsPerAxis/tileSize/channels";
			return false;
		}

		std::ofstream os(path, std::ios::binary | std::ios::trunc);
		if (!os)
		{
			outError = "WriteImpostorFile: impossible d'ouvrir en écriture: " + path;
			return false;
		}

		// --- Header (24 octets) -----------------------------------------
		ImpostorHeader header; // valeurs par défaut = magic/version courants
		WriteU32(os, header.magic);
		WriteU32(os, header.formatVersion);
		WriteU32(os, header.builderVersion);
		WriteU32(os, header.engineVersion);
		WriteU64(os, header.contentHash);

		// --- ImpostorAtlasInfo ------------------------------------------
		WriteU32(os, info.viewsPerAxis);
		WriteU32(os, info.tileSize);
		WriteU32(os, info.channels);
		for (int i = 0; i < 3; ++i) WriteF32(os, info.boundsMin[i]);
		for (int i = 0; i < 3; ++i) WriteF32(os, info.boundsMax[i]);

		// --- Atlas albedo -----------------------------------------------
		WriteU64(os, static_cast<uint64_t>(albedoAtlas.size()));
		if (!albedoAtlas.empty())
			os.write(reinterpret_cast<const char*>(albedoAtlas.data()),
			         static_cast<std::streamsize>(albedoAtlas.size()));

		// --- Atlas normal -----------------------------------------------
		WriteU64(os, static_cast<uint64_t>(normalAtlas.size()));
		if (!normalAtlas.empty())
			os.write(reinterpret_cast<const char*>(normalAtlas.data()),
			         static_cast<std::streamsize>(normalAtlas.size()));

		if (!os)
		{
			outError = "WriteImpostorFile: erreur d'écriture sur " + path;
			return false;
		}
		return true;
	}

	bool ReadImpostorFile(const std::string& path,
	                      ImpostorAtlasInfo& outInfo,
	                      std::vector<uint8_t>& outAlbedo,
	                      std::vector<uint8_t>& outNormal,
	                      std::string& outError)
	{
		std::ifstream is(path, std::ios::binary);
		if (!is)
		{
			outError = "ReadImpostorFile: impossible d'ouvrir en lecture: " + path;
			return false;
		}

		bool ok = true;

		// --- Header -----------------------------------------------------
		const uint32_t magic   = ReadU32(is, ok);
		const uint32_t fmtVer  = ReadU32(is, ok);
		(void)ReadU32(is, ok); // builderVersion
		(void)ReadU32(is, ok); // engineVersion
		(void)ReadU64(is, ok); // contentHash
		if (!ok)
		{
			outError = "ReadImpostorFile: fichier tronqué (header)";
			return false;
		}
		if (magic != kImpostorMagic)
		{
			outError = "ReadImpostorFile: magic invalide";
			return false;
		}
		if (fmtVer != kImpostorVersion)
		{
			outError = "ReadImpostorFile: version de format non supportée";
			return false;
		}

		// --- ImpostorAtlasInfo ------------------------------------------
		outInfo.viewsPerAxis = ReadU32(is, ok);
		outInfo.tileSize     = ReadU32(is, ok);
		outInfo.channels     = ReadU32(is, ok);
		for (int i = 0; i < 3; ++i) outInfo.boundsMin[i] = ReadF32(is, ok);
		for (int i = 0; i < 3; ++i) outInfo.boundsMax[i] = ReadF32(is, ok);
		if (!ok)
		{
			outError = "ReadImpostorFile: fichier tronqué (atlas info)";
			return false;
		}

		// --- Atlas albedo -----------------------------------------------
		const uint64_t albedoSize = ReadU64(is, ok);
		if (!ok)
		{
			outError = "ReadImpostorFile: fichier tronqué (taille albedo)";
			return false;
		}
		const uint64_t atlasDim = static_cast<uint64_t>(outInfo.viewsPerAxis) * outInfo.tileSize;
		const uint64_t expected = atlasDim * atlasDim * outInfo.channels;
		if (albedoSize != expected)
		{
			outError = "ReadImpostorFile: taille albedo incohérente avec les métadonnées";
			return false;
		}
		outAlbedo.resize(static_cast<size_t>(albedoSize));
		if (albedoSize > 0)
		{
			is.read(reinterpret_cast<char*>(outAlbedo.data()),
			        static_cast<std::streamsize>(albedoSize));
			if (!is)
			{
				outError = "ReadImpostorFile: fichier tronqué (données albedo)";
				return false;
			}
		}

		// --- Atlas normal -----------------------------------------------
		const uint64_t normalSize = ReadU64(is, ok);
		if (!ok)
		{
			outError = "ReadImpostorFile: fichier tronqué (taille normal)";
			return false;
		}
		if (normalSize != expected)
		{
			outError = "ReadImpostorFile: taille normal incohérente avec les métadonnées";
			return false;
		}
		outNormal.resize(static_cast<size_t>(normalSize));
		if (normalSize > 0)
		{
			is.read(reinterpret_cast<char*>(outNormal.data()),
			        static_cast<std::streamsize>(normalSize));
			if (!is)
			{
				outError = "ReadImpostorFile: fichier tronqué (données normal)";
				return false;
			}
		}

		return true;
	}
}
