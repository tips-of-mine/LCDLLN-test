// Lore v1.0.114 — progression d'exploration par personnage. Voir le header.

#include "src/shardd/world/DiscoveryProgress.h"

#include "src/shared/core/Config.h"

#include <cmath>
#include <format>
#include <sstream>
#include <utility>

namespace engine::server
{
	RespawnTrailingTokens ParseRespawnTrailingTokens(std::string_view rest)
	{
		RespawnTrailingTokens out;
		std::istringstream in{ std::string(rest) };
		int positional = 0;
		for (std::string token; in >> token;)
		{
			if (token.rfind("gy=", 0) == 0)
			{
				out.graveyardId = token.substr(3);
				continue;
			}
			if (positional == 0)
				out.faction = token == "-" ? std::string() : token;
			else if (positional == 1)
			{
				try
				{
					size_t used = 0;
					const float r = std::stof(token, &used);
					if (used == token.size() && std::isfinite(r))
					{
						out.hasNeutralRadius = true;
						out.neutralRadiusM = r;
					}
				}
				catch (...)
				{
					// Rayon illisible : rayon par défaut du serveur.
				}
			}
			++positional;
		}
		return out;
	}

	uint64_t DiscoveryCatalog::BucketKey(uint8_t layer, int64_t bx, int64_t bz)
	{
		// 28 bits par axe (±134 M seaux de 256 m : bien au-delà du monde), 8 bits de couche.
		const uint64_t ux = static_cast<uint64_t>(bx + (int64_t{ 1 } << 27)) & 0x0FFFFFFFull;
		const uint64_t uz = static_cast<uint64_t>(bz + (int64_t{ 1 } << 27)) & 0x0FFFFFFFull;
		return (static_cast<uint64_t>(layer) << 56) | (ux << 28) | uz;
	}

	void DiscoveryCatalog::IndexDisc(uint8_t layer, float x, float z, float radiusM, uint32_t index)
	{
		const int64_t bx0 = static_cast<int64_t>(std::floor((x - radiusM) / kBucketM));
		const int64_t bx1 = static_cast<int64_t>(std::floor((x + radiusM) / kBucketM));
		const int64_t bz0 = static_cast<int64_t>(std::floor((z - radiusM) / kBucketM));
		const int64_t bz1 = static_cast<int64_t>(std::floor((z + radiusM) / kBucketM));
		for (int64_t bx = bx0; bx <= bx1; ++bx)
			for (int64_t bz = bz0; bz <= bz1; ++bz)
				m_buckets[BucketKey(layer, bx, bz)].push_back(index);
	}

	const std::vector<uint32_t>* DiscoveryCatalog::BucketAt(uint8_t layer, float x, float z) const
	{
		const auto it = m_buckets.find(BucketKey(layer,
			static_cast<int64_t>(std::floor(x / kBucketM)), static_cast<int64_t>(std::floor(z / kBucketM))));
		return it == m_buckets.end() ? nullptr : &it->second;
	}

	size_t DiscoveryCatalog::SetGraveyards(std::vector<GraveyardSite> sites)
	{
		// Retire l'ancien index des cimetières (couche 0) ; les lieux secrets restent.
		for (auto it = m_buckets.begin(); it != m_buckets.end();)
			it = (it->first >> 56) == 0u ? m_buckets.erase(it) : std::next(it);
		m_graveyards.clear();
		m_graveyardIndex.clear();
		size_t duplicates = 0;
		for (GraveyardSite& s : sites)
		{
			if (s.id.empty() || !std::isfinite(s.x) || !std::isfinite(s.z))
				continue;
			if (m_graveyardIndex.count(s.id) != 0u)
			{
				++duplicates;
				continue;
			}
			const uint32_t index = static_cast<uint32_t>(m_graveyards.size());
			m_graveyardIndex.emplace(s.id, index);
			IndexDisc(0u, s.x, s.z, kGraveyardVisitRadiusM, index);
			m_graveyards.push_back(std::move(s));
		}
		return duplicates;
	}

