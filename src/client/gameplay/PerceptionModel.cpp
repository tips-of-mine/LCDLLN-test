// FIX.10 — Perception (rayon de vue) du personnage local (cf. PerceptionModel.h).

#include "src/client/gameplay/PerceptionModel.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>

namespace engine::client
{
	bool PerceptionModel::Init(const engine::core::Config& config)
	{
		const std::string relativePath = "gameplay/perception.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, relativePath);
		if (jsonText.empty())
		{
			LOG_WARN(Core, "[Perception] fichier absent ou vide ({}) : rayon fixe client.nameplate.vision_distance_m", relativePath);
			return false;
		}
		std::string error;
		if (!LoadFromText(jsonText, error))
		{
			LOG_WARN(Core, "[Perception] {} invalide ({}) : rayon fixe client.nameplate.vision_distance_m", relativePath, error);
			return false;
		}
		LOG_INFO(Core, "[Perception] Init OK (base={} m, +{} m/niveau)",
			m_data.GetDouble("base_level1_m", 0.0), m_data.GetDouble("per_level_m", 0.0));
		return true;
	}

	bool PerceptionModel::LoadFromText(std::string_view jsonText, std::string& outError)
	{
		engine::core::Config data;
		if (!data.LoadFromString(jsonText))
		{
			outError = "JSON invalide";
			m_loaded = false;
			return false;
		}
		if (data.GetDouble("base_level1_m", 0.0) <= 0.0)
		{
			outError = "base_level1_m manquant ou <= 0";
			m_loaded = false;
			return false;
		}
		m_data = std::move(data);
		m_halfCircleRatio = static_cast<float>(std::clamp(m_data.GetDouble("half_circle_ratio", 0.5), 0.05, 1.0));
		m_loaded = true;
		return true;
	}

	float PerceptionModel::RadiusM(uint32_t level, std::string_view classId, std::string_view raceId,
		std::string_view gender) const
	{
		const double maxLevel = std::max(1.0, m_data.GetDouble("max_level", 100.0));
		const double lvl = std::clamp(static_cast<double>(level), 1.0, maxLevel);
		const double base = m_data.GetDouble("base_level1_m", 10.0)
			+ m_data.GetDouble("per_level_m", 0.5) * (lvl - 1.0);
		const std::string profile = m_data.GetString("class_profile." + std::string(classId), "");
		const double profileMult = profile.empty() ? 1.0 : m_data.GetDouble("profile_mult." + profile, 1.0);
		const double raceMult = m_data.GetDouble("race_mult." + std::string(raceId), 1.0);
		const std::string sex = (gender == "female") ? "female" : "male";
		const double sexMult = profile.empty() ? 1.0 : m_data.GetDouble("sex_mult." + profile + "." + sex, 1.0);
		const double radius = base * profileMult * raceMult * sexMult;
		// Arrondi au dixième « commercial » comme ROUND() du classeur (epsilon : 31,05
		// doit donner 31,1 malgré l'imprécision binaire).
		return static_cast<float>(std::round(radius * 10.0 + 1e-6) / 10.0);
	}
}
