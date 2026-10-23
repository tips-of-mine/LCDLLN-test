#include "src/shared/world/StartLocations.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <cmath>

namespace engine::world
{
	namespace
	{
		/// Races jouables connues (cf. game/data/races/races.json) : le Config
		/// aplatit le JSON en clés pointées sans énumération, on sonde donc la
		/// liste connue (toute race supplémentaire doit être ajoutée ici).
		constexpr const char* kKnownRaces[] = { "humains", "elfes", "orcs", "nains", "demons", "morts_vivants", "divins" };
	}

	bool StartLocationTable::LoadFromText(std::string_view json, std::string& outError)
	{
		m_byRace.clear();
		m_defaultRace.clear();
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "start_locations: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "start_locations: schema_version 1 attendu"; return false; }
		for (const char* race : kKnownRaces)
		{
			const std::string base = std::string("races.") + race + ".";
			if (!c.Has(base + "x") || !c.Has(base + "z")) continue;
			StartLocation s;
			s.race = race;
			s.cityId = c.GetString(base + "city", "");
			s.cityName = c.GetString(base + "city_name", s.cityId);
			s.x = c.GetDouble(base + "x", 0.0);
			s.y = c.GetDouble(base + "y", 100.0);
			s.z = c.GetDouble(base + "z", 0.0);
			s.yawDeg = c.GetDouble(base + "yaw_deg", 0.0);
			if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) continue;
			m_byRace.emplace(s.race, std::move(s));
		}
		m_defaultRace = c.GetString("default_race", "humains");
		if (m_byRace.empty()) { outError = "start_locations: aucune race"; return false; }
		return true;
	}

	bool StartLocationTable::LoadFromContent(const engine::core::Config& config, std::string& outError)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, "world/start_locations.json");
		if (text.empty())
		{
			m_byRace.clear();
			outError = "start_locations: fichier absent (world/start_locations.json)";
			return false;
		}
		return LoadFromText(text, outError);
	}

	const StartLocation* StartLocationTable::Resolve(std::string_view race) const
	{
		if (auto it = m_byRace.find(std::string(race)); it != m_byRace.end()) return &it->second;
		if (auto it = m_byRace.find(m_defaultRace); it != m_byRace.end()) return &it->second;
		return nullptr;
	}
}
