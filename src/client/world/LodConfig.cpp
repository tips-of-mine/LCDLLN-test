#include "src/client/world/LodConfig.h"
#include "src/shared/core/Config.h"

#include <algorithm>
#include <cmath>

namespace engine::world
{
	void LodConfig::Init(const engine::core::Config& config)
	{
		m_distanceMax[0] = static_cast<float>(config.GetDouble("lod.distance_lod0_max", 25.0));
		m_distanceMax[1] = static_cast<float>(config.GetDouble("lod.distance_lod1_max", 60.0));
		m_distanceMax[2] = static_cast<float>(config.GetDouble("lod.distance_lod2_max", 150.0));
		m_distanceMax[3] = static_cast<float>(config.GetDouble("lod.distance_lod3_max", 400.0));
		// Ensure ascending order.
		for (int i = 1; i < kLodLevelCount; ++i)
			if (m_distanceMax[i] < m_distanceMax[i - 1])
				m_distanceMax[i] = m_distanceMax[i - 1];

		// Seuil de bascule mesh -> impostor (M45.5b). Centralise ici plutot que
		// dans world.impostor.distance_m. Defaut 60 m si la cle est absente.
		m_impostorDistanceMax = static_cast<float>(config.GetDouble("lod.distance_impostor_m", 60.0));
	}

	int LodConfig::GetLodLevel(float distanceMeters) const
	{
		if (distanceMeters <= 0.0f)
			return 0;
		for (int lod = 0; lod < kLodLevelCount; ++lod)
			if (distanceMeters <= m_distanceMax[lod])
				return lod;
		return kLodLevelCount - 1;
	}

	float LodConfig::GetDistanceMax(int lodLevel) const
	{
		const int i = std::clamp(lodLevel, 0, kLodLevelCount - 1);
		return m_distanceMax[i];
	}
}
