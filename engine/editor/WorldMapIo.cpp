#include "engine/editor/WorldMapIo.h"

#include "engine/editor/WorldMapEditDocument.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"
#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/world/OutputVersion.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
// Implémentation STB déjà dans AssetRegistry.cpp — pas de second STB_IMAGE_IMPLEMENTATION.
#include "stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace engine::editor
{
	namespace
	{
		void SkipWs(std::string_view s, size_t& i)
		{
			while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0)
			{
				++i;
			}
		}

		std::string UnescapeJsonString(std::string_view inner)
		{
			std::string o;
			o.reserve(inner.size());
			for (size_t i = 0; i < inner.size(); ++i)
			{
				if (inner[i] != '\\' || i + 1 >= inner.size())
				{
					o.push_back(inner[i]);
					continue;
				}
				switch (inner[++i])
				{
				case '"': o.push_back('"'); break;
				case '\\': o.push_back('\\'); break;
				case '/': o.push_back('/'); break;
				case 'b': o.push_back('\b'); break;
				case 'f': o.push_back('\f'); break;
				case 'n': o.push_back('\n'); break;
				case 'r': o.push_back('\r'); break;
				case 't': o.push_back('\t'); break;
				default: o.push_back(inner[i]); break;
				}
			}
			return o;
		}

		bool ParseJsonStringValue(std::string_view json, std::string_view key, std::string& out)
		{
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			size_t p = json.find(needle);
			if (p == std::string::npos)
			{
				return false;
			}
			p = json.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			SkipWs(json, p);
			if (p >= json.size() || json[p] != '"')
			{
				return false;
			}
			++p;
			const size_t start = p;
			while (p < json.size())
			{
				if (json[p] == '\\')
				{
					p += 2;
					continue;
				}
				if (json[p] == '"')
				{
					out = UnescapeJsonString(json.substr(start, p - start));
					return true;
				}
				++p;
			}
			return false;
		}

		bool ParseJsonIntValue(std::string_view json, std::string_view key, int64_t& out)
		{
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			size_t p = json.find(needle);
			if (p == std::string::npos)
			{
				return false;
			}
			p = json.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			SkipWs(json, p);
			size_t q = p;
			if (q < json.size() && json[q] == '-')
			{
				++q;
			}
			while (q < json.size() && std::isdigit(static_cast<unsigned char>(json[q])) != 0)
			{
				++q;
			}
			if (q == p || (json[p] == '-' && q == p + 1))
			{
				return false;
			}
			out = std::stoll(std::string(json.substr(p, q - p)));
			return true;
		}

		bool ParseJsonUIntValue(std::string_view json, std::string_view key, uint32_t& out)
		{
			int64_t v = 0;
			if (!ParseJsonIntValue(json, key, v) || v < 0)
			{
				return false;
			}
			out = static_cast<uint32_t>(v);
			return true;
		}

		bool KeyHasNull(std::string_view json, std::string_view key)
		{
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			size_t p = json.find(needle);
			if (p == std::string::npos)
			{
				return false;
			}
			p = json.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			SkipWs(json, p);
			return p + 4 <= json.size() && json.substr(p, 4) == "null";
		}

		constexpr size_t kMaxJsonStringArrayEntries = 4096;

		/// Lit une chaîne JSON à partir du guillemet ouvrant (avance \p p après le guillemet fermant).
		bool ReadJsonStringLiteral(std::string_view json, size_t& p, std::string& out, std::string& outError, std::string_view contextKey)
		{
			if (p >= json.size() || json[p] != '"')
			{
				outError = std::string(contextKey) + ": valeur chaîne attendue (guillemet ouvrant)";
				return false;
			}
			++p;
			const size_t start = p;
			while (p < json.size())
			{
				if (json[p] == '\\')
				{
					if (p + 1 >= json.size())
					{
						outError = std::string(contextKey) + ": échappement '\\' incomplet";
						return false;
					}
					p += 2;
					continue;
				}
				if (json[p] == '"')
				{
					out = UnescapeJsonString(json.substr(start, p - start));
					++p;
					return true;
				}
				++p;
			}
			outError = std::string(contextKey) + ": chaîne JSON non terminée";
			return false;
		}

		/// Parse un tableau JSON de chaînes uniquement pour la clé \p key. Clé absente ou valeur `null` → \p out vide, succès.
		bool ParseJsonStringArray(std::string_view json, std::string_view key, std::vector<std::string>& out, std::string& outError)
		{
			out.clear();
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			const size_t keyPos = json.find(needle);
			if (keyPos == std::string::npos)
			{
				return true;
			}
			size_t p = json.find(':', keyPos + needle.size());
			if (p == std::string::npos)
			{
				outError = std::string(key) + ": ':' manquant après la clé";
				return false;
			}
			++p;
			SkipWs(json, p);
			if (p + 4 <= json.size() && json.substr(p, 4) == "null")
			{
				return true;
			}
			if (p >= json.size() || json[p] != '[')
			{
				outError = std::string(key) + ": attendu '[' ou null";
				return false;
			}
			++p;
			for (;;)
			{
				SkipWs(json, p);
				if (p < json.size() && json[p] == ']')
				{
					++p;
					return true;
				}
				std::string item;
				if (!ReadJsonStringLiteral(json, p, item, outError, key))
				{
					return false;
				}
				if (out.size() >= kMaxJsonStringArrayEntries)
				{
					outError = std::string(key) + ": trop d'entrées (max " + std::to_string(kMaxJsonStringArrayEntries) + ")";
					return false;
				}
				out.push_back(std::move(item));
				SkipWs(json, p);
				if (p < json.size() && json[p] == ',')
				{
					++p;
					continue;
				}
				if (p < json.size() && json[p] == ']')
				{
					++p;
					return true;
				}
				outError = std::string(key) + ": ',' ou ']' attendu après un élément";
				return false;
			}
		}

		std::string EscapeJson(std::string_view s)
		{
			std::string o;
			o.reserve(s.size() + 8);
			for (char c : s)
			{
				switch (c)
				{
				case '"': o += "\\\""; break;
				case '\\': o += "\\\\"; break;
				case '\b': o += "\\b"; break;
				case '\f': o += "\\f"; break;
				case '\n': o += "\\n"; break;
				case '\r': o += "\\r"; break;
				case '\t': o += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buf[8];
						std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
						o += buf;
					}
					else
					{
						o.push_back(c);
					}
					break;
				}
			}
			return o;
		}

		std::string SerializeTexturesArray(const std::vector<std::string>& items)
		{
			std::ostringstream oss;
			oss << "[";
			for (size_t i = 0; i < items.size(); ++i)
			{
				if (i)
				{
					oss << ", ";
				}
				oss << '"' << EscapeJson(items[i]) << '"';
			}
			oss << "]";
			return oss.str();
		}

		std::string NormalizeContentRelativePathTokens(std::string_view raw)
		{
			std::string s(raw);
			while (!s.empty() && (s.front() == '/' || s.front() == '\\'))
			{
				s.erase(s.begin());
			}
			for (char& c : s)
			{
				if (c == '\\')
				{
					c = '/';
				}
			}
			return s;
		}

		bool ContentRelativePathHasNoTraversal(const std::string& posixRel)
		{
			if (posixRel.empty())
			{
				return false;
			}
			if (posixRel.front() == '/')
			{
				return false;
			}
#ifdef _WIN32
			if (posixRel.find(':') != std::string::npos)
			{
				return false;
			}
#endif
			size_t i = 0;
			while (i < posixRel.size())
			{
				const size_t j = posixRel.find('/', i);
				const std::string_view seg = j == std::string::npos ? std::string_view(posixRel.data() + i, posixRel.size() - i)
				                                                     : std::string_view(posixRel.data() + i, j - i);
				if (seg.empty() || seg == "." || seg == "..")
				{
					return false;
				}
				if (j == std::string::npos)
				{
					break;
				}
				i = j + 1;
			}
			return true;
		}

		/// Parse `terrain_world_size_m` (nombre ou null). Clé absente → pas d’override (succès).
		bool ParseOptionalTerrainWorldSizeM(std::string_view json, WorldMapEditDocument& d, std::string& outError)
		{
			constexpr std::string_view kKey = "terrain_world_size_m";
			const std::string          needle = std::string("\"") + std::string(kKey) + "\"";
			const size_t               keyPos = json.find(needle);
			if (keyPos == std::string::npos)
			{
				d.hasTerrainWorldSizeM = false;
				return true;
			}
			size_t p = json.find(':', keyPos + needle.size());
			if (p == std::string::npos)
			{
				outError = std::string(kKey) + ": ':' manquant après la clé";
				return false;
			}
			++p;
			SkipWs(json, p);
			if (p + 4 <= json.size() && json.substr(p, 4) == "null")
			{
				d.hasTerrainWorldSizeM = false;
				return true;
			}
			std::string num;
			while (p < json.size())
			{
				const char c = json[p];
				if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
				{
					num.push_back(c);
					++p;
					continue;
				}
				break;
			}
			if (num.empty())
			{
				outError = std::string(kKey) + ": nombre attendu ou null";
				return false;
			}
			char* endPtr = nullptr;
			const double v = std::strtod(num.c_str(), &endPtr);
			if (static_cast<size_t>(endPtr - num.c_str()) != num.size())
			{
				outError = std::string(kKey) + ": nombre JSON invalide";
				return false;
			}
			if (v <= 0.0 || v > 1.0e7)
			{
				outError = std::string(kKey) + ": valeur hors plage (]0, 1e7] mètres)";
				return false;
			}
			d.hasTerrainWorldSizeM = true;
			d.terrainWorldSizeM    = v;
			return true;
		}
	} // namespace

	std::string SanitizeZoneId(std::string_view raw)
	{
		std::string o;
		o.reserve(raw.size());
		for (char c : raw)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (std::isalnum(u) != 0)
			{
				o.push_back(static_cast<char>(std::tolower(u)));
			}
			else if (c == '_' || c == '-')
			{
				o.push_back('_');
			}
		}
		if (o.empty())
		{
			o = "zone";
		}
		return o;
	}

	bool SaveEditDocumentJson(const std::filesystem::path& absolutePath, const WorldMapEditDocument& doc, std::string& outError)
	{
		std::error_code ec;
		std::filesystem::create_directories(absolutePath.parent_path(), ec);
		std::ofstream out(absolutePath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			outError = "impossible d’ouvrir le fichier en écriture";
			return false;
		}
		out << "{\n";
		out << "  \"zone_id\": \"" << EscapeJson(doc.zoneId) << "\",\n";
		out << "  \"version\": " << doc.formatVersion << ",\n";
		out << "  \"size\": " << doc.heightmapResolution << ",\n";
		if (doc.hasSeed)
		{
			out << "  \"seed\": " << doc.seed << ",\n";
		}
		else
		{
			out << "  \"seed\": null,\n";
		}
		out << "  \"heightmap\": \"" << EscapeJson(doc.heightmapContentRelativePath) << "\",\n";
		out << "  \"textures\": " << SerializeTexturesArray(doc.textureAssets) << ",\n";
		out << "  \"objects\": " << SerializeTexturesArray(doc.objectPrefabIds) << ",\n";
		if (doc.hasTerrainWorldSizeM)
		{
			out << "  \"terrain_world_size_m\": " << doc.terrainWorldSizeM << "\n";
		}
		else
		{
			out << "  \"terrain_world_size_m\": null\n";
		}
		out << "}\n";
		if (!out.good())
		{
			outError = "erreur d’écriture";
			return false;
		}
		return true;
	}

	bool LoadEditDocumentJson(const std::filesystem::path& absolutePath, WorldMapEditDocument& doc, std::string& outError)
	{
		std::ifstream in(absolutePath, std::ios::binary);
		if (!in.is_open())
		{
			outError = "fichier introuvable";
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		const std::string json = ss.str();
		if (json.empty())
		{
			outError = "fichier vide";
			return false;
		}
		WorldMapEditDocument d{};
		std::string s;
		if (!ParseJsonStringValue(json, "zone_id", s))
		{
			outError = "zone_id manquant ou invalide";
			return false;
		}
		d.zoneId = s;
		uint32_t ver = 0;
		if (!ParseJsonUIntValue(json, "version", ver))
		{
			d.formatVersion = WorldMapEditDocument::kFormatVersion;
		}
		else
		{
			d.formatVersion = static_cast<int>(ver);
		}
		if (d.formatVersion > WorldMapEditDocument::kFormatVersion)
		{
			outError = "version document non supportée: " + std::to_string(d.formatVersion) + " (max " +
			           std::to_string(WorldMapEditDocument::kFormatVersion) + ")";
			return false;
		}
		uint32_t sz = 256;
		(void)ParseJsonUIntValue(json, "size", sz);
		d.heightmapResolution = sz;
		if (KeyHasNull(json, "seed"))
		{
			d.hasSeed = false;
		}
		else
		{
			int64_t sd = 0;
			if (ParseJsonIntValue(json, "seed", sd))
			{
				d.hasSeed = true;
				d.seed = sd;
			}
		}
		if (!ParseJsonStringValue(json, "heightmap", s))
		{
			outError = "heightmap manquant ou invalide";
			return false;
		}
		d.heightmapContentRelativePath = s;
		if (!ParseJsonStringArray(json, "textures", d.textureAssets, outError))
		{
			return false;
		}
		if (!ParseJsonStringArray(json, "objects", d.objectPrefabIds, outError))
		{
			return false;
		}
		if (!ParseOptionalTerrainWorldSizeM(json, d, outError))
		{
			return false;
		}
		doc = std::move(d);
		return true;
	}

	bool WriteFlatHeightmapR16h(const std::filesystem::path& absolutePath, uint32_t width, uint32_t height, uint16_t normalizedHeight,
		std::string& outError)
	{
		if (width == 0 || height == 0)
		{
			outError = "dimensions invalides";
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(absolutePath.parent_path(), ec);
		std::ofstream out(absolutePath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			outError = "création heightmap impossible";
			return false;
		}
		const uint32_t magic = engine::render::terrain::kHeightmapMagic;
		out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
		out.write(reinterpret_cast<const char*>(&width), sizeof(width));
		out.write(reinterpret_cast<const char*>(&height), sizeof(height));
		std::vector<uint16_t> row(static_cast<size_t>(width), normalizedHeight);
		for (uint32_t z = 0; z < height; ++z)
		{
			out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(uint16_t)));
		}
		if (!out.good())
		{
			outError = "écriture heightmap incomplète";
			return false;
		}
		return true;
	}

	bool ExportRuntimeBundle(const engine::core::Config& cfg, const WorldMapEditDocument& doc, std::string& outError)
	{
		const std::string zid = SanitizeZoneId(doc.zoneId);
		const std::filesystem::path zoneDir = engine::platform::FileSystem::ResolveContentPath(cfg, "zones/" + zid);
		std::error_code ec;
		std::filesystem::create_directories(zoneDir, ec);
		const std::filesystem::path srcHm = engine::platform::FileSystem::ResolveContentPath(cfg, doc.heightmapContentRelativePath);
		if (!engine::platform::FileSystem::Exists(srcHm))
		{
			outError = "heightmap source absent: " + srcHm.string();
			return false;
		}
		const std::filesystem::path dstHm = zoneDir / "terrain_height.r16h";
		std::filesystem::copy_file(srcHm, dstHm, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			outError = "copie heightmap échouée: " + ec.message();
			return false;
		}

		const std::filesystem::path metaPath = zoneDir / "zone.meta";
		{
			std::ofstream meta(metaPath, std::ios::binary | std::ios::trunc);
			if (!meta.is_open())
			{
				outError = "écriture zone.meta impossible";
				return false;
			}
			engine::world::OutputVersionHeader hdr{};
			hdr.magic = engine::world::kZoneMetaMagic;
			hdr.formatVersion = engine::world::kZoneMetaVersion;
			hdr.builderVersion = engine::world::kZoneBuilderVersion;
			hdr.engineVersion = engine::world::kZoneEngineVersion;
			hdr.contentHash = 0;
			if (!engine::world::WriteOutputVersionHeader(meta, hdr))
			{
				outError = "zone.meta stream invalide";
				return false;
			}
		}

		std::vector<std::string> exportedTextureRelPosix;
		std::vector<std::string> missingTextureRelPosix;
		exportedTextureRelPosix.reserve(doc.textureAssets.size());
		missingTextureRelPosix.reserve(doc.textureAssets.size());
		for (const std::string& raw : doc.textureAssets)
		{
			const std::string rel = NormalizeContentRelativePathTokens(raw);
			if (rel.empty())
			{
				continue;
			}
			if (!ContentRelativePathHasNoTraversal(rel))
			{
				outError = "chemin texture refusé (relatif au content, sans ..) : " + raw;
				return false;
			}
			const std::filesystem::path srcTex = engine::platform::FileSystem::ResolveContentPath(cfg, rel);
			if (!engine::platform::FileSystem::Exists(srcTex))
			{
				LOG_WARN(Core, "[WorldEditor] Export runtime : texture absente, ignorée → {}", rel);
				missingTextureRelPosix.push_back(rel);
				continue;
			}
			const std::string bundledRel = std::string("zones/") + zid + "/exported_textures/" + rel;
			const std::filesystem::path dstTex = engine::platform::FileSystem::ResolveContentPath(cfg, bundledRel);
			std::filesystem::create_directories(dstTex.parent_path(), ec);
			if (ec)
			{
				outError = "création dossier export textures : " + ec.message();
				return false;
			}
			std::filesystem::copy_file(srcTex, dstTex, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				outError = "copie texture export : " + ec.message();
				return false;
			}
			exportedTextureRelPosix.push_back(bundledRel);
		}

		const std::filesystem::path manifestPath = zoneDir / "runtime_manifest.json";
		{
			std::ofstream man(manifestPath, std::ios::binary | std::ios::trunc);
			if (!man.is_open())
			{
				outError = "écriture manifest impossible";
				return false;
			}
			man << "{\n";
			man << "  \"lcdlln_runtime_manifest_version\": 2,\n";
			man << "  \"zone_id\": \"" << EscapeJson(zid) << "\",\n";
			man << "  \"terrain_heightmap\": \"zones/" << EscapeJson(zid) << "/terrain_height.r16h\",\n";
			man << "  \"source_edit_format_version\": " << doc.formatVersion << ",\n";
			if (doc.hasTerrainWorldSizeM)
			{
				man << "  \"terrain_world_size_m\": " << doc.terrainWorldSizeM << ",\n";
			}
			else
			{
				man << "  \"terrain_world_size_m\": null,\n";
			}
			man << "  \"texture_assets\": " << SerializeTexturesArray(doc.textureAssets) << ",\n";
			man << "  \"exported_textures\": " << SerializeTexturesArray(exportedTextureRelPosix) << ",\n";
			man << "  \"texture_assets_source_missing\": " << SerializeTexturesArray(missingTextureRelPosix) << ",\n";
			man << "  \"object_prefab_ids\": " << SerializeTexturesArray(doc.objectPrefabIds) << ",\n";
			man << "  \"note\": \"Paquet produit par lcdlln_world_editor — textures copiées sous exported_textures/ ; brancher zone_builder / streaming selon pipeline jeu.\"\n";
			man << "}\n";
		}

		// Stub layout consommé par zone_builder (ticket 006) : instances vides → méta + dossier chunks/ sans instances.
		const std::filesystem::path layoutStubPath = zoneDir / "layout_from_editor.json";
		{
			std::ofstream lay(layoutStubPath, std::ios::binary | std::ios::trunc);
			if (!lay.is_open())
			{
				outError = "écriture layout_from_editor.json impossible";
				return false;
			}
			lay << "{\n";
			lay << "  \"version\": 1,\n";
			lay << "  \"instances\": []\n";
			lay << "}\n";
			if (!lay.good())
			{
				outError = "écriture layout_from_editor.json incomplète";
				return false;
			}
		}

		LOG_INFO(Core, "[WorldEditor] Export runtime OK → {} (textures exportées: {}, absentes: {})", zoneDir.string(),
			exportedTextureRelPosix.size(), missingTextureRelPosix.size());
		return true;
	}

	bool ImportPngToTexr(const engine::core::Config& cfg, const std::filesystem::path& pngAbsolutePath, std::string_view texrRelativeToTextures,
		bool srgb, std::string& outError)
	{
		int w = 0;
		int h = 0;
		int comp = 0;
		stbi_uc* pixels = stbi_load(pngAbsolutePath.string().c_str(), &w, &h, &comp, 4);
		if (!pixels || w <= 0 || h <= 0)
		{
			outError = "PNG illisible ou vide";
			if (pixels)
			{
				stbi_image_free(pixels);
			}
			return false;
		}

		std::string rel(texrRelativeToTextures);
		while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
		{
			rel.erase(rel.begin());
		}
		const std::filesystem::path destRel = std::filesystem::path("textures") / rel;
		const std::filesystem::path destAbs = engine::platform::FileSystem::ResolveContentPath(cfg, destRel.generic_string());
		std::error_code ec;
		std::filesystem::create_directories(destAbs.parent_path(), ec);

		std::ofstream out(destAbs, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			stbi_image_free(pixels);
			outError = "ouverture .texr impossible";
			return false;
		}

		const uint32_t magic = 0x52584554u; // "TEXR"
		const uint32_t uw = static_cast<uint32_t>(w);
		const uint32_t uh = static_cast<uint32_t>(h);
		const uint32_t srgbFlag = srgb ? 1u : 0u;
		out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
		out.write(reinterpret_cast<const char*>(&uw), sizeof(uw));
		out.write(reinterpret_cast<const char*>(&uh), sizeof(uh));
		out.write(reinterpret_cast<const char*>(&srgbFlag), sizeof(srgbFlag));
		out.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u));
		stbi_image_free(pixels);
		if (!out.good())
		{
			outError = "écriture .texr incomplète";
			return false;
		}
		LOG_INFO(Core, "[WorldEditor] Import PNG → TEXR OK: {}", destAbs.string());
		return true;
	}

	bool ImportAudioFile(const engine::core::Config& cfg, const std::filesystem::path& srcAbsolutePath, std::string_view destRelativeToAudio,
		std::string& outError)
	{
		if (!engine::platform::FileSystem::Exists(srcAbsolutePath))
		{
			outError = "fichier audio source absent";
			return false;
		}
		std::string rel(destRelativeToAudio);
		while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
		{
			rel.erase(rel.begin());
		}
		const std::filesystem::path destRel = std::filesystem::path("audio") / rel;
		const std::filesystem::path destAbs = engine::platform::FileSystem::ResolveContentPath(cfg, destRel.generic_string());
		std::error_code ec;
		std::filesystem::create_directories(destAbs.parent_path(), ec);
		std::filesystem::copy_file(srcAbsolutePath, destAbs, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			outError = "copie audio: " + ec.message();
			return false;
		}
		LOG_INFO(Core, "[WorldEditor] Import audio OK → {}", destAbs.string());
		return true;
	}

} // namespace engine::editor
