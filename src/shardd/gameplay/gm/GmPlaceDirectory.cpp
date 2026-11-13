// ADM.1 — Annuaire des lieux du monde : voir GmPlaceDirectory.h.

#include "src/shardd/gameplay/gm/GmPlaceDirectory.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdint>
#include <format>

namespace engine::server::gm
{
	std::string FoldPlaceText(std::string_view text)
	{
		std::string out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if (c < 0x80u)
			{
				if (c >= 'A' && c <= 'Z')
					out.push_back(static_cast<char>(c - 'A' + 'a'));
				else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
					out.push_back(static_cast<char>(c));
				// espaces, apostrophes, tirets, ponctuation : ignorés
				continue;
			}
			// Lettres accentuées latines sur deux octets (U+00C0..U+00FF : C3 80..C3 BF).
			if (c == 0xC3u && i + 1 < text.size())
			{
				const unsigned char d = static_cast<unsigned char>(text[i + 1]) | 0x20u; // majuscule → minuscule
				++i;
				char base = 0;
				if (d >= 0xA0u && d <= 0xA5u) base = 'a';
				else if (d == 0xA7u) base = 'c';
				else if (d >= 0xA8u && d <= 0xABu) base = 'e';
				else if (d >= 0xACu && d <= 0xAFu) base = 'i';
				else if (d == 0xB1u) base = 'n';
				else if (d >= 0xB2u && d <= 0xB6u) base = 'o';
				else if (d >= 0xB9u && d <= 0xBCu) base = 'u';
				else if (d == 0xBDu || d == 0xBFu) base = 'y';
				if (base != 0)
					out.push_back(base);
				continue;
			}
			// Autres caractères non ASCII (œ, ’…) : ignorés (octets de tête et de suite).
		}
		return out;
	}

	namespace
	{
		float GetF(const engine::core::Config& c, const std::string& key, float def)
		{
			return static_cast<float>(c.GetDouble(key, def));
		}

		/// Score de correspondance (plus petit = meilleur), -1 = aucune.
		int MatchScore(const GmPlace& p, const std::string& foldedQuery)
		{
			const std::string name = FoldPlaceText(p.name);
			const std::string id = FoldPlaceText(p.id);
			if (name == foldedQuery)
				return 0;
			if (id == foldedQuery)
				return 1;
			if (name.rfind(foldedQuery, 0) == 0)
				return 2;
			if (name.find(foldedQuery) != std::string::npos)
				return 3;
			if (id.find(foldedQuery) != std::string::npos)
				return 4;
			return -1;
		}
	}

	bool GmPlaceDirectory::LoadFromText(std::string_view json, std::string& outError)
	{
		std::vector<GmPlace> keptStarts;
		for (const GmPlace& p : m_places)
		{
			if (p.kind == GmPlaceKind::Start)
				keptStarts.push_back(p);
		}
		m_places = std::move(keptStarts);
		return AppendFromText(json, outError);
	}

	bool GmPlaceDirectory::AppendFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json))
		{
			outError = "places: JSON invalide";
			return false;
		}
		// Carte des lieux : champ "world" ("enfers" → Enfers, sinon Royaumes).
		const engine::world::cells::CellMap map =
			engine::world::cells::CellMapFromWorldId(c.GetString("world", "")).value_or(engine::world::cells::CellMap::Royaumes);
		// v1.0.110 : fichier des lieux secrets (secret_places.json, "secret": true).
		const bool secret = c.GetBool("secret", false);
		const size_t before = m_places.size();
		for (int64_t i = 0; i < c.GetInt("city_count", 4096); ++i)
		{
			const std::string b = std::format("cities[{}].", i);
			if (!c.Has(b + "x"))
				break;
			GmPlace p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.detail = c.GetString(b + "kind", "ville");
			p.kind = GmPlaceKind::City;
			p.map = map;
			p.x = GetF(c, b + "x", 0.0f);
			p.z = GetF(c, b + "z", 0.0f);
			if (std::isfinite(p.x) && std::isfinite(p.z) && !p.name.empty())
				m_places.push_back(std::move(p));
		}
		for (int64_t i = 0; i < c.GetInt("poi_count", 4096); ++i)
		{
			const std::string b = std::format("pois[{}].", i);
			if (!c.Has(b + "x"))
				break;
			GmPlace p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.detail = c.GetString(b + "kind", "lieu");
			p.kind = GmPlaceKind::Poi;
			p.map = map;
			p.x = GetF(c, b + "x", 0.0f);
			p.z = GetF(c, b + "z", 0.0f);
			if (std::isfinite(p.x) && std::isfinite(p.z) && !p.name.empty())
				m_places.push_back(std::move(p));
		}
		for (int64_t i = 0; i < c.GetInt("region_count", 4096); ++i)
		{
			const std::string b = std::format("regions[{}].", i);
			if (!c.Has(b + "x_min"))
				break;
			GmPlace p;
			p.id = c.GetString(b + "id", "");
			p.name = c.GetString(b + "name", p.id);
			p.detail = "région";
			p.kind = GmPlaceKind::Region;
			p.map = map;
			p.xMin = GetF(c, b + "x_min", 0.0f);
			p.xMax = GetF(c, b + "x_max", 0.0f);
			p.zMin = GetF(c, b + "z_min", 0.0f);
			p.zMax = GetF(c, b + "z_max", 0.0f);
			if (p.xMax < p.xMin) std::swap(p.xMin, p.xMax);
			if (p.zMax < p.zMin) std::swap(p.zMin, p.zMax);
			p.hasBounds = true;
			p.x = (p.xMin + p.xMax) * 0.5f;
			p.z = (p.zMin + p.zMax) * 0.5f;
			if (std::isfinite(p.x) && std::isfinite(p.z) && !p.name.empty())
				m_places.push_back(std::move(p));
		}
		if (m_places.size() == before)
		{
			outError = "places: aucun lieu";
			return false;
		}
		if (secret)
		{
			for (size_t i = before; i < m_places.size(); ++i)
			{
				m_places[i].secret = true;
				m_places[i].detail = "secret · " + m_places[i].detail;
			}
		}
		return true;
	}

	void GmPlaceDirectory::AppendSecretFromContent(const engine::core::Config& config, std::string_view worldId)
	{
		const std::string rel = "world/" + std::string(worldId) + "/secret_places.json";
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
		if (text.empty())
			return;
		std::string ignored;
		(void)AppendFromText(text, ignored);
	}

	bool GmPlaceDirectory::LoadFromContent(const engine::core::Config& config, std::string_view worldId, std::string& outError)
	{
		const std::string rel = "world/" + std::string(worldId) + "/places.json";
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
		if (text.empty())
		{
			outError = rel + " absent";
			return false;
		}
		if (!LoadFromText(text, outError))
			return false;
		AppendSecretFromContent(config, worldId);
		return true;
	}

	bool GmPlaceDirectory::AppendFromContent(const engine::core::Config& config, std::string_view worldId, std::string& outError)
	{
		const std::string rel = "world/" + std::string(worldId) + "/places.json";
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
		if (text.empty())
		{
			outError = rel + " absent";
			return false;
		}
		if (!AppendFromText(text, outError))
			return false;
		AppendSecretFromContent(config, worldId);
		return true;
	}

	std::vector<const GmPlace*> GmPlaceDirectory::Search(std::string_view query, size_t maxResults) const
	{
		std::vector<const GmPlace*> out;
		const std::string folded = FoldPlaceText(query);
		if (folded.empty())
			return out;
		std::vector<std::pair<int, const GmPlace*>> scored;
		for (const GmPlace& p : m_places)
		{
			const int s = MatchScore(p, folded);
			if (s >= 0)
				scored.emplace_back(s * 8 + static_cast<int>(p.kind), &p);
		}
		std::stable_sort(scored.begin(), scored.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [score, p] : scored)
		{
			if (out.size() >= maxResults)
				break;
			out.push_back(p);
		}
		return out;
	}

	std::vector<const GmPlace*> GmPlaceDirectory::List(std::string_view filter) const
	{
		std::vector<const GmPlace*> out;
		const std::string folded = FoldPlaceText(filter);
		for (const GmPlace& p : m_places)
		{
			if (folded.empty() || FoldPlaceText(p.name).find(folded) != std::string::npos)
				out.push_back(&p);
		}
		std::sort(out.begin(), out.end(), [](const GmPlace* a, const GmPlace* b) {
			return FoldPlaceText(a->name) < FoldPlaceText(b->name);
		});
		return out;
	}

	const GmPlace* GmPlaceDirectory::RegionAt(float x, float z, engine::world::cells::CellMap map) const
	{
		const GmPlace* best = nullptr;
		float bestArea = 0.0f;
		for (const GmPlace& p : m_places)
		{
			if (p.map != map || !p.hasBounds || x < p.xMin || x > p.xMax || z < p.zMin || z > p.zMax)
				continue;
			const float area = (p.xMax - p.xMin) * (p.zMax - p.zMin);
			if (best == nullptr || area < bestArea)
			{
				best = &p;
				bestArea = area;
			}
		}
		return best;
	}

	const GmPlace* GmPlaceDirectory::Nearest(float x, float z, float& outDistance, engine::world::cells::CellMap map) const
	{
		const GmPlace* best = nullptr;
		float bestSq = 0.0f;
		for (const GmPlace& p : m_places)
		{
			if (p.kind == GmPlaceKind::Region || p.map != map)
				continue;
			const float dx = p.x - x;
			const float dz = p.z - z;
			const float sq = dx * dx + dz * dz;
			if (best == nullptr || sq < bestSq)
			{
				best = &p;
				bestSq = sq;
			}
		}
		outDistance = best != nullptr ? std::sqrt(bestSq) : 0.0f;
		return best;
	}
}
