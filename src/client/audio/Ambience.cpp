// SND.4 — Ambiances sonores (cf. Ambience.h).

#include "src/client/audio/Ambience.h"

#include <algorithm>
#include <cmath>

namespace engine::audio
{
	std::string_view AmbienceSoundId(AmbienceLayer layer)
	{
		switch (layer)
		{
		case AmbienceLayer::ForestDay: return "amb_forest_day";
		case AmbienceLayer::ForestNight: return "amb_forest_night";
		case AmbienceLayer::PlainsDay: return "amb_plains_day";
		case AmbienceLayer::PlainsNight: return "amb_plains_night";
		case AmbienceLayer::Swamp: return "amb_swamp";
		case AmbienceLayer::Mountain: return "amb_mountain";
		case AmbienceLayer::Snow: return "amb_snow";
		case AmbienceLayer::Desert: return "amb_desert";
		case AmbienceLayer::Coast: return "amb_coast";
		case AmbienceLayer::River: return "amb_river";
		case AmbienceLayer::Town: return "amb_town";
		case AmbienceLayer::RainLight: return "amb_rain_light";
		case AmbienceLayer::RainHeavy: return "amb_rain_heavy";
		case AmbienceLayer::Count: break;
		}
		return "";
	}

	BiomeFamily BiomeFamilyOf(char c)
	{
		switch (c)
		{
		case '.': case 'r': return BiomeFamily::Plain;
		case 'T': case 't': case 'R': case 'L': case 'o': return BiomeFamily::Forest;
		case '~': case ':': case 'l': return BiomeFamily::Desert;
		case '^': case 'M': case 'V': return BiomeFamily::Mountain;
		case '*': return BiomeFamily::SnowMountain;
		case '%': case '&': return BiomeFamily::Swamp;
		case ' ': case '-': case ',': case '_': return BiomeFamily::Coast;
		default: return BiomeFamily::Unknown;
		}
	}

	bool BiomeHasRiver(char c)
	{
		return c == 'r' || c == 'R' || c == '&';
	}

	char BiomeAt(std::string_view grid, int size, double x, double z, double minX, double minZ, double sizeM)
	{
		if (size <= 0 || sizeM <= 0.0 || grid.size() < static_cast<size_t>(size) * size)
			return '\0';
		const double u = (x - minX) / sizeM, v = (z - minZ) / sizeM;
		if (u < 0.0 || v < 0.0 || u >= 1.0 || v >= 1.0)
			return '\0';
		const int i = std::min(size - 1, static_cast<int>(u * size));
		const int j = std::min(size - 1, static_cast<int>(v * size));
		return grid[static_cast<size_t>(j) * size + i];
	}

	float DaylightAt(float hours)
	{
		hours = std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
		if (hours < 5.0f || hours >= 21.0f)
			return 0.0f;
		if (hours < 6.5f)
			return (hours - 5.0f) / 1.5f;
		if (hours < 19.5f)
			return 1.0f;
		return (21.0f - hours) / 1.5f;
	}

	std::array<float, kAmbienceLayerCount> AmbienceTargets(const AmbienceInputs& in)
	{
		std::array<float, kAmbienceLayerCount> t{};
		auto set = [&](AmbienceLayer l, float v) { t[static_cast<size_t>(l)] = std::clamp(v, 0.0f, 1.0f); };
		const BiomeFamily fam = BiomeFamilyOf(in.biome);
		const float day = std::clamp(in.daylight, 0.0f, 1.0f);
		const float night = 1.0f - day;
		const float rain = std::clamp(in.rain, 0.0f, 1.0f);
		// Sous la pluie, oiseaux et insectes se taisent en grande partie.
		const float wildlife = 1.0f - 0.7f * rain;

		// Altitude / neige : le vent de montagne monte au-dessus de 280 m.
		const float altWind = std::clamp((in.altitude - 280.0f) / 220.0f, 0.0f, 1.0f);
		const bool snowy = fam == BiomeFamily::SnowMountain || in.snowGround;
		float mountain = fam == BiomeFamily::Mountain ? 1.0f : altWind;
		float snow = snowy ? 1.0f : 0.0f;
		if (snowy)
			mountain *= 0.4f;

		// Forêt : biome forestier ou arbres denses autour du joueur.
		float forest = std::max(fam == BiomeFamily::Forest ? 0.75f : 0.0f, std::clamp(in.treeDensity, 0.0f, 1.0f));
		float swamp = fam == BiomeFamily::Swamp ? 1.0f : 0.0f;
		float desert = fam == BiomeFamily::Desert ? 1.0f : 0.0f;
		float coast = (fam == BiomeFamily::Coast || in.seaNearby) ? 1.0f : 0.0f;
		const float river = (in.waterNearby && !in.seaNearby) ? 0.8f : (BiomeHasRiver(in.biome) ? 0.25f : 0.0f);
		// Plaine : ce qui n'est ni forêt, ni marais, ni désert, ni haute montagne.
		float plains = std::max(0.0f, 1.0f - forest - swamp - desert - std::max(mountain, snow) * 0.8f);
		if (fam == BiomeFamily::Unknown && forest < 0.01f)
			plains = std::max(plains, 0.6f);

		// En ville, la nature recule derrière la rumeur des rues.
		const float town = in.inTown ? 1.0f : 0.0f;
		const float natureScale = in.inTown ? 0.35f : 1.0f;
		forest *= natureScale;
		plains *= natureScale;
		swamp *= natureScale;
		desert *= natureScale;

		set(AmbienceLayer::ForestDay, forest * day * wildlife);
		set(AmbienceLayer::ForestNight, forest * night * wildlife);
		set(AmbienceLayer::PlainsDay, plains * day * wildlife);
		set(AmbienceLayer::PlainsNight, plains * night * wildlife);
		set(AmbienceLayer::Swamp, swamp * (0.6f + 0.4f * night) * wildlife);
		set(AmbienceLayer::Mountain, mountain);
		set(AmbienceLayer::Snow, snow);
		set(AmbienceLayer::Desert, desert);
		set(AmbienceLayer::Coast, coast);
		set(AmbienceLayer::River, river);
		set(AmbienceLayer::Town, town * (0.45f + 0.55f * day)); // la ville dort la nuit
		set(AmbienceLayer::RainLight, in.storm ? 0.0f : rain);
		set(AmbienceLayer::RainHeavy, in.storm ? std::max(rain, 0.6f) : 0.0f);
		return t;
	}
}
