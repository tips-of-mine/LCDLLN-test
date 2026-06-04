#pragma once

// M100.16 — Document monde des hazards (état éditeur). Header-only.

#include <fstream>
#include <string>
#include <vector>

#include "src/client/world/hazard/HazardVolumes.h"

namespace engine::editor::world
{
	class HazardDocument
	{
	public:
		void Add(const engine::world::hazard::HazardVolume& h) { m_hazards.push_back(h); }
		void Clear() { m_hazards.clear(); }
		const std::vector<engine::world::hazard::HazardVolume>& All() const { return m_hazards; }
		std::vector<engine::world::hazard::HazardVolume>& Mutable() { return m_hazards; }

		/// Écrit `instances/hazards.bin` (sérialisation partagée avec le client).
		bool SaveToDisk(const std::string& path, std::string& err) const
		{
			const std::vector<uint8_t> bytes = engine::world::hazard::SaveHazardsBin(m_hazards);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) { err = "HazardDocument::SaveToDisk: open failed: " + path; return false; }
			out.write(reinterpret_cast<const char*>(bytes.data()),
			          static_cast<std::streamsize>(bytes.size()));
			if (!out.good()) { err = "HazardDocument::SaveToDisk: write failed: " + path; return false; }
			return true;
		}

	private:
		std::vector<engine::world::hazard::HazardVolume> m_hazards;
	};
}
