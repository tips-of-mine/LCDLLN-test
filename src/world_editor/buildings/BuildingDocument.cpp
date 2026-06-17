#include "src/world_editor/buildings/BuildingDocument.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/core/ZonePaths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace engine::editor::world::buildings
{
	uint64_t BuildingDocument::Add(BuildingInstance building)
	{
		if (building.guid == 0u)
		{
			building.guid = NextGuid();
		}
		else
		{
			m_nextGuid = std::max(m_nextGuid, building.guid);
		}
		const uint64_t assigned = building.guid;
		m_buildings.push_back(std::move(building));
		m_dirty = true;
		if (m_onAdded) m_onAdded(m_buildings.back());
		return assigned;
	}

	bool BuildingDocument::Remove(uint64_t guid)
	{
		auto it = std::find_if(m_buildings.begin(), m_buildings.end(),
			[guid](const BuildingInstance& b) { return b.guid == guid; });
		if (it == m_buildings.end()) return false;
		m_buildings.erase(it);
		m_dirty = true;
		if (m_onRemoved) m_onRemoved(guid);
		return true;
	}

	bool BuildingDocument::Update(uint64_t guid, const BuildingInstance& newData)
	{
		auto it = std::find_if(m_buildings.begin(), m_buildings.end(),
			[guid](const BuildingInstance& b) { return b.guid == guid; });
		if (it == m_buildings.end()) return false;
		*it = newData;
		it->guid = guid; // préserve le guid quoi qu'il arrive
		m_dirty = true;
		if (m_onUpdated) m_onUpdated(*it);
		return true;
	}

	const BuildingDocument::BuildingInstance* BuildingDocument::GetByGuid(uint64_t guid) const
	{
		auto it = std::find_if(m_buildings.begin(), m_buildings.end(),
			[guid](const BuildingInstance& b) { return b.guid == guid; });
		return (it == m_buildings.end()) ? nullptr : &*it;
	}

	BuildingDocument::BuildingInstance* BuildingDocument::MutableByGuid(uint64_t guid)
	{
		auto it = std::find_if(m_buildings.begin(), m_buildings.end(),
			[guid](const BuildingInstance& b) { return b.guid == guid; });
		if (it == m_buildings.end()) return nullptr;
		m_dirty = true;
		return &*it;
	}

	bool BuildingDocument::SaveToDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ZoneInstancesFile(contentRoot, m_zoneId, "buildings.bin");
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			outError = "BuildingDocument: mkdir failed: " + ec.message();
			return false;
		}

		const std::vector<uint8_t> bytes =
			engine::world::instances::SaveBuildingsBin(m_buildings);

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "BuildingDocument: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "BuildingDocument: write failed";
			return false;
		}
		m_dirty = false;
		return true;
	}

	bool BuildingDocument::LoadFromDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ResolveInstancesFileForRead(contentRoot, m_zoneId, "buildings.bin");

		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			m_buildings.clear();
			m_nextGuid = 0u;
			m_dirty = false;
			return true;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0)
		{
			m_buildings.clear();
			m_nextGuid = 0u;
			m_dirty = false;
			return true;
		}
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);
		if (!f.good() && !f.eof())
		{
			outError = "BuildingDocument: read failed for " + path.string();
			return false;
		}

		if (!engine::world::instances::LoadBuildingsBin(
				std::span<const uint8_t>(bytes), m_buildings, outError))
			return false;

		uint64_t maxGuid = 0u;
		for (const auto& b : m_buildings) maxGuid = std::max(maxGuid, b.guid);
		m_nextGuid = maxGuid;
		m_dirty = false;
		return true;
	}
}
