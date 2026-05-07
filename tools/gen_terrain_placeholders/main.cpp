// Génère 8 PNG 4x4 unicolore dans `assets/terrain/placeholders/` (chemin
// passé en argv[1], défaut `./assets/terrain/placeholders/`). Utilisé par la
// custom command CMake POST_BUILD pour fournir des placeholders au shader
// 8-layer terrain (M100.9). Remplaçables transparents quand les vrais PBR
// arriveront.
//
// Format : PNG canonique color_type=2 (RGB), bit_depth=8, taille 4x4.
// IDAT compressé en deflate "stored block" (mode 0, pas de compression),
// pour ne dépendre d'aucune bibliothèque zlib externe.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	/// Couleurs RGB 8-bit par layer, alignées sur `assets/terrain/layer_palette.json`.
	/// Choisies pour être visuellement distinctes au blend dans le shader.
	constexpr std::array<std::array<uint8_t, 3>, 8> kLayerColors {{
		{{139,  90,  43}}, // dirt        — brun chocolat
		{{100, 145,  60}}, // grass_dry   — vert moyen
		{{ 60, 130,  60}}, // grass_wet   — vert foncé
		{{ 78,  50,  30}}, // mud         — brun très foncé
		{{220, 200, 130}}, // sand        — beige sable
		{{120, 120, 120}}, // rock        — gris pierre
		{{240, 240, 250}}, // snow        — blanc bleuté
		{{ 30,  20,  20}}, // lava_cooled — noir grenat
	}};

	/// Doit matcher l'ordre + les noms du `layer_palette.json` (M100.9).
	constexpr std::array<const char*, 8> kLayerNames {
		"dirt", "grass_dry", "grass_wet", "mud", "sand", "rock", "snow", "lava_cooled"
	};

	/// Big-endian uint32 pour les chunk lengths PNG.
	uint32_t SwapBE32(uint32_t v)
	{
		return ((v & 0xFFu) << 24)
		     | ((v & 0xFF00u) << 8)
		     | ((v & 0xFF0000u) >> 8)
		     | ((v >> 24) & 0xFFu);
	}

	/// CRC32 standard PNG (polynôme 0xEDB88320). Table calculée à la première
	/// invocation, statique thread-safe en C++ moderne.
	uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu)
	{
		static uint32_t table[256] = {0};
		static bool init = false;
		if (!init)
		{
			for (uint32_t i = 0; i < 256u; ++i)
			{
				uint32_t c = i;
				for (int k = 0; k < 8; ++k)
					c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				table[i] = c;
			}
			init = true;
		}
		for (size_t i = 0; i < len; ++i)
			crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
		return crc ^ 0xFFFFFFFFu;
	}

	/// Append un chunk PNG (length, type 4 octets, data, CRC32) au buffer.
	void WriteChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
	{
		const uint32_t lenBE = SwapBE32(static_cast<uint32_t>(data.size()));
		const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&lenBE);
		out.insert(out.end(), lenBytes, lenBytes + 4);
		out.insert(out.end(), reinterpret_cast<const uint8_t*>(type),
			reinterpret_cast<const uint8_t*>(type) + 4);
		out.insert(out.end(), data.begin(), data.end());

		// CRC sur type + data.
		std::vector<uint8_t> crcBuf;
		crcBuf.insert(crcBuf.end(), reinterpret_cast<const uint8_t*>(type),
			reinterpret_cast<const uint8_t*>(type) + 4);
		crcBuf.insert(crcBuf.end(), data.begin(), data.end());
		const uint32_t crc = Crc32(crcBuf.data(), crcBuf.size());
		const uint32_t crcBE = SwapBE32(crc);
		const uint8_t* crcBytes = reinterpret_cast<const uint8_t*>(&crcBE);
		out.insert(out.end(), crcBytes, crcBytes + 4);
	}

	/// Adler32 (zlib trailing checksum).
	uint32_t Adler32(const uint8_t* data, size_t len)
	{
		uint32_t a = 1u, b = 0u;
		for (size_t i = 0; i < len; ++i)
		{
			a = (a + data[i]) % 65521u;
			b = (b + a) % 65521u;
		}
		return (b << 16) | a;
	}

	/// Encode `raw` en stream zlib avec un seul stored block non compressé.
	/// Header zlib (CMF=0x78, FLG=0x01), bloc stored (BFINAL=1, BTYPE=00),
	/// LEN/NLEN little-endian, raw bytes, Adler32 trailing big-endian.
	/// Pas de dépendance zlib — pure RFC1950/RFC1951.
	std::vector<uint8_t> ZlibStored(const std::vector<uint8_t>& raw)
	{
		std::vector<uint8_t> out;
		out.push_back(0x78); // CMF : deflate, 32K window
		out.push_back(0x01); // FLG : no preset dict, fastest level

		// Stored block : BFINAL=1, BTYPE=00 → 0x01.
		out.push_back(0x01);
		const uint16_t len  = static_cast<uint16_t>(raw.size());
		const uint16_t nlen = static_cast<uint16_t>(~len);
		out.push_back(static_cast<uint8_t>(len  & 0xFFu));
		out.push_back(static_cast<uint8_t>((len  >> 8) & 0xFFu));
		out.push_back(static_cast<uint8_t>(nlen & 0xFFu));
		out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
		out.insert(out.end(), raw.begin(), raw.end());

		const uint32_t adler = Adler32(raw.data(), raw.size());
		const uint32_t adlerBE = SwapBE32(adler);
		const uint8_t* adlerBytes = reinterpret_cast<const uint8_t*>(&adlerBE);
		out.insert(out.end(), adlerBytes, adlerBytes + 4);
		return out;
	}

	/// Écrit un PNG `size × size` unicolore RGB (`r`, `g`, `b`) à `path`.
	void WritePng(const std::filesystem::path& path,
		uint8_t r, uint8_t g, uint8_t b, uint32_t size = 4u)
	{
		std::vector<uint8_t> png;
		// Signature PNG (\x89 P N G \r \n \x1a \n).
		const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		png.insert(png.end(), sig, sig + 8);

		// IHDR : width(4) + height(4) + bitdepth(1) + colortype(1) +
		// compression(1) + filter(1) + interlace(1) = 13 octets.
		std::vector<uint8_t> ihdr;
		const uint32_t wBE = SwapBE32(size), hBE = SwapBE32(size);
		const uint8_t* wBytes = reinterpret_cast<const uint8_t*>(&wBE);
		const uint8_t* hBytes = reinterpret_cast<const uint8_t*>(&hBE);
		ihdr.insert(ihdr.end(), wBytes, wBytes + 4);
		ihdr.insert(ihdr.end(), hBytes, hBytes + 4);
		ihdr.push_back(8); // bit depth
		ihdr.push_back(2); // color type RGB (truecolor)
		ihdr.push_back(0); // compression method (deflate)
		ihdr.push_back(0); // filter method (adaptive None)
		ihdr.push_back(0); // interlace method (none)
		WriteChunk(png, "IHDR", ihdr);

		// IDAT : pour chaque scanline, byte de filtre 0 (None) puis pixels RGB.
		std::vector<uint8_t> raw;
		for (uint32_t y = 0; y < size; ++y)
		{
			raw.push_back(0); // filter type None
			for (uint32_t x = 0; x < size; ++x)
			{
				raw.push_back(r);
				raw.push_back(g);
				raw.push_back(b);
			}
		}
		const std::vector<uint8_t> idat = ZlibStored(raw);
		WriteChunk(png, "IDAT", idat);

		// IEND (no data).
		WriteChunk(png, "IEND", std::vector<uint8_t>{});

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		f.write(reinterpret_cast<const char*>(png.data()),
			static_cast<std::streamsize>(png.size()));
	}
}

int main(int argc, char* argv[])
{
	std::filesystem::path outDir = (argc > 1) ? argv[1] : "assets/terrain/placeholders";
	std::error_code ec;
	std::filesystem::create_directories(outDir, ec);
	if (ec)
	{
		std::fprintf(stderr, "[gen_terrain_placeholders] mkdir failed: %s\n",
			ec.message().c_str());
		return 1;
	}

	for (size_t i = 0; i < kLayerNames.size(); ++i)
	{
		const std::filesystem::path file = outDir / (std::string(kLayerNames[i]) + ".png");
		WritePng(file, kLayerColors[i][0], kLayerColors[i][1], kLayerColors[i][2]);
	}
	std::printf("[gen_terrain_placeholders] wrote %zu PNG to %s\n",
		kLayerNames.size(), outDir.string().c_str());
	return 0;
}
