#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/core/ZonePaths.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalIo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace engine::editor::world::volumes::dungeons
{
	uint64_t DungeonPortalDocument::Add(DungeonPortalInstance instance)
	{
		if (instance.guid == kInvalidDungeonPortalGuid)
		{
			instance.guid = NextGuid();
		}
		else
		{
			m_nextGuid = std::max(m_nextGuid, instance.guid);
		}
		const uint64_t assigned = instance.guid;
		m_instances.push_back(instance);
		m_dirty = true;
		if (m_onAdded) m_onAdded(m_instances.back());
		return assigned;
	}

	bool DungeonPortalDocument::Remove(uint64_t guid)
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const DungeonPortalInstance& i) { return i.guid == guid; });
		if (it == m_instances.end()) return false;
		m_instances.erase(it);
		m_dirty = true;
		if (m_onRemoved) m_onRemoved(guid);
		return true;
	}

	bool DungeonPortalDocument::Update(uint64_t guid, const DungeonPortalInstance& newData)
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const DungeonPortalInstance& i) { return i.guid == guid; });
		if (it == m_instances.end()) return false;
		*it = newData;
		it->guid = guid;
		m_dirty = true;
		if (m_onUpdated) m_onUpdated(*it);
		return true;
	}

	const DungeonPortalInstance* DungeonPortalDocument::GetByGuid(uint64_t guid) const
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const DungeonPortalInstance& i) { return i.guid == guid; });
		return (it == m_instances.end()) ? nullptr : &*it;
	}

	bool DungeonPortalDocument::SaveToDisk(const engine::core::Config& cfg,
		std::string& outError)
	{
		// Lot B3 — ÉCRITURE toujours sur le chemin namespacé par zone.
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ZoneInstancesFile(contentRoot, m_zoneId, "dungeon_portals.bin");
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			outError = "DungeonPortalDoc: mkdir failed: " + ec.message();
			return false;
		}
		std::vector<uint8_t> bytes;
		if (!SaveDungeonPortalsBin(m_instances, bytes, outError)) return false;
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "DungeonPortalDoc: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "DungeonPortalDoc: write failed";
			return false;
		}
		m_dirty = false;
		return true;
	}

	bool DungeonPortalDocument::LoadFromDisk(const engine::core::Config& cfg,
		std::string& outError)
	{
		// Lot B3 — LECTURE : chemin namespacé s'il existe, sinon fallback
		// sur l'ancien chemin plat (migration douce).
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ResolveInstancesFileForRead(contentRoot, m_zoneId, "dungeon_portals.bin");
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			m_instances.clear();
			m_nextGuid = 0u;
			m_dirty = false;
			return true;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0)
		{
			m_instances.clear();
			m_nextGuid = 0u;
			m_dirty = false;
			return true;
		}
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);
		if (!f.good() && !f.eof())
		{
			outError = "DungeonPortalDoc: read failed for " + path.string();
			return false;
		}
		if (!LoadDungeonPortalsBin(std::span<const uint8_t>(bytes), m_instances, outError))
			return false;
		uint64_t maxGuid = 0u;
		for (const auto& inst : m_instances)
			maxGuid = std::max(maxGuid, inst.guid);
		m_nextGuid = maxGuid;
		m_dirty = false;
		return true;
	}
}
