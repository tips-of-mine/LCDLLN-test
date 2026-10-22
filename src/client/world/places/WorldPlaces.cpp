#include "src/client/world/places/WorldPlaces.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace engine::client
{
	namespace
	{
		/// Lit un réel ; accepte un entier JSON (le Config distingue int/double).
		float GetF(const engine::core::Config& c, const std::string& key, float def)
		{
			return static_cast<float>(c.GetDouble(key, def));
		}
	}

	bool WorldPlaces::LoadFromText(std::string_view json, std::string& outError)
	{
		*this = WorldPlaces{};
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "places: JSON invalide"; return false; }
		if (c.GetInt("schema_version", 0) != 1) { outError = "places: schema_version 1 attendu"; return false; }
		if (!c.Has("terrain.size_m")) { outError = "places: terrain.size_m absent"; return false; }

		m_displayName = c.GetString("display_name", "");
		m_mapImage = c.GetString("map_image", "");
		m_originX = GetF(c, "terrain.origin_x", 0.0f);
		m_originZ = GetF(c, "terrain.origin_z", 0.0f);
		m_sizeM = std::max(1.0f, GetF(c, "terrain.size_m", 1.0f));

		// Le Config aplatit les tableaux en `cities[i].champ` : on sonde par index
		// jusqu'au premier trou (les compteurs *_count servent de borne haute).
		const int64_t cityCount = c.GetInt("city_count", 4096);
		for (int64_t i = 0; i < cityCount; ++i)
		{
			const std::string b = std::format("cities[{}].", i);
			if (!c.Has(b + "x")) break;
			PlaceCity p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.kind = c.GetString(b + "kind", "");
			p.race = c.GetString(b + "race", "");
			p.state = c.GetString(b + "state", "");
			p.description = c.GetString(b + "description", "");
			p.x = GetF(c, b + "x", 0.0f);
			p.z = GetF(c, b + "z", 0.0f);
			p.radiusM = std::max(4.0f, GetF(c, b + "radius_m", 20.0f));
			for (int k = 0; k < 16; ++k)
			{
				const std::string sk = std::format("{}start_for[{}]", b, k);
				if (!c.Has(sk)) break;
				p.startFor.push_back(c.GetString(sk, ""));
			}
			if (!std::isfinite(p.x) || !std::isfinite(p.z)) continue;
			m_cities.push_back(std::move(p));
		}
		const int64_t poiCount = c.GetInt("poi_count", 4096);
		for (int64_t i = 0; i < poiCount; ++i)
		{
			const std::string b = std::format("pois[{}].", i);
			if (!c.Has(b + "x")) break;
			PlacePoi p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.kind = c.GetString(b + "kind", "");
			p.description = c.GetString(b + "description", "");
			p.x = GetF(c, b + "x", 0.0f);
			p.z = GetF(c, b + "z", 0.0f);
			m_pois.push_back(std::move(p));
		}
		const int64_t regionCount = c.GetInt("region_count", 4096);
		for (int64_t i = 0; i < regionCount; ++i)
		{
			const std::string b = std::format("regions[{}].", i);
			if (!c.Has(b + "x_min")) break;
			PlaceRegion r;
			r.id = c.GetString(b + "id", "");
			r.name = c.GetString(b + "name", r.id);
			r.xMin = GetF(c, b + "x_min", 0.0f);
			r.xMax = GetF(c, b + "x_max", 0.0f);
			r.zMin = GetF(c, b + "z_min", 0.0f);
			r.zMax = GetF(c, b + "z_max", 0.0f);
			if (r.xMax < r.xMin) std::swap(r.xMin, r.xMax);
			if (r.zMax < r.zMin) std::swap(r.zMin, r.zMax);
			m_regions.push_back(std::move(r));
		}
		m_loaded = true;
		return true;
	}

	bool WorldPlaces::LoadFromContent(const engine::core::Config& config, std::string_view worldId, std::string& outError)
	{
		const std::string rel = "world/" + std::string(worldId) + "/places.json";
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
		if (text.empty())
		{
			*this = WorldPlaces{};
			outError = "places: fichier absent (" + rel + ")";
			return false;
		}
		return LoadFromText(text, outError);
	}

	bool WorldPlaces::WorldToMapUv(float x, float z, float& outU, float& outV) const
	{
		outU = (x - m_originX) / m_sizeM;
		outV = (z - m_originZ) / m_sizeM;
		return outU >= 0.0f && outU <= 1.0f && outV >= 0.0f && outV <= 1.0f;
	}

	void WorldPlaces::MapUvToWorld(float u, float v, float& outX, float& outZ) const
	{
		outX = m_originX + u * m_sizeM;
		outZ = m_originZ + v * m_sizeM;
	}

	const PlaceCity* WorldPlaces::CityAt(float x, float z, float marginM) const
	{
		const PlaceCity* best = nullptr;
		float bestD2 = (std::numeric_limits<float>::max)();
		for (const PlaceCity& c : m_cities)
		{
			const float dx = x - c.x, dz = z - c.z;
			const float d2 = dx * dx + dz * dz;
			const float r = c.radiusM + marginM;
			if (d2 <= r * r && d2 < bestD2) { bestD2 = d2; best = &c; }
		}
		return best;
	}

	const PlaceRegion* WorldPlaces::RegionAt(float x, float z) const
	{
		const PlaceRegion* best = nullptr;
		for (const PlaceRegion& r : m_regions)
			if (r.Contains(x, z) && (best == nullptr || r.Area() < best->Area())) best = &r;
		return best;
	}

	const PlaceCity* WorldPlaces::StartCityFor(std::string_view race) const
	{
		for (const PlaceCity& c : m_cities)
			for (const std::string& s : c.startFor)
				if (s == race) return &c;
		return nullptr;
	}

	// ------------------------------------------------------------ bannière

	void PlaceBannerTracker::Show(std::string title, std::string subtitle)
	{
		m_title = std::move(title);
		m_subtitle = std::move(subtitle);
		m_timerSec = kDurationSec;
	}

	void PlaceBannerTracker::Update(const WorldPlaces& places, float x, float z, float dtSec)
	{
		if (m_timerSec > 0.0f) m_timerSec = std::max(0.0f, m_timerSec - std::max(0.0f, dtSec));
		if (!places.IsLoaded()) return;

		// Ville : entrée à rayon + 6 m, sortie à rayon + 14 m (hystérésis).
		const PlaceCity* city = places.CityAt(x, z, 6.0f);
		if (city == nullptr && !m_cityId.empty())
		{
			const PlaceCity* still = places.CityAt(x, z, 14.0f);
			if (still != nullptr && still->id == m_cityId) city = still;
		}
		const PlaceRegion* region = places.RegionAt(x, z);
		const std::string cityId = city ? city->id : std::string{};
		const std::string regionId = region ? region->id : std::string{};

		if (!m_initialized)
		{
			m_initialized = true;
			m_cityId = cityId;
			m_regionId = regionId;
			if (city != nullptr)
				Show(city->name, region ? region->name : PlaceKindLabel(city->kind));
			return;
		}
		if (cityId != m_cityId)
		{
			m_cityId = cityId;
			if (city != nullptr)
			{
				std::string sub = PlaceKindLabel(city->kind);
				if (city->state == "ruine") sub += " en ruine";
				if (region != nullptr) sub += " - " + region->name;
				Show(city->name, std::move(sub));
			}
		}
		if (regionId != m_regionId)
		{
			m_regionId = regionId;
			// Une entrée de ville vient d'armer la bannière : elle prime.
			if (region != nullptr && city == nullptr)
				Show(region->name, "Region");
		}
	}

	float PlaceBannerTracker::Alpha() const
	{
		if (m_timerSec <= 0.0f) return 0.0f;
		const float elapsed = kDurationSec - m_timerSec;
		const float fadeIn = std::clamp(elapsed / 0.4f, 0.0f, 1.0f);
		const float fadeOut = std::clamp(m_timerSec / 1.2f, 0.0f, 1.0f);
		return std::min(fadeIn, fadeOut);
	}

	std::string PlaceKindLabel(std::string_view kind)
	{
		if (kind == "capitale") return "Capitale";
		if (kind == "grande_ville") return "Grande ville";
		if (kind == "ville") return "Ville";
		if (kind == "village") return "Village";
		if (kind == "hameau") return "Hameau";
		if (kind == "forteresse") return "Forteresse";
		if (kind == "camp") return "Camp";
		if (kind == "ruine") return "Ruines";
		if (kind == "monolithe") return "Monolithe";
		return std::string(kind);
	}
}
