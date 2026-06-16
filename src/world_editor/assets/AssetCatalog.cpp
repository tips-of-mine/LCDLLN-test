// Auberge éditable (T1) — Implémentation du catalogue d'assets props.
// Logique pure : std::filesystem uniquement, pas de dépendance moteur ni ImGui.

#include "src/world_editor/assets/AssetCatalog.h"

#include <algorithm>
#include <filesystem>

namespace engine::editor::world::assets
{
	namespace
	{
		/// Extrait le préfixe sémantique d'un nom de fichier :
		/// tout ce qui précède le premier '_', ou le nom sans extension s'il n'y a pas de '_'.
		/// \param fileName  Nom de fichier court (avec extension).
		/// \return Préfixe en majuscule initiale (ex. "Wall", "Barrel").
		std::string Prefix(const std::string& fileName)
		{
			const size_t us = fileName.find('_');
			if (us != std::string::npos) return fileName.substr(0, us);
			const size_t dot = fileName.find('.');
			return fileName.substr(0, dot == std::string::npos ? fileName.size() : dot);
		}
	}

	AssetCategory CategorizeAsset(const std::string& fileName)
	{
		const std::string p = Prefix(fileName);
		if (p == "Wall")        return AssetCategory::Wall;
		if (p == "Door" || p == "DoorFrame") return AssetCategory::Door;
		if (p == "Window" || p == "WindowShutters") return AssetCategory::Window;
		if (p == "Roof")        return AssetCategory::Roof;
		if (p == "Floor")       return AssetCategory::Floor;
		if (p == "Corner")      return AssetCategory::Corner;
		if (p == "Overhang")    return AssetCategory::Overhang;
		if (p == "Balcony")     return AssetCategory::Balcony;
		if (p == "Stairs" || p == "Stair") return AssetCategory::Stairs;
		if (p == "Table" || p == "Chair" || p == "Bench" || p == "Stool" ||
		    p == "Bed" || p == "Shelf" || p == "Bookcase" || p == "Cabinet" ||
		    p == "Nightstand" || p == "Workbench")
			return AssetCategory::Furniture;
		if (p == "Lantern" || p == "Torch" || p == "Chandelier" ||
		    p == "CandleStick")
			return AssetCategory::Lighting;
		if (p == "Barrel" || p == "Crate" || p == "FarmCrate" || p == "Chest" ||
		    p == "Bucket" || p == "Pouch")
			return AssetCategory::Container;
		if (p == "Banner" || p == "Vase" || p == "Pot" || p == "Mug" ||
		    p == "Bottle" || p == "SmallBottle" || p == "SmallBottles" ||
		    p == "Chalice" || p == "Coin" || p == "Cauldron" || p == "Key")
			return AssetCategory::Decoration;
		return AssetCategory::Unknown;
	}

	const char* CategoryLabel(AssetCategory c)
	{
		switch (c)
		{
			case AssetCategory::Wall:       return "Murs";
			case AssetCategory::Door:       return "Portes";
			case AssetCategory::Window:     return "Fenetres";
			case AssetCategory::Roof:       return "Toits";
			case AssetCategory::Floor:      return "Planchers";
			case AssetCategory::Corner:     return "Coins";
			case AssetCategory::Overhang:   return "Surplombs";
			case AssetCategory::Balcony:    return "Balcons";
			case AssetCategory::Stairs:     return "Escaliers";
			case AssetCategory::Furniture:  return "Mobilier";
			case AssetCategory::Lighting:   return "Eclairage";
			case AssetCategory::Container:  return "Conteneurs";
			case AssetCategory::Decoration: return "Decoration";
			case AssetCategory::Unknown:    return "Autres";
		}
		return "Autres";
	}

	std::vector<AssetEntry> ScanPropAssets(const std::string& absoluteDir,
		const std::string& relativePrefix)
	{
		namespace fs = std::filesystem;
		std::vector<AssetEntry> out;
		std::error_code ec;
		if (!fs::is_directory(absoluteDir, ec)) return out;
		for (const auto& e : fs::directory_iterator(absoluteDir, ec))
		{
			if (ec) break;
			if (!e.is_regular_file()) continue;
			const std::string name = e.path().filename().string();
			if (e.path().extension() != ".gltf") continue;
			AssetEntry a;
			a.fileName = name;
			a.relativePath = relativePrefix + name;
			a.category = CategorizeAsset(name);
			out.push_back(std::move(a));
		}
		std::sort(out.begin(), out.end(), [](const AssetEntry& x, const AssetEntry& y)
		{
			if (x.category != y.category)
				return static_cast<int>(x.category) < static_cast<int>(y.category);
			return x.fileName < y.fileName;
		});
		return out;
	}
}
