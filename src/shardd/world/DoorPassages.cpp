// Camp de l'Oracle (lore v1.0.141) — passages conditionnels d'une même carte.
// Voir DoorPassages.h.

#include "src/shardd/world/DoorPassages.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace engine::server
{
	namespace cells = engine::world::cells;

	bool DoorPassages::AddFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "passages: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "passages: schema_version 1 attendu"; return false; }
		const std::string world = c.GetString("world", "");
		const std::optional<cells::CellMap> map = cells::CellMapFromWorldId(world);
		if (!map) { outError = "passages: monde inconnu '" + world + "'"; return false; }

		std::vector<DoorPassage> added;
		const int64_t count = std::clamp<int64_t>(c.GetInt("passage_count", 64), 0, 4096);
		for (int64_t i = 0; i < count; ++i)
		{
			const std::string b = std::format("passages[{}].", i);
			if (!c.Has(b + "id")) break;
			DoorPassage p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.map = *map;
			p.x = c.GetDouble(b + "x", NAN);
			p.y = c.GetDouble(b + "y", 0.0);
			p.z = c.GetDouble(b + "z", NAN);
			p.radiusMeters = static_cast<float>(std::clamp(c.GetDouble(b + "radius_m", 2.5), 0.5, 20.0));
			p.toX = c.GetDouble(b + "to_x", NAN);
			p.toY = c.GetDouble(b + "to_y", 0.0);
			p.toZ = c.GetDouble(b + "to_z", NAN);
			p.toYawDeg = static_cast<float>(c.GetDouble(b + "to_yaw_deg", 0.0));
			p.minGroup = static_cast<uint32_t>(std::clamp<int64_t>(c.GetInt(b + "min_group", 0), 0, 40));
			p.deniedMessage = c.GetString(b + "denied_message", "");
			for (int64_t q = 0; q < 16; ++q)
			{
				const std::string key = std::format("{}requires_quests[{}]", b, q);
				if (!c.Has(key)) break;
				const std::string questId = c.GetString(key, "");
				if (!questId.empty()) p.requiredQuests.push_back(questId);
			}
			if (p.id.empty() || !std::isfinite(p.x) || !std::isfinite(p.z) || !std::isfinite(p.toX) || !std::isfinite(p.toZ))
				continue;
			// L'arrivée doit être hors du rayon de son propre déclencheur (pas de boucle).
			if (std::hypot(p.toX - p.x, p.toZ - p.z) <= static_cast<double>(p.radiusMeters))
				continue;
			added.push_back(std::move(p));
		}
		for (DoorPassage& p : added)
		{
			// Identifiant unique par carte : une seconde entrée remplace la première.
			auto it = std::find_if(m_passages.begin(), m_passages.end(),
				[&](const DoorPassage& o) { return o.map == p.map && o.id == p.id; });
			if (it != m_passages.end()) *it = std::move(p);
			else m_passages.push_back(std::move(p));
		}
		return true;
	}

	bool DoorPassages::LoadForCellWorld(const engine::core::Config& config, std::string& outError, std::string& outWarning)
	{
		Clear();
		outWarning.clear();
		for (int i = 0; i < cells::kCellMapCount; ++i)
		{
			const std::string rel = "world/" + std::string(cells::WorldIdOf(static_cast<cells::CellMap>(i))) + "/passages.json";
			const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
			if (text.empty())
			{
				outWarning += (outWarning.empty() ? "" : " ; ") + rel + " absent";
				continue;
			}
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

	const DoorPassage* DoorPassages::Find(cells::CellMap map, std::string_view id) const
	{
		for (const DoorPassage& p : m_passages)
			if (p.map == map && p.id == id)
				return &p;
		return nullptr;
	}

	const DoorPassage* DoorPassages::PassageAt(cells::CellMap map, double x, double z) const
	{
		for (const DoorPassage& p : m_passages)
		{
			if (p.map != map)
				continue;
			const double dx = x - p.x;
			const double dz = z - p.z;
			const double r = static_cast<double>(p.radiusMeters);
			if (dx * dx + dz * dz <= r * r)
				return &p;
		}
		return nullptr;
	}

	bool DoorPassages::ConditionsMet(const DoorPassage& p, const std::function<bool(std::string_view)>& questSatisfied,
		uint32_t groupSize)
	{
		for (const std::string& q : p.requiredQuests)
			if (!questSatisfied || !questSatisfied(q))
				return false;
		return p.minGroup <= 1u || groupSize >= p.minGroup;
	}

	DoorPassageResult DoorPassages::Update(DoorPassageTravelState& state, cells::CellMap map,
		double x, double z, bool alive, uint64_t nowMs, const std::function<bool(const DoorPassage&)>& allowed) const
	{
		DoorPassageResult result;
		if (!std::isfinite(x) || !std::isfinite(z))
			return result;
		const DoorPassage* inside = PassageAt(map, x, z);
		// Sorti du rayon d'un passage refusé : le prochain refus sera de nouveau signalé.
		if (!state.deniedInsideId.empty() && (inside == nullptr || inside->id != state.deniedInsideId))
			state.deniedInsideId.clear();
		if (!state.armed)
		{
			if (inside != nullptr)
			{
				state.outsideSinceMs.reset();
				return result;
			}
			if (!state.outsideSinceMs)
			{
				state.outsideSinceMs = nowMs;
				return result;
			}
			if (nowMs - *state.outsideSinceMs < kRearmDelayMs)
				return result;
			state.armed = true;
			state.outsideSinceMs.reset();
		}
		if (!alive || inside == nullptr)
			return result;
		if (allowed && allowed(*inside))
		{
			state.deniedInsideId.clear();
			Disarm(state);
			result.outcome = DoorPassageOutcome::Pass;
			result.passage = inside;
			return result;
		}
		if (state.deniedInsideId == inside->id)
			return result;
		state.deniedInsideId = inside->id;
		result.outcome = DoorPassageOutcome::Denied;
		result.passage = inside;
		return result;
	}
}
