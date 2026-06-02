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

	namespace
	{
		/// Lit un bloc [u64 size][size octets] et valide size == expected.
		/// \param label Nom de l'atlas pour les messages d'erreur ("albedo"…).
		/// \return true si la taille et les données ont été lues et validées.
		bool ReadAtlasBlock(std::istream& is, uint64_t expected,
		                    std::vector<uint8_t>& out, const char* label,
		                    std::string& err)
		{
			bool ok = true;
			const uint64_t size = ReadU64(is, ok);
			if (!ok)
			{
				err = std::string("ReadImpostorFile: fichier tronqué (taille ") + label + ")";
				return false;
			}
			if (size != expected)
			{
				err = std::string("ReadImpostorFile: taille ") + label +
				      " incohérente avec les métadonnées";
				return false;
			}
			out.resize(static_cast<size_t>(size));
			if (size > 0)
			{
				is.read(reinterpret_cast<char*>(out.data()),
				        static_cast<std::streamsize>(size));
				if (!is)
				{
					err = std::string("ReadImpostorFile: fichier tronqué (données ") + label + ")";
					return false;
				}
			}
			return true;
		}

		/// Écrit un bloc [u64 size][size octets] dans le flux.
		void WriteAtlasBlock(std::ostream& os, const std::vector<uint8_t>& data)
		{
			WriteU64(os, static_cast<uint64_t>(data.size()));
			if (!data.empty())
				os.write(reinterpret_cast<const char*>(data.data()),
				         static_cast<std::streamsize>(data.size()));
		}
	}

	bool WriteImpostorFile(const std::string& path,
	                       const ImpostorAtlasInfo& info,
	                       uint64_t contentHash,
	                       const std::vector<uint8_t>& albedo,
	                       const std::vector<uint8_t>& normal,
	                       const std::vector<uint8_t>& orm,
	                       std::string& err)
	{
		// Vérification de cohérence des tailles avant écriture.
		const uint64_t atlasDim = static_cast<uint64_t>(info.viewsPerAxis) * info.tileSize;
		const uint64_t expected = atlasDim * atlasDim * info.channels;
		if (albedo.size() != expected || normal.size() != expected || orm.size() != expected)
		{
			err = "WriteImpostorFile: taille d'atlas incohérente avec viewsPerAxis/tileSize/channels";
			return false;
		}

		std::ofstream os(path, std::ios::binary | std::ios::trunc);
		if (!os)
		{
			err = "WriteImpostorFile: impossible d'ouvrir en écriture: " + path;
			return false;
		}

		// --- Header (24 octets) -----------------------------------------
		ImpostorHeader header; // magic/version/builderVersion = défauts courants (v2)
		WriteU32(os, header.magic);
		WriteU32(os, header.formatVersion);
		WriteU32(os, header.builderVersion);
		WriteU32(os, header.engineVersion);
		WriteU64(os, contentHash); // hash FNV-1a 64 fourni par l'appelant

		// --- ImpostorAtlasInfo ------------------------------------------
		WriteU32(os, info.viewsPerAxis);
		WriteU32(os, info.tileSize);
		WriteU32(os, info.channels);
		for (int i = 0; i < 3; ++i) WriteF32(os, info.boundsMin[i]);
		for (int i = 0; i < 3; ++i) WriteF32(os, info.boundsMax[i]);

		// --- Trois atlas dans l'ordre disque : albedo, normal, orm ------
		WriteAtlasBlock(os, albedo);
		WriteAtlasBlock(os, normal);
		WriteAtlasBlock(os, orm);

		if (!os)
		{
			err = "WriteImpostorFile: erreur d'écriture sur " + path;
			return false;
		}
		return true;
	}

	bool ReadImpostorFile(const std::string& path,
	                      ImpostorAtlasInfo& outInfo,
	                      uint64_t& outContentHash,
	                      std::vector<uint8_t>& outAlbedo,
	                      std::vector<uint8_t>& outNormal,
	                      std::vector<uint8_t>& outOrm,
	                      std::string& err)
	{
		std::ifstream is(path, std::ios::binary);
		if (!is)
		{
			err = "ReadImpostorFile: impossible d'ouvrir en lecture: " + path;
			return false;
		}

		bool ok = true;

		// --- Header -----------------------------------------------------
		const uint32_t magic   = ReadU32(is, ok);
		const uint32_t fmtVer  = ReadU32(is, ok);
		(void)ReadU32(is, ok); // builderVersion
		(void)ReadU32(is, ok); // engineVersion
		outContentHash = ReadU64(is, ok);
		if (!ok)
		{
			err = "ReadImpostorFile: fichier tronqué (header)";
			return false;
		}
		if (magic != kImpostorMagic)
		{
			err = "ReadImpostorFile: magic invalide";
			return false;
		}
		if (fmtVer != kImpostorVersion)
		{
			err = "ReadImpostorFile: version de format non supportée (attendu 2)";
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
			err = "ReadImpostorFile: fichier tronqué (atlas info)";
			return false;
		}

		const uint64_t atlasDim = static_cast<uint64_t>(outInfo.viewsPerAxis) * outInfo.tileSize;
		const uint64_t expected = atlasDim * atlasDim * outInfo.channels;

		// --- Trois atlas dans l'ordre disque : albedo, normal, orm ------
		if (!ReadAtlasBlock(is, expected, outAlbedo, "albedo", err)) return false;
		if (!ReadAtlasBlock(is, expected, outNormal, "normal", err)) return false;
		if (!ReadAtlasBlock(is, expected, outOrm, "orm", err)) return false;

		return true;
	}
}
