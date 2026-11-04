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

	void StartLocationTable::Clear()
	{
		m_byRace.clear();
		m_defaultRace.clear();
		for (std::vector<cells::CellCoord>& list : m_generatedCells)
			list.clear();
		m_generatedCellIds.clear();
	}

	bool StartLocationTable::LoadFromText(std::string_view json, std::string& outError)
	{
		Clear();
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "start_locations: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "start_locations: schema_version 1 attendu"; return false; }
		// Carte de la table : champ "world" ("enfers" → Enfers ; absent ou autre → Royaumes).
		const cells::CellMap map = cells::CellMapFromWorldId(c.GetString("world", "royaumes")).value_or(cells::CellMap::Royaumes);
		std::vector<cells::CellCoord>& generated = m_generatedCells[static_cast<size_t>(map)];
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
			s.map = map;
			if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) continue;
			m_byRace.emplace(s.race, std::move(s));
		}
		m_defaultRace = c.GetString("default_race", "humains");
		if (m_byRace.empty()) { outError = "start_locations: aucune race"; return false; }
		// Cellules générées : liste « N003_W009,N011_E008 » (identifiants du lore).
		const std::string cellsText = c.GetString("generated_cells", "");
		size_t begin = 0;
		while (begin < cellsText.size())
		{
			size_t end = cellsText.find(',', begin);
			if (end == std::string::npos) end = cellsText.size();
			std::string_view token(cellsText.data() + begin, end - begin);
			while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
			while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
			if (!token.empty())
			{
				const std::optional<cells::CellCoord> cell = cells::ParseLoreCellId(token);
				if (!cell || !cells::IsInLoreGrid(*cell, map))
				{
					Clear();
					outError = "start_locations: cellule invalide dans generated_cells '" + std::string(token) + "'";
					return false;
				}
				if (m_generatedCellIds.insert(cells::CellZoneId(*cell, map)).second)
					generated.push_back(*cell);
			}
			begin = end + 1;
		}
		return true;
	}

	bool StartLocationTable::MergeFromText(std::string_view json, std::string& outError)
	{
		StartLocationTable other;
		if (!other.LoadFromText(json, outError)) return false;
		for (int i = 0; i < cells::kCellMapCount; ++i)
		{
			if (!other.m_generatedCells[i].empty() && !m_generatedCells[i].empty())
			{
				outError = "start_locations: carte '" + std::string(cells::WorldIdOf(static_cast<cells::CellMap>(i))) + "' deja chargee";
				return false;
			}
		}
		for (const auto& [race, start] : other.m_byRace)
		{
			if (m_byRace.count(race) != 0)
			{
				outError = "start_locations: race '" + race + "' deja dotee d'un depart";
				return false;
			}
		}
		for (auto& [race, start] : other.m_byRace)
			m_byRace.emplace(race, std::move(start));
		for (int i = 0; i < cells::kCellMapCount; ++i)
			for (const cells::CellCoord& cell : other.m_generatedCells[i])
				m_generatedCells[i].push_back(cell);
		m_generatedCellIds.insert(other.m_generatedCellIds.begin(), other.m_generatedCellIds.end());
		return true;
	}

	bool StartLocationTable::LoadForWorld(const engine::core::Config& config, std::string_view world, std::string& outError)
	{
		std::string warningIgnored;
		return LoadForWorld(config, world, outError, warningIgnored);
	}

	bool StartLocationTable::LoadForWorld(const engine::core::Config& config, std::string_view world, std::string& outError,
		std::string& outWarning)
	{
		outWarning.clear();
		const std::string relativePath = "world/" + std::string(world) + "/start_locations.json";
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, relativePath);
		if (text.empty())
		{
			Clear();
			outError = "start_locations: fichier absent (" + relativePath + ")";
			return false;
		}
		if (!LoadFromText(text, outError)) return false;
		// Seconde carte : les Enfers accompagnent toujours le continent.
		if (cells::IsCellWorld(world))
		{
			const std::string enfersPath = "world/" + std::string(cells::WorldIdOf(cells::CellMap::Enfers)) + "/start_locations.json";
			const std::string enfersText = engine::platform::FileSystem::ReadAllTextContent(config, enfersPath);
			std::string mergeError;
			if (enfersText.empty())
				outWarning = "start_locations: " + enfersPath + " absent (pas de carte des Enfers)";
			else if (!MergeFromText(enfersText, mergeError))
				outWarning = mergeError + " (" + enfersPath + " ignore)";
		}
		return true;
	}

	const std::vector<cells::CellCoord>& StartLocationTable::GeneratedCells(cells::CellMap map) const
	{
		return m_generatedCells[static_cast<size_t>(map)];
	}

	bool StartLocationTable::IsGeneratedCell(cells::CellCoord c, cells::CellMap map) const
	{
		if (GeneratedCells(map).empty()) return map == cells::CellMap::Royaumes;
		// Hors grille de la carte : jamais générée (et CellZoneId exige |n|,|e| < 500).
		if (!cells::IsInLoreGrid(c, map)) return false;
		return m_generatedCellIds.count(cells::CellZoneId(c, map)) != 0;
	}

	std::vector<StartLocation> StartLocationTable::All() const
	{
		std::vector<StartLocation> out;
		out.reserve(m_byRace.size());
		for (const auto& [race, start] : m_byRace)
		{
			(void)race;
			out.push_back(start);
		}
		return out;
	}

	const StartLocation* StartLocationTable::Resolve(std::string_view race) const
	{
		if (auto it = m_byRace.find(std::string(race)); it != m_byRace.end()) return &it->second;
		if (auto it = m_byRace.find(m_defaultRace); it != m_byRace.end()) return &it->second;
		return nullptr;
	}
}
