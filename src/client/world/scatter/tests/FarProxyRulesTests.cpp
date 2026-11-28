// v1.0.145 — Silhouettes lointaines du bâti (FarProxyRules.h) et grille à pas variable du
// champ de décor (ScatterField) : classement des pièces, familles de teinte, chemins des
// meshes unitaires (présents dans game/data/meshes/proxies), règle d'affichage, Gather d'un
// champ à grille large.

#include "src/client/world/scatter/FarProxyRules.h"
#include "src/client/world/scatter/ScatterField.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace sc = engine::world::scatter;

namespace
{
	int g_fail = 0;
	void Check(bool cond, const char* msg)
	{
		if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; }
	}
}

int main()
{
	// Classement : murs, toitures, toits de tour ; mobilier, sols et ouvertures ignorés.
	Check(sc::ClassifyFarProxy("meshes/props/Wall_Plaster_Straight.gltf") == sc::FarProxyShape::Block, "mur -> bloc");
	Check(sc::ClassifyFarProxy("meshes/props/Corner_Exterior_Wood.gltf") == sc::FarProxyShape::Block, "angle -> bloc");
	Check(sc::ClassifyFarProxy("meshes/props/Rampart_Wood_1.gltf") == sc::FarProxyShape::Block, "rempart -> bloc");
	Check(sc::ClassifyFarProxy("meshes/props/Roof_RoundTiles_6x8.gltf") == sc::FarProxyShape::Gable, "toiture -> toit");
	Check(sc::ClassifyFarProxy("meshes/props/Roof_Tower_RoundTiles.gltf") == sc::FarProxyShape::Spire, "toit de tour -> fleche");
	Check(sc::ClassifyFarProxy("meshes/props/Roof_Front_Brick6.gltf") == sc::FarProxyShape::None, "pignon ignore");
	Check(sc::ClassifyFarProxy("meshes/props/Floor_WoodDark.gltf") == sc::FarProxyShape::None, "sol ignore");
	Check(sc::ClassifyFarProxy("meshes/props/Lantern_Wall.gltf") == sc::FarProxyShape::None, "lanterne murale ignoree");
	Check(sc::ClassifyFarProxy("meshes/props/Barrel.gltf") == sc::FarProxyShape::None, "mobilier ignore");
	Check(sc::ClassifyFarProxy("meshes/props/Wall_Plaster_Door_Round.gltf") == sc::FarProxyShape::None, "porte ignoree");

	// Familles de teinte.
	Check(std::string(sc::FarProxyFamily("Wall_Plaster_WoodGrid", sc::FarProxyShape::Block)) == "Plaster", "plâtre");
	Check(std::string(sc::FarProxyFamily("Wall_RedBrick_Straight", sc::FarProxyShape::Block)) == "RedBrick", "brique rouge");
	Check(std::string(sc::FarProxyFamily("Wall_UnevenBrick_Straight", sc::FarProxyShape::Block)) == "Brick", "brique");
	Check(std::string(sc::FarProxyFamily("Wall_Plank_Straight", sc::FarProxyShape::Block)) == "Wood", "planches");
	Check(std::string(sc::FarProxyFamily("Wall_DryStone_Straight", sc::FarProxyShape::Block)) == "Stone", "pierre");
	Check(std::string(sc::FarProxyFamily("Roof_RoundTiles_6x8", sc::FarProxyShape::Gable)) == "Tiles", "tuiles");
	Check(std::string(sc::FarProxyFamily("Roof_Shingle_6x8", sc::FarProxyShape::Gable)) == "Slate", "bardeaux");
	Check(std::string(sc::FarProxyFamily("Roof_Wooden_2x1", sc::FarProxyShape::Gable)) == "Wood", "toit de bois");

	// Chaque silhouette possible existe sur disque (build_far_proxies.py).
	for (sc::FarProxyShape shape : { sc::FarProxyShape::Block, sc::FarProxyShape::Gable, sc::FarProxyShape::Spire })
	{
		const std::vector<const char*> fams = shape == sc::FarProxyShape::Block
			? std::vector<const char*>{ "Plaster", "Stone", "Brick", "RedBrick", "Wood" }
			: std::vector<const char*>{ "Tiles", "Slate", "Wood", "Thatch" };
		for (const char* f : fams)
		{
			const std::string rel = sc::FarProxyMeshPath(shape, f);
			Check(std::filesystem::exists("game/data/" + rel), ("mesh de silhouette present : " + rel).c_str());
		}
	}
	Check(sc::FarProxyMeshPath(sc::FarProxyShape::None, "Stone").empty(), "aucun mesh pour None");

	// Règle d'affichage : cachée tant que tout le bâtiment est dans la distance du bâti,
	// visible dès qu'une partie la dépasse, coupée au-delà du plan lointain.
	Check(!sc::FarProxyVisible(100.0f, 8.0f, 150.0f, 900.0f), "proche : silhouette cachee");
	Check(sc::FarProxyVisible(145.0f, 8.0f, 150.0f, 900.0f), "bande de recouvrement : visible");
	Check(sc::FarProxyVisible(600.0f, 8.0f, 150.0f, 900.0f), "loin : visible");
	Check(!sc::FarProxyVisible(950.0f, 8.0f, 150.0f, 900.0f), "au-dela du plan lointain : coupee");

	// Grille large : un Gather à 1 500 m trouve une instance lointaine et ignore le reste.
	sc::ScatterField field(sc::kFarProxyGridM);
	Check(field.CellSizeM() == sc::kFarProxyGridM, "taille de case du champ");
	sc::ScatterItem itNear; itNear.center = { 50.0f, 0.0f, 0.0f }; itNear.radius = 6.0f;
	sc::ScatterItem itFar; itFar.center = { 1200.0f, 0.0f, 300.0f }; itFar.radius = 6.0f;
	sc::ScatterItem tooFar; tooFar.center = { 4000.0f, 0.0f, 0.0f }; tooFar.radius = 6.0f;
	field.Add(itNear); field.Add(itFar); field.Add(tooFar);
	sc::Frustum all{};
	for (auto& p : all.planes) p = { 0.0f, 0.0f, 0.0f, 1.0f }; // tout est dedans
	std::vector<uint32_t> out;
	field.Gather({ 0.0f, 0.0f, 0.0f }, all, sc::CullDistances{ 1500.0f, 1500.0f, 1500.0f }, out);
	Check(out.size() == 2, "gather grille large : 2 instances dans 1 500 m");
	sc::ScatterField dense;
	Check(dense.CellSizeM() == sc::ScatterField::kCellM, "grille par defaut 32 m inchangee");

	if (g_fail == 0) std::printf("far_proxy_rules_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
