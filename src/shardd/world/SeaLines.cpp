// Lignes maritimes — passeur au ponton (voir SeaLines.h).

#include "src/shardd/world/SeaLines.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace engine::server
{
	namespace cells = engine::world::cells;

	bool SeaLines::AddFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "sea_lines: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "sea_lines: schema_version 1 attendu"; return false; }
		const std::string world = c.GetString("world", "");
		const std::optional<cells::CellMap> map = cells::CellMapFromWorldId(world);
		if (!map) { outError = "sea_lines: monde inconnu '" + world + "'"; return false; }
		std::vector<SeaLine> added;
		for (int li = 0; li < 256; ++li)
		{
			const std::string b = std::format("lines[{}].", li);
			if (!c.Has(b + "id")) break;
			SeaLine line;
			line.id = c.GetString(b + "id", "");
			line.name = c.GetString(b + "name", line.id);
			line.map = *map;
			line.loop = c.GetBool(b + "loop", false);
			line.priceGoldPerLeg = static_cast<uint32_t>(std::clamp<int64_t>(c.GetInt(b + "price_gold_per_leg", 10), 0, 100000));
			for (int k = 0; k < 64; ++k)
			{
				const std::string s = std::format("{}stops[{}].", b, k);
				if (!c.Has(s + "city")) break;
				if (!c.Has(s + "x") || !c.Has(s + "z")) continue; // escale sans ponton
				SeaLineStop st;
				st.cityId = c.GetString(s + "city", "");
				st.name = c.GetString(s + "name", st.cityId);
				st.x = c.GetDouble(s + "x", 0.0);
				st.y = c.GetDouble(s + "y", 0.0);
				st.z = c.GetDouble(s + "z", 0.0);
				st.yawDeg = static_cast<float>(c.GetDouble(s + "yaw_deg", 0.0));
				if (std::isfinite(st.x) && std::isfinite(st.y) && std::isfinite(st.z))
					line.stops.push_back(std::move(st));
			}
			if (!line.id.empty() && line.stops.size() >= 2)
				added.push_back(std::move(line));
		}
		for (SeaLine& l : added)
		{
			auto it = std::find_if(m_lines.begin(), m_lines.end(), [&](const SeaLine& o) { return o.id == l.id; });
			if (it != m_lines.end()) *it = std::move(l);
			else m_lines.push_back(std::move(l));
		}
		return true;
	}

	bool SeaLines::LoadForCellWorld(const engine::core::Config& config, std::string& outError)
	{
		Clear();
		for (int i = 0; i < cells::kCellMapCount; ++i)
		{
			const std::string rel = "world/" + std::string(cells::WorldIdOf(static_cast<cells::CellMap>(i))) + "/sea_lines.json";
			const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
			if (text.empty()) continue;
			std::string err;
			if (!AddFromText(text, err))
			{
				Clear();
				outError = err + " (" + rel + ")";
				return false;
			}
		}
		return true;
	}

	const SeaLine* SeaLines::Find(std::string_view id) const
	{
		for (const SeaLine& l : m_lines)
			if (l.id == id) return &l;
		return nullptr;
	}

	uint32_t SeaLines::Legs(const SeaLine& line, size_t from, size_t to)
	{
		const size_t n = line.stops.size();
		const size_t d = from > to ? from - to : to - from;
		return static_cast<uint32_t>(line.loop ? std::min(d, n - d) : d);
	}

	SeaTravelRefusal SeaLines::Plan(std::string_view lineId, size_t toStop, cells::CellMap map, double x, double z,
		SeaTravelPlan& outPlan) const
	{
		const SeaLine* line = Find(lineId);
		if (line == nullptr || line->map != map) return SeaTravelRefusal::UnknownLine;
		if (toStop >= line->stops.size()) return SeaTravelRefusal::BadStop;
		// Escale de départ : le ponton le plus proche, dans le rayon.
		size_t from = line->stops.size();
		double best = kPontonRadiusM;
		for (size_t k = 0; k < line->stops.size(); ++k)
		{
			const double d = std::hypot(line->stops[k].x - x, line->stops[k].z - z);
			if (d <= best) { best = d; from = k; }
		}
		if (from == line->stops.size()) return SeaTravelRefusal::NotAtPonton;
		if (from == toStop) return SeaTravelRefusal::SameStop;
		outPlan.line = line;
		outPlan.fromStop = from;
		outPlan.toStop = toStop;
		outPlan.legs = Legs(*line, from, toStop);
		outPlan.priceGold = outPlan.legs * line->priceGoldPerLeg;
		outPlan.arrival = &line->stops[toStop];
		return SeaTravelRefusal::None;
	}
}
