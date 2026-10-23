#pragma once

// tools/world_gen — catalogue des maisons PARAMÉTRIQUES générées dans
// `buildings/templates/maison.json` (type « maison »).
//
// Les pièces du kit modulaire (meshes/props/Wall_*, Floor_*, Roof_*,
// Corner_*) suivent une grille de 2 m et des étages de 3 m. Les conventions
// (positions au centre des segments de 2 m, rotations des façades, pignons,
// toiture centrée à y = étages × 3) sont celles des modèles faits à la main
// (house.json / tavern.json, ex. « auberge_hameau ») — vérifiées sur leurs
// pièces avant d'être généralisées ici.
//
// Convention d'orientation : la façade avec la PORTE est à z = -D/2 (local),
// donc l'avant du bâtiment regarde vers -Z local. Un placement de yaw θ
// (rotation +Y standard) fait regarder l'avant vers (-sin θ, -cos θ).

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace tools::world_gen
{
	/// Style de maçonnerie d'une maison paramétrique.
	enum class WallStyle
	{
		Plaster,  ///< enduit + colombages (village humain)
		Brick,    ///< briques irrégulières (forteresses, nains)
		Mixed,    ///< rez-de-chaussée brique, étages enduits (villes)
	};

	struct HouseSpec
	{
		const char* id;
		const char* displayName;
		int widthM;    ///< X (multiple de 2 ; 4, 6 ou 8 : tailles de toits du kit)
		int depthM;    ///< Z (multiple de 2)
		int floors;    ///< étages (3 m chacun)
		WallStyle style;
		bool ruin;     ///< ruine : pas de toit, pans de murs manquants, 1 niveau
	};

	/// Catalogue complet (ordre stable = ordre des variantes dans maison.json).
	inline const std::vector<HouseSpec>& HouseCatalog()
	{
		static const std::vector<HouseSpec> k = {
			{ "chaumiere_4x4",     "Chaumiere (4x4 m, 1 niveau)",            4,  4, 1, WallStyle::Plaster, false },
			{ "maison_4x6",        "Maison de bourg (4x6 m, 2 niveaux)",     4,  6, 2, WallStyle::Plaster, false },
			{ "echoppe_4x8",       "Echoppe (4x8 m, 1 niveau)",              4,  8, 1, WallStyle::Mixed,   false },
			{ "maison_6x6",        "Maison de pierre (6x6 m, 2 niveaux)",    6,  6, 2, WallStyle::Mixed,   false },
			{ "maison_haute_6x8",  "Maison haute (6x8 m, 3 niveaux)",        6,  8, 3, WallStyle::Mixed,   false },
			{ "longere_6x10",      "Longere (6x10 m, 1 niveau)",             6, 10, 1, WallStyle::Plaster, false },
			{ "grande_maison_8x10","Grande maison (8x10 m, 2 niveaux)",      8, 10, 2, WallStyle::Mixed,   false },
			{ "manoir_8x12",       "Manoir (8x12 m, 3 niveaux)",             8, 12, 3, WallStyle::Brick,   false },
			{ "caserne_8x14",      "Caserne (8x14 m, 2 niveaux)",            8, 14, 2, WallStyle::Brick,   false },
			{ "forge_6x6",         "Forge (6x6 m, 1 niveau)",                6,  6, 1, WallStyle::Brick,   false },
			{ "ruine_6x6",         "Ruine (6x6 m)",                          6,  6, 1, WallStyle::Brick,   true  },
			{ "ruine_6x10",        "Ruine (6x10 m)",                         6, 10, 1, WallStyle::Plaster, true  },
			{ "ruine_8x10",        "Ruine (8x10 m)",                         8, 10, 1, WallStyle::Mixed,   true  },
		};
		return k;
	}

	/// Rayon d'encombrement au sol (m), perron compris, marge de 0,8 m.
	inline double HouseFootprintRadius(const HouseSpec& s)
	{
		const double hx = s.widthM * 0.5, hz = s.depthM * 0.5 + 1.6;
		return std::sqrt(hx * hx + hz * hz) + 0.8;
	}

	inline const HouseSpec* FindHouse(const std::string& id)
	{
		for (const HouseSpec& s : HouseCatalog())
			if (id == s.id) return &s;
		return nullptr;
	}
}
