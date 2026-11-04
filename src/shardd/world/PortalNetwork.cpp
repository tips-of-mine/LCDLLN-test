// Seconde carte (Les Enfers) — réseau de portails entre cartes. Voir PortalNetwork.h.

#include "src/shardd/world/PortalNetwork.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace engine::server
{
	namespace cells = engine::world::cells;

	bool PortalNetwork::AddFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "portals: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "portals: schema_version 1 attendu"; return false; }
		const std::string world = c.GetString("world", "");
		const std::optional<cells::CellMap> map = cells::CellMapFromWorldId(world);
		if (!map) { outError = "portals: monde inconnu '" + world + "'"; return false; }

		std::vector<PortalEndpoint> added;
		const int64_t count = std::clamp<int64_t>(c.GetInt("portal_count", 64), 0, 4096);
		for (int64_t i = 0; i < count; ++i)
		{
			const std::string b = std::format("portals[{}].", i);
			if (!c.Has(b + "id")) break;
			PortalEndpoint p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.map = *map;
			p.x = c.GetDouble(b + "x", 0.0);
			p.y = c.GetDouble(b + "y", 0.0);
			p.z = c.GetDouble(b + "z", 0.0);
			p.radiusMeters = static_cast<float>(std::clamp(c.GetDouble(b + "radius_m", 6.0), 0.5, 50.0));
			p.arrivalOffsetMeters = static_cast<float>(std::clamp(c.GetDouble(b + "arrival_offset_m", 8.0), 0.0, 100.0));
			// Lien "<monde>:<id>" vers une AUTRE carte ; sinon le portail est ignoré.
			const std::string link = c.GetString(b + "link", "");
			const size_t colon = link.find(':');
			if (p.id.empty() || colon == std::string::npos || colon + 1 >= link.size()) continue;
			const std::optional<cells::CellMap> linkMap = cells::CellMapFromWorldId(std::string_view(link).substr(0, colon));
			if (!linkMap || *linkMap == *map) continue;
			if (!std::isfinite(p.x) || !std::isfinite(p.z)) continue;
			p.linkMap = *linkMap;
			p.linkId = link.substr(colon + 1);
			added.push_back(std::move(p));
		}
		for (PortalEndpoint& p : added)
		{
			// Identifiant unique par carte : une seconde entrée remplace la première.
			auto it = std::find_if(m_portals.begin(), m_portals.end(),
				[&](const PortalEndpoint& o) { return o.map == p.map && o.id == p.id; });
			if (it != m_portals.end()) *it = std::move(p);
			else m_portals.push_back(std::move(p));
		}
		return true;
	}

	bool PortalNetwork::LoadForCellWorld(const engine::core::Config& config, std::string& outError, std::string& outWarning)
	{
		Clear();
		outWarning.clear();
		for (int i = 0; i < cells::kCellMapCount; ++i)
		{
			const std::string rel = "world/" + std::string(cells::WorldIdOf(static_cast<cells::CellMap>(i))) + "/portals.json";
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

	size_t PortalNetwork::LinkedCount() const
	{
		return static_cast<size_t>(std::count_if(m_portals.begin(), m_portals.end(),
			[this](const PortalEndpoint& p) { return Find(p.linkMap, p.linkId) != nullptr; }));
	}

	const PortalEndpoint* PortalNetwork::Find(cells::CellMap map, std::string_view id) const
	{
		for (const PortalEndpoint& p : m_portals)
			if (p.map == map && p.id == id)
				return &p;
		return nullptr;
	}

	const PortalEndpoint* PortalNetwork::PortalAt(cells::CellMap map, double x, double z) const
	{
		for (const PortalEndpoint& p : m_portals)
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

	std::optional<PortalDestination> PortalNetwork::DestinationOf(const PortalEndpoint& from) const
	{
		const PortalEndpoint* to = Find(from.linkMap, from.linkId);
		if (to == nullptr)
			return std::nullopt;
		PortalDestination d;
		d.map = to->map;
		// Arrivée au sud (+Z) du portail apparié, hors de son rayon : pas de renvoi immédiat.
		const double offset = std::max(static_cast<double>(to->arrivalOffsetMeters), static_cast<double>(to->radiusMeters) + 1.0);
		d.x = to->x;
		d.y = to->y;
		d.z = to->z + offset;
		d.fromId = from.id;
		d.toId = to->id;
		return d;
	}

	std::optional<PortalDestination> PortalNetwork::Update(PortalTravelState& state, cells::CellMap map,
		double x, double z, bool alive, uint64_t nowMs) const
	{
		if (!std::isfinite(x) || !std::isfinite(z))
			return std::nullopt;
		const PortalEndpoint* inside = PortalAt(map, x, z);
		if (!state.armed)
		{
			// Réarmement : sorti de tout rayon depuis kRearmDelayMs.
			if (inside != nullptr)
			{
				state.outsideSinceMs.reset();
				return std::nullopt;
			}
			if (!state.outsideSinceMs)
			{
				state.outsideSinceMs = nowMs;
				return std::nullopt;
			}
			if (nowMs - *state.outsideSinceMs < kRearmDelayMs)
				return std::nullopt;
			state.armed = true;
			state.outsideSinceMs.reset();
		}
		if (!alive || inside == nullptr)
			return std::nullopt;
		std::optional<PortalDestination> destination = DestinationOf(*inside);
		if (destination)
			Disarm(state);
		return destination;
	}
}
