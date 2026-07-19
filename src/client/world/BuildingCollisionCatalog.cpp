#include "src/client/world/BuildingCollisionCatalog.h"

#include <cctype>

namespace engine::world
{
	namespace
	{
		/// Convertit une string_view en minuscules ASCII (insensibilité à la casse des basenames).
		std::string ToLowerAscii(std::string_view s)
		{
			std::string o; o.reserve(s.size());
			for (char ch : s) o.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch);
			return o;
		}
	}

	bool BuildingCollisionCatalog::LoadFromJson(const std::string& jsonText)
	{
		m_cache.clear();
		// LoadFromString renvoie false si le texte n'est pas un JSON valide dont
		// la racine est un objet — ce qui garantit que le test « JSON invalide »
		// reçoit bien false sans traitement supplémentaire.
		m_loaded = m_cfg.LoadFromString(jsonText);
		return m_loaded;
	}

	const BuildingCollisionCatalog::Piece* BuildingCollisionCatalog::Lookup(std::string_view meshBaseName) const
	{
		if (!m_loaded) return nullptr;
		const std::string key = ToLowerAscii(meshBaseName);

		// Recherche dans le cache (ne contient QUE les pièces trouvées au catalogue ;
		// les meshes absents ne sont jamais mis en cache pour ne pas mélanger
		// « absent » et « passable sans boîte »).
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return &it->second;

		const std::string base = "pieces." + key + ".";
		const bool passable = m_cfg.GetBool(base + "passable", false);
		// GetInt renvoie int64_t ; on cast en int pour l'itération.
		const int count = static_cast<int>(m_cfg.GetInt(base + "box_count", -1));

		// Si ni passable ni box_count présent, la pièce est absente du catalogue.
		if (!passable && count < 0)
			return nullptr; // l'appelant retombe sur la collision cylindre par défaut

		Piece p;
		p.passable = passable;
		// Roadmap-5 (2026-07-19) — dessus marchable (sols, plateformes).
		p.walkable = m_cfg.GetBool(base + "walkable", false);
		for (int i = 0; i < count; ++i)
		{
			const std::string b = base + "box_" + std::to_string(i) + ".";
			LocalBox lb{};
			lb.cx = static_cast<float>(m_cfg.GetDouble(b + "cx", 0.0));
			lb.cy = static_cast<float>(m_cfg.GetDouble(b + "cy", 0.0));
			lb.cz = static_cast<float>(m_cfg.GetDouble(b + "cz", 0.0));
			lb.hx = static_cast<float>(m_cfg.GetDouble(b + "hx", 0.0));
			lb.hy = static_cast<float>(m_cfg.GetDouble(b + "hy", 0.0));
			lb.hz = static_cast<float>(m_cfg.GetDouble(b + "hz", 0.0));
			if (lb.hx > 0.0f && lb.hy > 0.0f && lb.hz > 0.0f)
				p.boxes.push_back(lb);
		}
		auto [ins, ok] = m_cache.emplace(key, std::move(p));
		return &ins->second;
	}
}
