// SND.2 — Carte des sols d'une cellule (cf. CellSurfaceMap.h).

#include "src/client/world/cells/CellSurfaceMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::world::cells
{
	namespace
	{
		constexpr uint32_t kSlapMagic = 0x50414C53u; // "SLAP" (TerrainSplatting.h)
		constexpr uint32_t kMaxDim = 16384u;
	}

	bool CellSurfaceMap::BuildFromSlap(std::span<const uint8_t> f)
	{
		m_layers.clear();
		m_width = m_height = 0;
		if (f.size() < 12u)
			return false;
		uint32_t magic = 0, w = 0, h = 0;
		std::memcpy(&magic, f.data(), 4);
		std::memcpy(&w, f.data() + 4, 4);
		std::memcpy(&h, f.data() + 8, 4);
		if (magic != kSlapMagic || w == 0 || h == 0 || w > kMaxDim || h > kMaxDim)
			return false;
		const size_t n = static_cast<size_t>(w) * h;
		if (f.size() != 12u + n * 4u)
			return false;
		m_layers.resize(n);
		const uint8_t* px = f.data() + 12;
		for (size_t i = 0; i < n; ++i, px += 4)
		{
			uint8_t best = 0;
			for (uint8_t c = 1; c < 4; ++c)
				if (px[c] > px[best])
					best = c;
			m_layers[i] = best;
		}
		m_width = w;
		m_height = h;
		return true;
	}

	int CellSurfaceMap::LayerAt(double x, double z, double minX, double minZ, double sizeM) const
	{
		if (m_layers.empty() || sizeM <= 0.0)
			return -1;
		const double u = (x - minX) / sizeM;
		const double v = (z - minZ) / sizeM;
		if (u < 0.0 || v < 0.0 || u >= 1.0 || v >= 1.0)
			return -1;
		const uint32_t i = std::min(m_width - 1u, static_cast<uint32_t>(u * m_width));
		const uint32_t j = std::min(m_height - 1u, static_cast<uint32_t>(v * m_height));
		return m_layers[static_cast<size_t>(j) * m_width + i];
	}
}
