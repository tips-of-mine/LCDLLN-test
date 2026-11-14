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
			// v1.0.118 — niveaux de référence par faction de jeu (absents = inconnus).
			for (int64_t j = 0; j < 64; ++j)
			{
				const std::string lb = std::format("{}levels[{}].", b, j);
				if (!c.Has(lb + "faction")) break;
				PlaceLevelBand band;
				band.faction = c.GetString(lb + "faction", "");
				band.minLevel = static_cast<int>(c.GetInt(lb + "min", 1));
				band.maxLevel = static_cast<int>(c.GetInt(lb + "max", band.minLevel));
				if (!band.faction.empty() && band.minLevel >= 1 && band.minLevel <= band.maxLevel)
					r.levels.push_back(std::move(band));
			}
			m_regions.push_back(std::move(r));
		}
		// Cellules générées (monde « Royaumes ») : identifiants du lore (« N003_W009 »).
		const int64_t cellCount = c.GetInt("cell_count", 4096);
		for (int64_t i = 0; i < cellCount; ++i)
		{
			const std::string b = std::format("cells[{}].", i);
			if (!c.Has(b + "lore")) break;
			m_generatedCells.push_back(c.GetString(b + "lore", ""));
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

	const PlaceRegion* WorldPlaces::RegionById(std::string_view id) const
	{
		if (id.empty()) return nullptr;
		for (const PlaceRegion& r : m_regions)
			if (r.id == id) return &r;
		return nullptr;
	}

	const PlaceLevelBand* PlaceRegion::LevelsFor(std::string_view faction) const
	{
		for (const PlaceLevelBand& b : levels)
			if (b.faction == faction) return &b;
		return nullptr;
	}

	ZoneLevelTone ZoneLevelToneFor(int playerLevel, int minLevel, int maxLevel)
	{
		constexpr int kEcart = 5; // seuil de la règle des niveaux par faction (LORE_BD §16.32)
		if (minLevel < 1 || maxLevel < minLevel || playerLevel < 1) return ZoneLevelTone::Inconnu;
		if (playerLevel > maxLevel + kEcart) return ZoneLevelTone::Gris;
		if (playerLevel > maxLevel) return ZoneLevelTone::Vert;
		if (playerLevel >= minLevel) return ZoneLevelTone::Jaune;
		if (playerLevel >= minLevel - kEcart) return ZoneLevelTone::Orange;
		return ZoneLevelTone::Rouge;
	}

	ZoneLevelTone ZoneLevelToneFor(const PlaceRegion* region, std::string_view faction, int playerLevel)
	{
		if (region == nullptr) return ZoneLevelTone::Inconnu;
		const PlaceLevelBand* b = region->LevelsFor(faction);
		return b ? ZoneLevelToneFor(playerLevel, b->minLevel, b->maxLevel) : ZoneLevelTone::Inconnu;
	}

	void ZoneLevelToneRgb(ZoneLevelTone tone, uint8_t& r, uint8_t& g, uint8_t& b)
	{
		switch (tone)
		{
		case ZoneLevelTone::Gris:   r = 168; g = 168; b = 168; return;
		case ZoneLevelTone::Vert:   r = 120; g = 214; b = 110; return;
		case ZoneLevelTone::Jaune:  r = 250; g = 222; b = 92;  return;
		case ZoneLevelTone::Orange: r = 245; g = 150; b = 60;  return;
		case ZoneLevelTone::Rouge:  r = 232; g = 72;  b = 60;  return;
		case ZoneLevelTone::Inconnu:
		default:                    r = 246; g = 226; b = 172; return; // or pâle historique
		}
	}

	const PlaceCity* WorldPlaces::StartCityFor(std::string_view faction) const
	{
		for (const PlaceCity& c : m_cities)
			for (const std::string& s : c.startFor)
				if (s == faction) return &c;
		return nullptr;
	}

	// ------------------------------------------------------------ bannière

	void PlaceBannerTracker::Show(std::string title, std::string subtitle, std::string regionId)
	{
		m_title = std::move(title);
		m_subtitle = std::move(subtitle);
		m_shownRegionId = std::move(regionId);
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
				Show(city->name, region ? region->name : PlaceKindLabel(city->kind), regionId);
			return;
		}
		if (cityId != m_cityId)
		{
			m_cityId = cityId;
			if (city != nullptr)
			{
				std::string sub = PlaceKindLabel(city->kind);
				if (city->state == "ruine") sub += " en ruine";
				else if (city->state == "reconstruction") sub += " en reconstruction";
				if (region != nullptr) sub += " - " + region->name;
				Show(city->name, std::move(sub), regionId);
			}
		}
		if (regionId != m_regionId)
		{
			m_regionId = regionId;
			// Une entrée de ville vient d'armer la bannière : elle prime.
			if (region != nullptr && city == nullptr)
				Show(region->name, "Region", regionId);
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
		// Genres de POI (section `pois` du master). Accents en octets UTF-8
		// explicites : le source ne dépend pas du jeu de caractères du compilateur.
		if (kind == "faille") return "Faille";
		if (kind == "portail") return "Portail";
		if (kind == "foret") return "For\xC3\xAAt"; // « Forêt » en UTF-8
		if (kind == "etang") return "\xC3\x89tang"; // « Étang » en UTF-8
		if (kind == "champ_bataille") return "Champ de bataille";
		if (kind == "volcan") return "Volcan";
		if (kind == "gouffre") return "Gouffre";
		// Lieux des tomes 7 à 14 (lore v1.0.55).
		if (kind == "temple") return "Temple";
		if (kind == "sanctuaire") return "Sanctuaire";
		if (kind == "port") return "Port";
		if (kind == "cite") return "Cit\xC3\xA9"; // « Cité » en UTF-8
		if (kind == "defile") return "D\xC3\xA9" "fil\xC3\xA9"; // « Défilé » en UTF-8
		if (kind == "salle") return "Salle souterraine";
		// Lieux des albums hors série (lore v1.0.59).
		if (kind == "elevage") return "\xC3\x89levage"; // « Élevage » en UTF-8
		return std::string(kind);
	}
}
