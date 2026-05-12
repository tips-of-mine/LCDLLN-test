// src/client/world/hazard/HazardVolumes.cpp
#include "src/client/world/hazard/HazardVolumes.h"

#include <cmath>

namespace engine::world::hazard
{
	bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept
	{
		switch (hz.shape)
		{
			case HazardShape::Box:
			{
				const float dx = worldPos.x - hz.position.x;
				const float dy = worldPos.y - hz.position.y;
				const float dz = worldPos.z - hz.position.z;
				return std::fabs(dx) <= hz.boxHalfExtents.x
					&& std::fabs(dy) <= hz.boxHalfExtents.y
					&& std::fabs(dz) <= hz.boxHalfExtents.z;
			}
			case HazardShape::Cylinder:
			{
				if (worldPos.y < hz.position.y) return false;
				if (worldPos.y > hz.position.y + hz.cylHeight) return false;
				const float dx = worldPos.x - hz.position.x;
				const float dz = worldPos.z - hz.position.z;
				return (dx * dx + dz * dz) <= (hz.cylRadius * hz.cylRadius);
			}
		}
		return false;
	}

	bool SaveHazardsBin(const HazardScene& /*scene*/,
		std::vector<uint8_t>& /*outBytes*/, std::string& outError)
	{
		// stub : implémenté en Task 2.
		outError = "SaveHazardsBin not implemented yet (Task 2)";
		return false;
	}

	bool LoadHazardsBin(std::span<const uint8_t> /*bytes*/,
		HazardScene& /*outScene*/, std::string& outError)
	{
		// stub : implémenté en Task 2.
		outError = "LoadHazardsBin not implemented yet (Task 2)";
		return false;
	}
}