	bool DiscoveryCatalog::AppendSecretPlacesFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json))
		{
			outError = "secret_places: JSON invalide";
			return false;
		}
		const engine::world::cells::CellMap map =
			engine::world::cells::CellMapFromWorldId(c.GetString("world", "")).value_or(engine::world::cells::CellMap::Royaumes);
		// Ajoute la liste \p array (cities / pois) ; \p withRadius : lire radius_m.
		auto append = [&](const char* array, const char* countKey, bool withRadius) {
			for (int64_t i = 0; i < c.GetInt(countKey, 4096); ++i)
			{
				const std::string b = std::format("{}[{}].", array, i);
				if (!c.Has(b + "x"))
					break;
				SecretPlaceSite p;
				p.id = c.GetString(b + "id", "");
				p.name = c.GetString(b + "name", p.id);
				p.kind = c.GetString(b + "kind", withRadius ? "ville" : "lieu");
				p.x = static_cast<float>(c.GetDouble(b + "x", 0.0));
				p.z = static_cast<float>(c.GetDouble(b + "z", 0.0));
				p.map = map;
				const double r = withRadius ? c.GetDouble(b + "radius_m", 0.0) : 0.0;
				p.radiusM = (std::isfinite(r) && r > 0.0) ? static_cast<float>(r) : kSecretPlaceDefaultRadiusM;
				if (p.id.empty() || !std::isfinite(p.x) || !std::isfinite(p.z) || m_secretIndex.count(p.id) != 0u)
					continue;
				const uint32_t index = static_cast<uint32_t>(m_secrets.size());
				m_secretIndex.emplace(p.id, index);
				IndexDisc(static_cast<uint8_t>(1u + static_cast<uint8_t>(map)), p.x, p.z, p.radiusM, index);
				m_secrets.push_back(std::move(p));
			}
		};
		append("cities", "city_count", true);
		append("pois", "poi_count", false);
		return true;
	}

	bool DiscoveryCatalog::HasGraveyard(std::string_view id) const
	{
		return m_graveyardIndex.count(std::string(id)) != 0u;
	}

	int64_t DiscoveryCatalog::FindSecretPlace(std::string_view id) const
	{
		const auto it = m_secretIndex.find(std::string(id));
		return it == m_secretIndex.end() ? -1 : static_cast<int64_t>(it->second);
	}

	void DiscoveryCatalog::GraveyardsAt(float x, float z, std::vector<uint32_t>& out) const
	{
		out.clear();
		const std::vector<uint32_t>* bucket = BucketAt(0u, x, z);
		if (bucket == nullptr)
			return;
		const float r2 = kGraveyardVisitRadiusM * kGraveyardVisitRadiusM;
		for (const uint32_t i : *bucket)
		{
			const float dx = m_graveyards[i].x - x, dz = m_graveyards[i].z - z;
			if (dx * dx + dz * dz <= r2)
				out.push_back(i);
		}
	}

	void DiscoveryCatalog::SecretPlacesAt(engine::world::cells::CellMap map, float x, float z, std::vector<uint32_t>& out) const
	{
		out.clear();
		const std::vector<uint32_t>* bucket = BucketAt(static_cast<uint8_t>(1u + static_cast<uint8_t>(map)), x, z);
		if (bucket == nullptr)
			return;
		for (const uint32_t i : *bucket)
		{
			const SecretPlaceSite& p = m_secrets[i];
			const float dx = p.x - x, dz = p.z - z;
			if (p.map == map && dx * dx + dz * dz <= p.radiusM * p.radiusM)
				out.push_back(i);
		}
	}

	bool CharacterDiscoveryProgress::Load(const DiscoveryCatalog& catalog, const std::vector<std::string>& visitedGraveyards,
		const std::vector<std::string>& discoveredPlaces, bool pilgrimExploitUnlocked)
	{
		m_loaded = true;
		m_visited.clear();
		m_discovered.clear();
		m_sessionGraveyards.clear();
		m_sessionSecrets.clear();
		m_visitedKnown = 0;
		m_grantPending = false;
		m_pilgrimUnlocked = pilgrimExploitUnlocked;
		for (const std::string& id : visitedGraveyards)
			if (m_visited.insert(id).second && catalog.HasGraveyard(id))
				++m_visitedKnown;
		m_discovered.insert(discoveredPlaces.begin(), discoveredPlaces.end());
		return CheckPilgrim(catalog);
	}

	bool CharacterDiscoveryProgress::CheckPilgrim(const DiscoveryCatalog& catalog)
	{
		if (m_pilgrimUnlocked || !catalog.PilgrimExploitPossible() || m_visitedKnown < catalog.GraveyardCount())
			return false;
		m_pilgrimUnlocked = true;
		m_grantPending = true;
		return true;
	}

	bool CharacterDiscoveryProgress::AddVisit(const DiscoveryCatalog& catalog, std::string_view graveyardId, DiscoveryEvents& out)
	{
		if (!m_loaded || !catalog.HasGraveyard(graveyardId))
			return false;
		std::string id(graveyardId);
		if (!m_visited.insert(id).second)
			return false;
		++m_visitedKnown;
		m_sessionGraveyards.push_back(id);
		out.graveyards.push_back(std::move(id));
		if (CheckPilgrim(catalog))
			out.exploitUnlocked = true;
		return true;
	}

	void CharacterDiscoveryProgress::ObservePosition(const DiscoveryCatalog& catalog, engine::world::cells::CellMap map,
		float x, float z, DiscoveryEvents& out)
	{
		if (!m_loaded || !std::isfinite(x) || !std::isfinite(z))
			return;
		if (map == engine::world::cells::CellMap::Royaumes)
		{
			catalog.GraveyardsAt(x, z, m_scratch);
			for (const uint32_t i : m_scratch)
				(void)AddVisit(catalog, catalog.Graveyard(i).id, out);
		}
		catalog.SecretPlacesAt(map, x, z, m_scratch);
		for (const uint32_t i : m_scratch)
		{
			const std::string& id = catalog.SecretPlace(i).id;
			if (!m_discovered.insert(id).second)
				continue;
			m_sessionSecrets.push_back(id);
			out.secretPlaces.push_back(i);
		}
	}

	bool CharacterDiscoveryProgress::RecordGraveyardVisit(const DiscoveryCatalog& catalog, std::string_view graveyardId,
		DiscoveryEvents& out)
	{
		return AddVisit(catalog, graveyardId, out);
	}

	std::vector<uint32_t> CharacterDiscoveryProgress::DiscoveredPlaceIndices(const DiscoveryCatalog& catalog) const
	{
		std::vector<uint32_t> out;
		for (uint32_t i = 0; i < catalog.SecretPlaceCount(); ++i)
			if (m_discovered.count(catalog.SecretPlace(i).id) != 0u)
				out.push_back(i);
		return out;
	}
}
