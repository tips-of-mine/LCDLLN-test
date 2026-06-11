#include "src/world_editor/volumes/MeshInsertDocument.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/core/ZonePaths.h"
#include "src/world_editor/volumes/MeshInsertIo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

namespace engine::editor::world::volumes
{
	uint64_t MeshInsertDocument::Add(MeshInsertInstance instance)
	{
		if (instance.guid == kInvalidMeshInsertGuid)
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

	bool MeshInsertDocument::Remove(uint64_t guid)
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const MeshInsertInstance& i) { return i.guid == guid; });
		if (it == m_instances.end()) return false;
		m_instances.erase(it);
		m_dirty = true;
		if (m_onRemoved) m_onRemoved(guid);
		return true;
	}

	bool MeshInsertDocument::Update(uint64_t guid, const MeshInsertInstance& newData)
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const MeshInsertInstance& i) { return i.guid == guid; });
		if (it == m_instances.end()) return false;
		*it = newData;
		it->guid = guid; // préserve le guid quoi qu'il arrive
		m_dirty = true;
		if (m_onUpdated) m_onUpdated(*it);
		return true;
	}

	const MeshInsertInstance* MeshInsertDocument::GetByGuid(uint64_t guid) const
	{
		auto it = std::find_if(m_instances.begin(), m_instances.end(),
			[guid](const MeshInsertInstance& i) { return i.guid == guid; });
		return (it == m_instances.end()) ? nullptr : &*it;
	}

	std::vector<MeshInsertInstance> MeshInsertDocument::GetByCategory(
		const std::string& category) const
	{
		std::vector<MeshInsertInstance> out;
		for (const auto& inst : m_instances)
		{
			if (inst.insertCategory == category) out.push_back(inst);
		}
		return out;
	}

	bool MeshInsertDocument::SaveToDisk(const engine::core::Config& cfg,
		std::string& outError)
	{
		// Lot B3 — ÉCRITURE toujours sur le chemin namespacé par zone.
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ZoneInstancesFile(contentRoot, m_zoneId, "mesh_inserts.bin");
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			outError = "MeshInsertDocument: mkdir failed: " + ec.message();
			return false;
		}

		std::vector<uint8_t> bytes;
		if (!SaveMeshInsertsBin(m_instances, bytes, outError)) return false;

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "MeshInsertDocument: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "MeshInsertDocument: write failed";
			return false;
		}
		m_dirty = false;
		return true;
	}

	bool MeshInsertDocument::LoadFromDisk(const engine::core::Config& cfg,
		std::string& outError)
	{
		// Lot B3 — LECTURE : chemin namespacé s'il existe, sinon fallback
		// sur l'ancien chemin plat (migration douce).
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			zone_paths::ResolveInstancesFileForRead(contentRoot, m_zoneId, "mesh_inserts.bin");

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
			outError = "MeshInsertDocument: read failed for " + path.string();
			return false;
		}

		if (!LoadMeshInsertsBin(std::span<const uint8_t>(bytes), m_instances, outError))
			return false;

		// Initialise le compteur Guid au max+1 des instances chargées pour
		// éviter les collisions sur les futurs Add.
		uint64_t maxGuid = 0u;
		for (const auto& inst : m_instances)
		{
			maxGuid = std::max(maxGuid, inst.guid);
		}
		m_nextGuid = maxGuid;
		m_dirty = false;
		return true;
	}
}
