// cell_world_frame_tests — repère de rendu et sélection des cellules (client « Royaumes »).
#include "src/client/world/cells/CellWorldFrame.h"
#include "src/client/world/cells/CellHeightFields.h"
#include "src/client/world/cells/CellAssetLoader.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}
	bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}

int main()
{
	using namespace engine::world::cells;

	// Repère centré sur le hameau de Marn (N003_W009, centre (-90000, -30000)).
	RenderFrame f;
	f.SetOriginCell({ 3, -9 });
	Check(Near(f.OriginX(), -90000.0) && Near(f.OriginZ(), -30000.0), "origine = centre de cellule");
	Check(Near(f.ToRenderX(-89985.5), 14.5) && Near(f.ToRenderZ(-30010.25), -10.25), "global -> rendu");
	Check(Near(f.ToGlobalX(14.5), -89985.5) && Near(f.ToGlobalZ(-10.25), -30010.25), "rendu -> global");
	Check(f.CellOfRender(0.0, 0.0) == CellCoord{ 3, -9 } && f.CellOfRender(5000.0, 0.0) == CellCoord{ 3, -8 }, "cellule d'un point de rendu");
	const CellPoint east = f.CellCenterRender({ 3, -8 });
	Check(Near(east.x, 10000.0) && Near(east.z, 0.0), "centre de la voisine est en rendu");
	const CellPoint north = f.CellCenterRender({ 4, -9 });
	Check(Near(north.x, 0.0) && Near(north.z, -10000.0), "nord = -Z");

	// Rebase vers l'est : un point à +5100 m devient -4900 m dans le nouveau repère.
	const CellPoint d = f.RebaseDelta({ 3, -8 });
	Check(Near(d.x, -10000.0) && Near(d.z, 0.0), "delta de rebase");
	Check(Near(5100.0 + d.x, -4900.0), "point recentre");
	// Précision : un float garde le centimètre à 15 km du centre.
	const float r = f.ToRenderX(-89999.987);
	Check(std::fabs(static_cast<double>(r) - 0.013) < 1e-6, "precision du rendu pres de l'origine");

	// Ensemble des cellules générées.
	GeneratedCellSet g;
	g.Add({ 3, -9 });
	g.Add({ 3, -8 });
	g.Add({ 3, -9 });      // doublon
	g.Add({ 600, 0 });     // hors carte : ignorée
	Check(g.Cells().size() == 2, "doublons et hors carte ignores");
	Check(g.Contains({ 3, -8 }) && !g.Contains({ 0, 0 }) && !g.Contains({ 600, 0 }), "appartenance");
	Check(g.ContainsGlobal(-90000.0, -30000.0) && !g.ContainsGlobal(0.0, 0.0), "appartenance globale");
	Check(!g.ContainsGlobal(1e30, 0.0) && !g.ContainsGlobal(std::nan(""), 0.0), "valeurs aberrantes");

	// Sélection : au centre de Marn, rayon 3 km → seulement Marn ; à 1 km du bord est → les deux.
	std::vector<CellCoord> sel;
	SelectCellsInRange(-90000.0, -30000.0, 3000.0, g, sel);
	Check(sel.size() == 1 && sel[0] == CellCoord{ 3, -9 }, "centre : une cellule");
	SelectCellsInRange(-86000.0, -30000.0, 3000.0, g, sel);
	Check(sel.size() == 2 && sel[0] == CellCoord{ 3, -9 } && sel[1] == CellCoord{ 3, -8 }, "pres du bord : deux, la plus proche d'abord");
	SelectCellsInRange(0.0, 0.0, 3000.0, g, sel);
	Check(sel.empty(), "loin de tout : aucune");

	// Mur invisible.
	Check(IsMoveAllowed(g, -90000.0, -30000.0, -80000.0, -30000.0), "vers une cellule generee : permis");
	Check(!IsMoveAllowed(g, -94990.0, -30000.0, -95010.0, -30000.0), "vers une cellule non generee : bloque");
	Check(IsMoveAllowed(g, 0.0, 0.0, 10.0, 0.0), "depart hors monde : jamais bloque");
	GeneratedCellSet empty;
	Check(IsMoveAllowed(empty, 0.0, 0.0, 1e6, 0.0), "sans liste : pas de mur");

	// Hauteurs CPU multi-cellules, interrogées en coordonnées de rendu.
	{
		RenderFrame frame;
		frame.SetOriginCell({ 3, -9 });
		CellHeightFields fields(&frame);
		CellHeightTile tile;
		tile.cell = { 3, -8 };                 // voisine est de l'origine
		tile.minX = -85000.0; tile.minZ = -35000.0;
		tile.worldSize = 10000.0; tile.heightScale = 1000.0;
		tile.data.width = 3; tile.data.height = 3;
		// Rampe en X : 0, moitié, plein (normalisé).
		tile.data.heights = { 0, 32768, 65535, 0, 32768, 65535, 0, 32768, 65535 };
		fields.Put(tile);
		Check(fields.Count() == 1 && fields.Has({ 3, -8 }) && !fields.Has({ 3, -9 }), "tuile residente");
		// Centre de la cellule est = rendu (10000, 0) → milieu de la rampe (~500 m).
		Check(std::fabs(fields.HeightAt(10000.0f, 0.0f) - 500.0f) < 1.0f, "hauteur au centre de la voisine");
		Check(fields.IsLoadedAt(10000.0f, 0.0f) && !fields.IsLoadedAt(0.0f, 0.0f), "residence en coordonnees de rendu");
		Check(fields.HeightAt(0.0f, 0.0f) == 0.0f, "cellule absente : 0");
		float h = -1.0f;
		Check(fields.SampleGlobal(-75001.0, -30000.0, h) && h > 990.0f, "bord est de la tuile");
		Check(!fields.SampleGlobal(1e30, 0.0, h), "valeur aberrante refusee");
		fields.Remove({ 3, -8 });
		Check(fields.Count() == 0, "retrait");
	}

	// Chargement des données d'une cellule (thread de travail) sur un contenu minimal.
	{
		namespace fs = std::filesystem;
		const fs::path root = fs::temp_directory_path() / "lcdlln_cell_assets_test";
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root / "zones" / "cell_n003_w009");
		{
			std::ofstream z(root / "zones" / "cell_n003_w009" / "zone.json");
			z << "{ \"terrain\": { \"world_size\": 10000, \"height_scale\": 1000, \"origin_x\": -95000, \"origin_z\": -35000 },"
			     " \"render\": { \"terrain\": { \"heightmap\": \"zones/cell_n003_w009/h.r16h\" } } }";
		}
		{
			std::ofstream h(root / "zones" / "cell_n003_w009" / "h.r16h", std::ios::binary);
			const uint32_t hdr[3] = { 0x504D4148u, 5u, 5u };
			h.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
			for (int i = 0; i < 25; ++i)
			{
				const uint16_t v = static_cast<uint16_t>(i * 1000);
				h.write(reinterpret_cast<const char*>(&v), sizeof(v));
			}
		}
		CellAssetJob job;
		job.cell = { 3, -9 };
		job.wantRender = true;
		job.baseConfig.SetValue("paths.content", root.string());
		CellAssets a;
		Check(LoadCellAssets(job, a) && a.heightsOk && a.renderOk, "cellule chargee");
		Check(a.heights.data.width == 5u && Near(a.heights.minX, -95000.0) && Near(a.heights.heightScale, 1000.0), "heightmap CPU");
		Check(a.terrain.heightmap.heights.size() == 25u && a.terrain.normalPixels.size() == 25u * 4u, "heightmap + normal map de rendu");
		Check(std::fabs(a.terrain.normalVertStep - 2500.0f) < 1e-3f && std::fabs(a.terrain.normalHeightScale - 1000.0f) < 1e-3f,
			"echelle de la normal map = celle du terrain");
		Check(!a.hasScatter && a.buildings.empty(), "sans decor ni batiment");

		CellAssetJob missing = job;
		missing.cell = { 4, -9 };
		CellAssets b;
		Check(!LoadCellAssets(missing, b) && !b.heightsOk && !b.error.empty(), "cellule absente : echec explicite");

		// Thread de travail : deux demandes (dont un doublon), deux résultats.
		CellAssetLoader loader;
		loader.Start();
		loader.Request(job);
		loader.Request(job);
		loader.Request(missing);
		Check(loader.IsPending({ 3, -9 }, true) && loader.IsPending({ 4, -9 }, false), "demandes en vol");
		std::vector<CellAssets> done;
		for (int i = 0; i < 500 && done.size() < 2; ++i)
		{
			loader.Poll(done);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		Check(done.size() == 2 && loader.InFlight() == 0u, "deux resultats, doublon ignore");
		loader.Stop();
		fs::remove_all(root, ec);
	}

	if (g_fail == 0) std::printf("cell_world_frame_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
