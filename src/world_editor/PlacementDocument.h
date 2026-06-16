#pragma once

// M100.17 — Document de placement (props posés dans la zone). Header-only.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "src/client/world/instances/PropInstances.h"

namespace engine::editor::world
{
	class PlacementDocument
	{
	public:
		uint32_t AllocInstanceId() { return m_nextInstanceId++; }

		void Add(const engine::world::instances::PropInstance& p) { m_props.push_back(p); }

		void RemoveById(uint32_t instanceId)
		{
			m_props.erase(std::remove_if(m_props.begin(), m_props.end(),
				[instanceId](const engine::world::instances::PropInstance& p)
				{ return p.instanceId == instanceId; }), m_props.end());
		}

		/// Alloue un identifiant de groupe unique non nul (auberge, bâtiment…).
		uint32_t AllocGroupId() { return m_nextGroupId++; }

		/// Retire toutes les instances membres du groupe `groupId` (no-op si 0).
		void RemoveByGroup(uint32_t groupId)
		{
			if (groupId == 0) return;
			m_props.erase(std::remove_if(m_props.begin(), m_props.end(),
				[groupId](const engine::world::instances::PropInstance& p)
				{ return p.groupId == groupId; }), m_props.end());
		}

		const std::vector<engine::world::instances::PropInstance>& All() const { return m_props; }
		std::vector<engine::world::instances::PropInstance>& Mutable() { return m_props; }

		/// Écrit `instances/props.bin` (sérialisation partagée avec le client).
		bool SaveToDisk(const std::string& path, std::string& err) const
		{
			const std::vector<uint8_t> bytes = engine::world::instances::SavePropsBin(m_props);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) { err = "PlacementDocument::SaveToDisk: open failed: " + path; return false; }
			out.write(reinterpret_cast<const char*>(bytes.data()),
			          static_cast<std::streamsize>(bytes.size()));
			if (!out.good()) { err = "PlacementDocument::SaveToDisk: write failed: " + path; return false; }
			return true;
		}

	private:
		std::vector<engine::world::instances::PropInstance> m_props;
		uint32_t m_nextInstanceId = 1;
		uint32_t m_nextGroupId = 1;
	};
}
