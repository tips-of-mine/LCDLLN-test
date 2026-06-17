#include "src/world_editor/assets/AssetCatalog.h"

#include "src/shared/core/Config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace engine::editor::world::assets
{
	const AssetCatalogEntry* AssetCatalog::FindById(const std::string& id) const
	{
		auto it = std::find_if(m_entries.begin(), m_entries.end(),
			[&id](const AssetCatalogEntry& e) { return e.id == id; });
		return (it == m_entries.end()) ? nullptr : &*it;
	}

	std::vector<const AssetCatalogEntry*> AssetCatalog::ByCategory(
		const std::string& category) const
	{
		std::vector<const AssetCatalogEntry*> out;
		for (const auto& e : m_entries)
			if (e.category == category) out.push_back(&e);
		return out;
	}

	std::vector<std::string> AssetCatalog::Categories() const
	{
		std::vector<std::string> out;
		for (const auto& e : m_entries)
		{
			if (std::find(out.begin(), out.end(), e.category) == out.end())
				out.push_back(e.category);
		}
		return out;
	}

	bool AssetCatalog::ParseJson(const std::string& jsonText, std::string& outError)
	{
		m_entries.clear();
		engine::core::Config c;
		if (!c.LoadFromString(jsonText))
		{
			outError = "AssetCatalog: JSON invalide";
			return false;
		}
		const int n = static_cast<int>(c.GetInt("assets.count", 0));
		m_entries.reserve(static_cast<size_t>(n > 0 ? n : 0));
		for (int i = 0; i < n; ++i)
		{
			const std::string base = "assets." + std::to_string(i) + ".";
			AssetCatalogEntry e;
			e.id              = c.GetString(base + "id", "");
			e.gltfRelativePath = c.GetString(base + "gltf", "");
			if (e.id.empty() || e.gltfRelativePath.empty()) continue;
			e.category      = c.GetString(base + "category", "Misc");
			e.displayName   = c.GetString(base + "displayName", e.id);
			e.thumbnailPath = c.GetString(base + "thumbnail", "");
			m_entries.push_back(std::move(e));
		}
		return true;
	}

	bool AssetCatalog::LoadFromContent(const std::string& contentRoot, std::string& outError)
	{
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "meshes" / "props" / "catalog.json";
		std::ifstream f(path, std::ios::binary);
		if (!f.good())
		{
			// Fichier absent = catalogue vide, pas une erreur.
			m_entries.clear();
			return true;
		}
		std::string json((std::istreambuf_iterator<char>(f)),
			std::istreambuf_iterator<char>());
		return ParseJson(json, outError);
	}
}
