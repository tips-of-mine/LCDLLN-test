#pragma once

// M100.18 — Document foliage (instances résolues d'une zone). Header-only.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "src/client/world/foliage/FoliageInstances.h"

namespace engine::editor::world
{
	class FoliageDocument
	{
	public:
		void Append(const std::vector<engine::world::foliage::FoliageInstance>& batch)
		{
			m_instances.insert(m_instances.end(), batch.begin(), batch.end());
		}

		/// Retire les `n` dernières instances (undo d'un stroke, LIFO).
		void RemoveLast(size_t n)
		{
			if (n >= m_instances.size()) m_instances.clear();
			else m_instances.resize(m_instances.size() - n);
		}

		const std::vector<engine::world::foliage::FoliageInstance>& All() const { return m_instances; }
		std::vector<engine::world::foliage::FoliageInstance>& Mutable() { return m_instances; }

		bool SaveToDisk(const std::string& path, std::string& err) const
		{
			const std::vector<uint8_t> bytes = engine::world::foliage::SaveFoliageBin(m_instances);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) { err = "FoliageDocument::SaveToDisk: open failed: " + path; return false; }
			out.write(reinterpret_cast<const char*>(bytes.data()),
			          static_cast<std::streamsize>(bytes.size()));
			if (!out.good()) { err = "FoliageDocument::SaveToDisk: write failed: " + path; return false; }
			return true;
		}

	private:
		std::vector<engine::world::foliage::FoliageInstance> m_instances;
	};
}
