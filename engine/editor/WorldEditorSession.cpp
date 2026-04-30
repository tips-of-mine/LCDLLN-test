#include "engine/editor/WorldEditorSession.h"

#include "engine/editor/WorldMapIo.h"

#include "engine/world/WorldModel.h"

#include <array>
#include <algorithm>
#include <cmath>
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"
#include "engine/render/terrain/TerrainGrassDetail.h"
#include "engine/render/terrain/TerrainHoleMask.h"

#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
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

		std::string NewLayoutInstanceGuid()
		{
			static std::atomic<uint64_t> seq{1};
			const uint64_t n = seq.fetch_add(1u, std::memory_order_relaxed);
			char buf[48];
			std::snprintf(buf, sizeof(buf), "we_inst_%llu", static_cast<unsigned long long>(n));
			return std::string(buf);
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
		: m_treeRng(std::random_device{}())
	{
		SetBuf(m_bufZoneId, m_doc.zoneId);
		SetBuf(m_bufSize, "256");
		m_bufSeed[0] = '\0';
	}

	void WorldEditorSession::SetStatus(std::string_view message)
	{
		m_status.assign(message.begin(), message.end());
	}

	void WorldEditorSession::SetTerrainSaveHook(std::function<bool(const engine::core::Config&, const WorldMapEditDocument&)> hook)
	{
		m_terrainSaveHook = std::move(hook);
	}

	void WorldEditorSession::SanitizeAllLayoutInstancesAgainstTreeCatalog()
	{
		for (WorldMapEditLayoutInstance& inst : m_doc.layoutInstances)
		{
			m_treeCatalog.SanitizeLayoutInstance(inst);
		}
	}

	void WorldEditorSession::EnsureTreeCatalogLoaded(const engine::core::Config& cfg)
	{
		if (m_treeCatalogLoadAttempted)
		{
			return;
		}
		m_treeCatalogLoadAttempted = true;
		std::string err;
		if (!m_treeCatalog.LoadFromFile(cfg, "world_editor/tree_species_catalog.json", err))
		{
			SetStatus(std::string("Catalogue arbres: ") + err);
			return;
		}
		SanitizeAllLayoutInstancesAgainstTreeCatalog();
		const int n = static_cast<int>(m_treeCatalog.Species().size());
		if (n > 0)
		{
			m_treeSpeciesUiIndex = std::clamp(m_treeSpeciesUiIndex, 0, n - 1);
			const auto& sp = m_treeCatalog.Species()[static_cast<size_t>(m_treeSpeciesUiIndex)];
			const int ns = static_cast<int>(sp.shapes.size());
			if (ns > 0)
			{
				m_treeShapeVariantUiIndex = std::clamp(m_treeShapeVariantUiIndex, 0, ns - 1);
			}
		}
	}

	void WorldEditorSession::PlaceOrMoveLayoutInstanceAtTerrainHit(const engine::core::Config& cfg, double worldX, double worldY, double worldZ)
	{
		EnsureTreeCatalogLoaded(cfg);
		if (m_selectedLayoutInstance >= 0
			&& m_selectedLayoutInstance < static_cast<int>(m_doc.layoutInstances.size()))
		{
			WorldMapEditLayoutInstance& inst = m_doc.layoutInstances[static_cast<size_t>(m_selectedLayoutInstance)];
			inst.worldX = worldX;
			inst.worldY = worldY;
			inst.worldZ = worldZ;
			SetStatus("Instance déplacée.");
			return;
		}
		const int kind = std::clamp(m_instancePlacementKind, 0, 1);
		WorldMapEditLayoutInstance inst{};
		inst.guid = NewLayoutInstanceGuid();
		if (kind == 1)
		{
			inst.gltfContentRelativePath = "zones/zone_1/zone_1.gltf";
			inst.speciesId.clear();
			inst.shapeVariantIndex = 0u;
			inst.uniformScale = 1.0;
		}
		else
		{
			if (m_treeCatalog.Species().empty())
			{
				SetStatus("Aucune espèce d’arbre valide (catalogue absent ou sans 2+ glTF par espèce). Utilisez « Rocher » ou corrigez le JSON.");
				return;
			}
			const int si = std::clamp(m_treeSpeciesUiIndex, 0, static_cast<int>(m_treeCatalog.Species().size()) - 1);
			const TreeSpeciesSpec& sp = m_treeCatalog.Species()[static_cast<size_t>(si)];
			const int nShapes = static_cast<int>(sp.shapes.size());
			const int shI = std::clamp(m_treeShapeVariantUiIndex, 0, std::max(0, nShapes - 1));
			inst.speciesId = sp.id;
			inst.shapeVariantIndex = static_cast<uint32_t>(shI);
			inst.gltfContentRelativePath = sp.shapes[static_cast<size_t>(shI)].gltfContentRelativePath;
			double scale = 1.0;
			if (m_treeRandomizeScaleOnPlace)
			{
				std::uniform_real_distribution<double> dist(sp.scaleMin, sp.scaleMax);
				scale = dist(m_treeRng);
			}
			else
			{
				const double t = std::clamp(static_cast<double>(m_treeScaleT01), 0.0, 1.0);
				scale = sp.scaleMin + (sp.scaleMax - sp.scaleMin) * t;
			}
			inst.uniformScale = std::clamp(scale, sp.scaleMin, sp.scaleMax);
		}
		inst.worldX = worldX;
		inst.worldY = worldY;
		inst.worldZ = worldZ;
		m_doc.layoutInstances.push_back(std::move(inst));
		SetStatus(kind == 1 ? "Rocher placé." : "Arbre placé.");
	}

	void WorldEditorSession::RemoveLayoutInstance(size_t index)
	{
		if (index >= m_doc.layoutInstances.size())
		{
			return;
		}
		m_doc.layoutInstances.erase(m_doc.layoutInstances.begin() + static_cast<std::ptrdiff_t>(index));
		if (m_selectedLayoutInstance == static_cast<int>(index))
		{
			m_selectedLayoutInstance = -1;
		}
		else if (m_selectedLayoutInstance > static_cast<int>(index))
		{
			--m_selectedLayoutInstance;
		}
		SetStatus("Instance supprimée.");
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
		const std::filesystem::path splatAbs = dirAbs / "splat.slap";
		const std::string splatRel = relDir + "/splat.slap";
		if (!WriteDefaultTerrainSplatSlap(splatAbs, 1024u, 1024u, err))
		{
			SetStatus("Nouvelle carte: " + err);
			LOG_ERROR(Core, "[WorldEditor] {}", err);
			return false;
		}
		m_doc.splatmapContentRelativePath = splatRel;
		const std::filesystem::path grassAbs = dirAbs / "grass.grms";
		const std::string grassRel = relDir + "/grass.grms";
		{
			engine::render::terrain::HoleMaskData gm;
			engine::render::terrain::TerrainGrassDetail::GenerateZeros(1024u, 1024u, gm);
			if (!engine::render::terrain::TerrainGrassDetail::SaveToFile(grassAbs.string(), gm))
			{
				SetStatus("Nouvelle carte: écriture grass.grms impossible");
				LOG_ERROR(Core, "[WorldEditor] grass.grms write failed: {}", grassAbs.string());
				return false;
			}
		}
		m_doc.grassMaskContentRelativePath = grassRel;
		m_doc.textureAssets.clear();
		m_doc.objectPrefabIds.clear();
		m_doc.layoutInstances.clear();
		m_doc.routes.clear();
		m_routeDraftXz.clear();
		m_routeApplyDraftRequested = false;
		m_selectedLayoutInstance = -1;
		m_instancePlacementKind = 0;
		m_treeSpeciesUiIndex = 0;
		m_treeShapeVariantUiIndex = 0;
		m_treeScaleT01 = 0.5f;
		m_doc.formatVersion = WorldMapEditDocument::kFormatVersion;
		m_doc.hasTerrainWorldSizeM = true;
		m_doc.terrainWorldSizeM    = static_cast<double>(engine::world::kZoneSize);

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
		SyncDocIdFromBuffer();
		EnsureTreeCatalogLoaded(cfg);
		SanitizeAllLayoutInstancesAgainstTreeCatalog();
		std::string path(m_bufSavePath.data());
		if (path.empty())
		{
			SetStatus("Chemin sauvegarde vide.");
			return false;
		}
		if (m_terrainSaveHook && !m_terrainSaveHook(cfg, m_doc))
		{
			SetStatus("Sauvegarde: échec écriture des fichiers terrain (heightmap / splat / grass).");
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
		m_treeCatalogLoadAttempted = false;
		m_editJsonAbsolutePath = path;
		SetBuf(m_bufSavePath, m_editJsonAbsolutePath);
		SetStatus("Chargé: " + path);
		m_selectedLayoutInstance = -1;
		m_routeDraftXz.clear();
		m_routeApplyDraftRequested = false;
		EnsureTreeCatalogLoaded(cfg);
		RequestTerrainGpuReload();
		SyncBuffersFromDoc();
		return true;
	}

	bool WorldEditorSession::ActionExportRuntime(const engine::core::Config& cfg)
	{
		EnsureTreeCatalogLoaded(cfg);
		SanitizeAllLayoutInstancesAgainstTreeCatalog();
		std::string err;
		if (!ExportRuntimeBundle(cfg, m_doc, err))
		{
			SetStatus("Export: " + err);
			return false;
		}
		SetStatus("Export runtime OK → game/data/zones/" + SanitizeZoneId(m_doc.zoneId) + "/");
		return true;
	}

	std::filesystem::path WorldEditorSession::CanonicalMapJsonPath(const engine::core::Config& cfg, std::string_view zoneId)
	{
		const std::string sanitized = SanitizeZoneId(zoneId);
		const std::string relDir = std::string(kMapsContentRelativeDir) + "/" + sanitized;
		return engine::platform::FileSystem::ResolveContentPath(cfg, relDir) / kEditDocFilename;
	}

	void WorldEditorSession::RefreshAvailableMaps(const engine::core::Config& cfg)
	{
		m_availableMapsScanned = true;
		m_availableMapIds.clear();
		const std::filesystem::path mapsRoot = engine::platform::FileSystem::ResolveContentPath(cfg, kMapsContentRelativeDir);
		std::error_code ec;
		if (!std::filesystem::exists(mapsRoot, ec) || !std::filesystem::is_directory(mapsRoot, ec))
		{
			m_selectedAvailableMapIndex = 0;
			return;
		}
		for (const std::filesystem::directory_entry& e : std::filesystem::directory_iterator(mapsRoot, ec))
		{
			if (ec)
			{
				break;
			}
			if (!e.is_directory(ec))
			{
				continue;
			}
			const std::filesystem::path docPath = e.path() / kEditDocFilename;
			if (!std::filesystem::is_regular_file(docPath, ec))
			{
				continue;
			}
			m_availableMapIds.emplace_back(e.path().filename().string());
		}
		std::sort(m_availableMapIds.begin(), m_availableMapIds.end());
		if (m_availableMapIds.empty())
		{
			m_selectedAvailableMapIndex = 0;
		}
		else
		{
			m_selectedAvailableMapIndex = std::clamp(m_selectedAvailableMapIndex, 0,
				static_cast<int>(m_availableMapIds.size()) - 1);
		}
	}

	bool WorldEditorSession::ActionSaveCurrentMap(const engine::core::Config& cfg)
	{
		SyncDocIdFromBuffer();
		EnsureTreeCatalogLoaded(cfg);
		SanitizeAllLayoutInstancesAgainstTreeCatalog();
		const std::string zid = SanitizeZoneId(m_doc.zoneId);
		if (zid.empty())
		{
			SetStatus("Sauvegarde: nom de carte vide.");
			return false;
		}
		m_doc.zoneId = zid;
		const std::filesystem::path jsonAbs = CanonicalMapJsonPath(cfg, zid);
		std::error_code ec;
		std::filesystem::create_directories(jsonAbs.parent_path(), ec);
		if (m_terrainSaveHook && !m_terrainSaveHook(cfg, m_doc))
		{
			SetStatus("Sauvegarde: échec écriture des fichiers terrain (heightmap / splat / grass).");
			return false;
		}
		std::string err;
		if (!SaveEditDocumentJson(jsonAbs, m_doc, err))
		{
			SetStatus("Sauvegarde: " + err);
			return false;
		}
		m_editJsonAbsolutePath = jsonAbs.string();
		SetBuf(m_bufSavePath, m_editJsonAbsolutePath);
		SetStatus("Carte sauvegardée: " + zid);
		LOG_INFO(Core, "[WorldEditor] Saved map zone={} -> {}", zid, m_editJsonAbsolutePath);
		RefreshAvailableMaps(cfg);
		for (int i = 0; i < static_cast<int>(m_availableMapIds.size()); ++i)
		{
			if (m_availableMapIds[static_cast<size_t>(i)] == zid)
			{
				m_selectedAvailableMapIndex = i;
				break;
			}
		}
		return true;
	}

	bool WorldEditorSession::ActionLoadMapByZoneId(const engine::core::Config& cfg, std::string_view zoneId)
	{
		const std::string zid = SanitizeZoneId(zoneId);
		if (zid.empty())
		{
			SetStatus("Chargement: nom de carte vide.");
			return false;
		}
		const std::filesystem::path jsonAbs = CanonicalMapJsonPath(cfg, zid);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(jsonAbs, ec))
		{
			SetStatus("Chargement: fichier introuvable -> " + jsonAbs.string());
			return false;
		}
		std::string err;
		if (!LoadEditDocumentJson(jsonAbs, m_doc, err))
		{
			SetStatus("Chargement: " + err);
			return false;
		}
		m_doc.zoneId = SanitizeZoneId(m_doc.zoneId.empty() ? zid : m_doc.zoneId);
		m_treeCatalogLoadAttempted = false;
		m_editJsonAbsolutePath = jsonAbs.string();
		SetBuf(m_bufSavePath, m_editJsonAbsolutePath);
		SetBuf(m_bufLoadPath, m_editJsonAbsolutePath);
		SetStatus("Carte chargée: " + zid);
		LOG_INFO(Core, "[WorldEditor] Loaded map zone={} -> {}", zid, m_editJsonAbsolutePath);
		m_selectedLayoutInstance = -1;
		m_routeDraftXz.clear();
		m_routeApplyDraftRequested = false;
		EnsureTreeCatalogLoaded(cfg);
		RequestTerrainGpuReload();
		SyncBuffersFromDoc();
		RefreshAvailableMaps(cfg);
		for (int i = 0; i < static_cast<int>(m_availableMapIds.size()); ++i)
		{
			if (m_availableMapIds[static_cast<size_t>(i)] == zid)
			{
				m_selectedAvailableMapIndex = i;
				break;
			}
		}
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

	void WorldEditorSession::ClearRouteDraft()
	{
		m_routeDraftXz.clear();
	}

	void WorldEditorSession::AddRouteDraftPoint(double worldX, double worldZ)
	{
		m_routeDraftXz.emplace_back(worldX, worldZ);
	}

	void WorldEditorSession::RequestApplyRouteDraftToSplat()
	{
		m_routeApplyDraftRequested = true;
	}

	bool WorldEditorSession::ConsumeRouteApplyDraftRequest()
	{
		if (!m_routeApplyDraftRequested)
		{
			return false;
		}
		m_routeApplyDraftRequested = false;
		return true;
	}

} // namespace engine::editor
