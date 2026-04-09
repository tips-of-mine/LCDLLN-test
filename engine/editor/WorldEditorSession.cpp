#include "engine/editor/WorldEditorSession.h"

#include "engine/editor/WorldMapIo.h"

#include <array>
#include <algorithm>
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace engine::editor
{
	namespace
	{
		template<size_t N>
		void SetBuf(std::array<char, N>& buf, std::string_view s)
		{
			const size_t n = std::min(s.size(), N - 1);
			std::memcpy(buf.data(), s.data(), n);
			buf[n] = '\0';
		}

		bool ParseU32(std::string_view s, uint32_t& out)
		{
			out = 0;
			if (s.empty())
			{
				return false;
			}
			const char* first = s.data();
			const char* last = first + s.size();
			const auto r = std::from_chars(first, last, out);
			return r.ec == std::errc{} && r.ptr == last;
		}

		bool ParseI64(std::string_view s, int64_t& out)
		{
			out = 0;
			if (s.empty())
			{
				return false;
			}
			const char* first = s.data();
			const char* last = first + s.size();
			const auto r = std::from_chars(first, last, out);
			return r.ec == std::errc{} && r.ptr == last;
		}

		uint32_t NextPow2Clamp(uint32_t v)
		{
			if (v < 64u)
			{
				return 64u;
			}
			if (v > 2048u)
			{
				return 2048u;
			}
			uint32_t p = 64u;
			while (p < v && p < 2048u)
			{
				p *= 2u;
			}
			return p;
		}
	} // namespace

	WorldEditorSession::WorldEditorSession()
	{
		SetBuf(m_bufZoneId, m_doc.zoneId);
		SetBuf(m_bufSize, "256");
		m_bufSeed[0] = '\0';
	}

	void WorldEditorSession::SetStatus(std::string_view message)
	{
		m_status.assign(message.begin(), message.end());
	}

	void WorldEditorSession::SyncBuffersFromDoc()
	{
		SetBuf(m_bufZoneId, m_doc.zoneId);
		char tmp[32];
		std::snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(m_doc.heightmapResolution));
		SetBuf(m_bufSize, tmp);
		if (m_doc.hasSeed)
		{
			std::snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(m_doc.seed));
			SetBuf(m_bufSeed, tmp);
		}
		else
		{
			m_bufSeed[0] = '\0';
		}
	}

	void WorldEditorSession::SyncDocIdFromBuffer()
	{
		m_doc.zoneId.assign(m_bufZoneId.data());
		if (m_doc.zoneId.empty())
		{
			m_doc.zoneId = "untitled_zone";
		}
	}

	void WorldEditorSession::RequestTerrainGpuReload()
	{
		m_terrainGpuReloadRequested = true;
	}

	bool WorldEditorSession::ConsumeTerrainGpuReloadRequest()
	{
		if (!m_terrainGpuReloadRequested)
		{
			return false;
		}
		m_terrainGpuReloadRequested = false;
		return true;
	}

	bool WorldEditorSession::ActionNewMap(const engine::core::Config& cfg)
	{
		SyncDocIdFromBuffer();
		uint32_t sz = 256;
		(void)ParseU32(std::string_view(m_bufSize.data()), sz);
		sz = NextPow2Clamp(sz);

		const std::string zid = SanitizeZoneId(m_doc.zoneId);
		m_doc.zoneId = zid;
		m_doc.heightmapResolution = sz;
		if (m_bufSeed[0] != '\0')
		{
			int64_t sd = 0;
			if (ParseI64(std::string_view(m_bufSeed.data()), sd))
			{
				m_doc.hasSeed = true;
				m_doc.seed = sd;
			}
		}
		else
		{
			m_doc.hasSeed = false;
		}

		const std::string relDir = "world_editor/maps/" + zid;
		const std::filesystem::path dirAbs = engine::platform::FileSystem::ResolveContentPath(cfg, relDir);
		const std::filesystem::path hmAbs = dirAbs / "height.r16h";
		const std::string hmRel = relDir + "/height.r16h";
		std::string err;
		if (!WriteFlatHeightmapR16h(hmAbs, sz, sz, 32768u, err))
		{
			SetStatus("Nouvelle carte: " + err);
			LOG_ERROR(Core, "[WorldEditor] {}", err);
			return false;
		}
		m_doc.heightmapContentRelativePath = hmRel;
		m_doc.textureAssets.clear();
		m_doc.objectPrefabIds.clear();
		m_doc.formatVersion = WorldMapEditDocument::kFormatVersion;

		const std::filesystem::path jsonAbs = dirAbs / "map.lcdlln_edit.json";
		m_editJsonAbsolutePath = jsonAbs.string();
		SetBuf(m_bufSavePath, m_editJsonAbsolutePath);
		if (!SaveEditDocumentJson(jsonAbs, m_doc, err))
		{
			SetStatus("Carte créée mais sauvegarde JSON échouée: " + err);
			LOG_WARN(Core, "[WorldEditor] {}", err);
			RequestTerrainGpuReload();
			return true;
		}
		SetStatus("Nouvelle carte OK — " + hmRel);
		RequestTerrainGpuReload();
		SyncBuffersFromDoc();
		LOG_INFO(Core, "[WorldEditor] New map OK zone={} size={}", zid, sz);
		return true;
	}

	bool WorldEditorSession::ActionSaveEditJson(const engine::core::Config& cfg)
	{
		(void)cfg;
		SyncDocIdFromBuffer();
		std::string path(m_bufSavePath.data());
		if (path.empty())
		{
			SetStatus("Chemin sauvegarde vide.");
			return false;
		}
		std::string err;
		if (!SaveEditDocumentJson(std::filesystem::path(path), m_doc, err))
		{
			SetStatus("Sauvegarde: " + err);
			return false;
		}
		m_editJsonAbsolutePath = path;
		SetStatus("Sauvegardé: " + path);
		return true;
	}

	bool WorldEditorSession::ActionLoadEditJson(const engine::core::Config& cfg)
	{
		(void)cfg;
		std::string path(m_bufLoadPath.data());
		if (path.empty())
		{
			SetStatus("Chemin chargement vide.");
			return false;
		}
		std::string err;
		if (!LoadEditDocumentJson(std::filesystem::path(path), m_doc, err))
		{
			SetStatus("Chargement: " + err);
			return false;
		}
		m_editJsonAbsolutePath = path;
		SetBuf(m_bufSavePath, m_editJsonAbsolutePath);
		SetStatus("Chargé: " + path);
		RequestTerrainGpuReload();
		SyncBuffersFromDoc();
		return true;
	}

	bool WorldEditorSession::ActionExportRuntime(const engine::core::Config& cfg)
	{
		std::string err;
		if (!ExportRuntimeBundle(cfg, m_doc, err))
		{
			SetStatus("Export: " + err);
			return false;
		}
		SetStatus("Export runtime OK → game/data/zones/" + SanitizeZoneId(m_doc.zoneId) + "/");
		return true;
	}

	bool WorldEditorSession::ActionImportTexture(const engine::core::Config& cfg)
	{
		const std::string png(m_bufPngPath.data());
		const std::string dest(m_bufTexrName.data());
		if (png.empty() || dest.empty())
		{
			SetStatus("Import texture: chemins PNG ou nom .texr requis.");
			return false;
		}
		std::string err;
		if (!ImportPngToTexr(cfg, std::filesystem::path(png), dest, true, err))
		{
			SetStatus("Import texture: " + err);
			return false;
		}
		const std::string rel = std::string("textures/") + dest;
		m_doc.textureAssets.push_back(rel);
		SetStatus("Texture importée: " + rel);
		return true;
	}

	bool WorldEditorSession::ActionImportAudio(const engine::core::Config& cfg)
	{
		const std::string src(m_bufAudioSrc.data());
		const std::string dst(m_bufAudioDest.data());
		if (src.empty() || dst.empty())
		{
			SetStatus("Import audio: chemins requis (source absolue, dest relative à audio/).");
			return false;
		}
		std::string err;
		if (!ImportAudioFile(cfg, std::filesystem::path(src), dst, err))
		{
			SetStatus("Import audio: " + err);
			return false;
		}
		SetStatus("Audio copié vers audio/" + dst + " (aucune lecture).");
		return true;
	}

} // namespace engine::editor
