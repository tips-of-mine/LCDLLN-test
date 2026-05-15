#include "src/world_editor/zone_presets/CustomizationApplier.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world::zone_presets
{
	namespace
	{
		bool NearlyOne(float v)
		{
			return std::fabs(v - 1.0f) < 1e-4f;
		}
	}

	bool CustomizationParams::IsNeutral() const
	{
		return NearlyOne(reliefMultiplier)
			&& NearlyOne(waterDensityMultiplier)
			&& NearlyOne(drynessMultiplier);
	}

	bool HasAffectedByTag(const std::vector<std::string>& affectedBy,
		const std::string& tag)
	{
		return std::find(affectedBy.begin(), affectedBy.end(), tag) != affectedBy.end();
	}

	void ApplyCustomization(OperationParams& params,
		const std::vector<std::string>& affectedBy,
		const CustomizationParams& custom)
	{
		// "relief" : hauteurs / profondeurs / amplitudes du terrain macro.
		if (HasAffectedByTag(affectedBy, "relief"))
		{
			const double f = static_cast<double>(custom.reliefMultiplier);
			params.ScaleNumber("heightMeters", f);
			params.ScaleNumber("depthMeters", f);
			params.ScaleNumber("amplitudeMeters", f);
		}

		// "water_density" : intensité de la simulation hydrique.
		if (HasAffectedByTag(affectedBy, "water_density"))
		{
			const double f = static_cast<double>(custom.waterDensityMultiplier);
			params.ScaleNumber("numDroplets", f);
			params.ScaleNumber("flowAccumThreshold", f);
		}

		// "dryness" : plus aride → plus d'évaporation, plus de vent.
		if (HasAffectedByTag(affectedBy, "dryness"))
		{
			const double f = static_cast<double>(custom.drynessMultiplier);
			params.ScaleNumber("evaporationRate", f);
			params.ScaleNumber("windStrength", f);
		}
	}
}
