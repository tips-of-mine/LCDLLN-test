#pragma once

// Règles de matériaux des props statiques (kit « Medieval Village » + nature).
//
// Extrait d'Engine::BuildPropFromMeshMatrix (chantier « Royaumes ») pour être
// partagé, À L'IDENTIQUE, entre le rendu historique par prop et le rendu
// instancié du monde (ScatterField) — et testé en CI. Header-only, sans Vulkan.
//
// Les pièces de structure (murs, sols, toits…) du projet n'embarquent aucune
// texture dans leur glTF mais gardent le nom du matériau d'origine (MI_Plaster,
// MI_WoodTrim, MI_UnevenBrick…). On retrouve alors la texture du kit, copiée à
// côté des meshes (meshes/props/T_*.png), par ce nom.

#include <cstdint>
#include <string_view>

namespace engine::world::scatter
{
	/// Jeu de textures du kit (noms de fichiers relatifs au dossier du mesh).
	/// `normal` / `orm` peuvent être nullptr (canal absent du kit).
	struct TrimTextureSet
	{
		const char* baseColor = nullptr;
		const char* normal = nullptr;
		const char* orm = nullptr;
	};

	/// Résout les textures du kit pour un matériau SANS texture glTF.
	/// Priorité au NOM DU MATÉRIAU (un mur « Wall_Plaster_WoodGrid » garde son
	/// bois) ; le chemin du mesh ne sert qu'à affiner la brique générique.
	/// \return false si aucune règle ne s'applique (repli : couleur plate).
	inline bool ResolveTrimTextures(std::string_view matName, std::string_view meshPath, TrimTextureSet& out)
	{
		auto matHas = [&](const char* s) { return matName.find(s) != std::string_view::npos; };
		auto pathHas = [&](const char* s) { return meshPath.find(s) != std::string_view::npos; };
		out = TrimTextureSet{};
		if (matHas("Plaster"))          out = { "T_Plaster_BaseColor.png", "T_Plaster_Normal.png", "T_Plaster_ORM.png" };
		else if (matHas("Wood"))        out = { "T_WoodTrim_BaseColor.png", "T_WoodTrim_Normal.png", "T_WoodTrim_ORM.png" };
		else if (matHas("UnevenBrick")) out = { "T_UnevenBrick_BaseColor.png", "T_UnevenBrick_Normal.png", nullptr };
		else if (matHas("RedBrick"))    out = { "T_RedBrick_BaseColor.png", "T_Brick_Normal.png", nullptr };
		else if (matHas("Rock"))        out = { "T_RockTrim_BaseColor.png", "T_RockTrim_Normal.png", "T_RockTrim_ORM.png" };
		else if (matHas("RoundTile"))   out = { "T_RoundTiles_BaseColor.png", "T_RoundTiles_Normal.png", nullptr };
		else if (matHas("Metal"))       out = { "T_Trim_Metal_BaseColor.png", "T_Trim_Metal_Normal.png", "T_Trim_Metal_ORM.png" };
		else if (matHas("Brick"))
		{
			if (pathHas("RedBrick"))         out = { "T_RedBrick_BaseColor.png", "T_Brick_Normal.png", nullptr };
			else if (pathHas("UnevenBrick")) out = { "T_UnevenBrick_BaseColor.png", "T_UnevenBrick_Normal.png", nullptr };
			else                             out = { "T_Brick_BaseColor.png", "T_Brick_Normal.png", nullptr };
		}
		else if (pathHas("RoundTiles"))  out = { "T_RoundTiles_BaseColor.png", "T_RoundTiles_Normal.png", nullptr };
		return out.baseColor != nullptr;
	}

	/// Couleur plate de repli (texture du kit introuvable) : teinte par famille
	/// de matériau, plutôt qu'un blanc qui « crie » à l'écran.
	inline void FallbackFlatColor(std::string_view matName, std::string_view meshPath,
		uint8_t& r, uint8_t& g, uint8_t& b)
	{
		auto matHas = [&](const char* s) { return matName.find(s) != std::string_view::npos; };
		auto pathHas = [&](const char* s) { return meshPath.find(s) != std::string_view::npos; };
		r = 200; g = 195; b = 185;
		if (matHas("Plaster"))                                 { r = 232; g = 224; b = 203; }
		else if (matHas("RedBrick") || pathHas("RedBrick"))    { r = 165; g =  86; b =  72; }
		else if (matHas("Brick") || pathHas("Brick"))          { r = 170; g = 120; b =  96; }
		else if (pathHas("WoodDark"))                          { r = 102; g =  72; b =  48; }
		else if (pathHas("WoodLight"))                         { r = 170; g = 130; b =  88; }
		else if (matHas("Wood"))                               { r = 140; g = 100; b =  62; }
		else if (matHas("Metal"))                              { r = 120; g = 122; b = 128; }
		else if (matHas("Glass") || matHas("Window"))          { r = 205; g = 222; b = 232; }
	}

	/// Politique du rendu instancié : un matériau sans texture glTF dont le nom
	/// suit la convention du kit (préfixe « MI_ ») reçoit les textures du kit ;
	/// tout autre matériau sans texture (herbes, dalles…) prend la couleur de
	/// sommet (COLOR_0), comme le décor « nature » historique.
	inline bool UsesKitTextures(std::string_view matName)
	{
		return matName.rfind("MI_", 0) == 0;
	}
}
