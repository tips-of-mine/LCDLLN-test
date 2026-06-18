// M100.34 — Tests Selection / Layers / Minimap / DeleteCommand / Export zone.
//
// Headless. Lié à engine_core (+ zone_builder_lib). Le round-trip d'export
// écrit une arborescence chunk-package via WorldEditorExporter (Save*Bin
// header-only, mêmes sérialiseurs que le client) puis relit chaque fichier
// avec les Load*Bin (parité éditeur ↔ client : "consommé par le client").

#include "src/world_editor/DeleteCommand.h"
#include "src/world_editor/LayersDocument.h"
#include "src/world_editor/SelectionTool.h"
#include "src/world_editor/WorldEditorExporter.h"
#include "src/world_editor/panels/MinimapPanel.h"

#include "src/client/world/foliage/FoliageInstances.h"
#include "src/client/world/hazard/HazardVolumes.h"
#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/PropInstances.h"
#include "src/client/world/interactive/InteractiveInstances.h"
#include "src/client/world/spline/SplineInstances.h"
#include "src/client/world/thermal/ShadeMap.h"
#include "src/client/world/wind/WindZones.h"
#include "src/client/world/zones/Zones.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace engine::editor::world;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 1e-4f) { return (a - b < eps) && (b - a < eps); }

	std::vector<uint8_t> ReadFile(const std::filesystem::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		if (!in.good()) return {};
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	engine::world::instances::PropInstance MakeProp(uint32_t id, float x)
	{
		engine::world::instances::PropInstance p;
		p.instanceId = id;
		p.assetId = id * 10u;
		p.position = { x, 0.0f, 0.0f };
		return p;
	}

	// -------------------------------------------------------------------------
	// Selection
	// -------------------------------------------------------------------------
	void Test_Selection_Rect_SelectsContainedProps()
	{
		std::vector<SelectablePoint> pts = {
			{1, 0.0f, 0.0f}, {2, 5.0f, 5.0f}, {3, 20.0f, 20.0f}, {4, -3.0f, 2.0f}
		};
		// Rectangle [-1..10]x[-1..10] : contient ids 1 et 2, pas 3 (trop loin)
		// ni 4 (x=-3 < -1).
		SelectionRect rect{ -1.0f, -1.0f, 10.0f, 10.0f };
		auto sel = SelectInRect(pts, rect);
		REQUIRE(sel.size() == 2);
		bool has1 = false, has2 = false;
		for (uint32_t id : sel) { if (id == 1) has1 = true; if (id == 2) has2 = true; }
		REQUIRE(has1 && has2);

		// Rectangle inversé (drag bas-droite → haut-gauche) : même résultat.
		SelectionRect inv{ 10.0f, 10.0f, -1.0f, -1.0f };
		REQUIRE(SelectInRect(pts, inv).size() == 2);
	}

	void Test_Selection_Lasso_SelectsContainedProps()
	{
		std::vector<SelectablePoint> pts = {
			{1, 1.0f, 1.0f}, {2, 2.0f, 8.0f}, {3, 50.0f, 50.0f}
		};
		// Triangle (0,0)-(10,0)-(0,10) : contient id 1, pas 2 (au-dessus de
		// l'hypoténuse x+z<=10 ? 2+8=10 → sur la limite, exclu), pas 3.
		std::vector<std::pair<float, float>> poly = { {0,0}, {10,0}, {0,10} };
		auto sel = SelectInLasso(pts, poly);
		bool has1 = false, has3 = false;
		for (uint32_t id : sel) { if (id == 1) has1 = true; if (id == 3) has3 = true; }
		REQUIRE(has1);
		REQUIRE(!has3);

		// Polygone dégénéré (< 3 sommets) → rien.
		std::vector<std::pair<float, float>> deg = { {0,0}, {1,1} };
		REQUIRE(SelectInLasso(pts, deg).empty());
	}

	// -------------------------------------------------------------------------
	// Layers
	// -------------------------------------------------------------------------
	void Test_Layer_Visibility_HidesPropsInOutliner()
	{
		LayersDocument doc;
		doc.AssignEntity(42, 3);
		REQUIRE(doc.GetEntityLayer(42) == 3);
		REQUIRE(doc.IsEntityVisible(42)); // calque visible par défaut.
		doc.SetVisible(3, false);
		REQUIRE(!doc.IsEntityVisible(42)); // masqué.
		// Entité non assignée suit Default (visible).
		REQUIRE(doc.IsEntityVisible(999));
	}

	void Test_Layer_Lock_BlocksEdits()
	{
		LayersDocument doc;
		doc.AssignEntity(7, 2);
		REQUIRE(!doc.IsEntityLocked(7));
		doc.SetLocked(2, true);
		REQUIRE(doc.IsEntityLocked(7));
		// 16 calques max ; index hors borne retombe sur Default sans crash.
		doc.AssignEntity(8, 99);
		REQUIRE(doc.GetEntityLayer(8) == 0);
	}

	// -------------------------------------------------------------------------
	// Minimap
	// -------------------------------------------------------------------------
	void Test_Minimap_RendersCurrentChunk()
	{
		std::vector<std::pair<int32_t, int32_t>> loaded = { {0,0}, {1,0} };
		MinimapView v = BuildMinimapView(0, 0, 1, loaded);
		REQUIRE(v.cells.size() == 9); // (2*1+1)^2
		int currentCount = 0, loadedCount = 0;
		for (const auto& c : v.cells)
		{
			if (c.isCurrent) { ++currentCount; REQUIRE(c.chunkX == 0 && c.chunkZ == 0); }
			if (c.isLoaded) ++loadedCount;
		}
		REQUIRE(currentCount == 1);
		REQUIRE(loadedCount == 2); // (0,0) et (1,0)
	}

	// -------------------------------------------------------------------------
	// DeleteCommand
	// -------------------------------------------------------------------------
	void Test_DeleteCommand_RoundTrip()
	{
		std::vector<engine::world::instances::PropInstance> props = {
			MakeProp(1, 1.0f), MakeProp(2, 2.0f), MakeProp(3, 3.0f), MakeProp(4, 4.0f)
		};
		DeleteCommand cmd(props, { 2, 4 });
		cmd.Execute();
		REQUIRE(props.size() == 2);
		REQUIRE(props[0].instanceId == 1 && props[1].instanceId == 3);
		cmd.Undo();
		REQUIRE(props.size() == 4);
		// Ordre d'origine restauré.
		REQUIRE(props[0].instanceId == 1);
		REQUIRE(props[1].instanceId == 2);
		REQUIRE(props[2].instanceId == 3);
		REQUIRE(props[3].instanceId == 4);
		REQUIRE(Near(props[3].position.x, 4.0f));
	}

	// -------------------------------------------------------------------------
	// Export zone — round-trip complet (parité éditeur ↔ client)
	// -------------------------------------------------------------------------
	void Test_ExportZone_FullRoundtrip()
	{
		ZoneExportInputs in;
		// Props.
		in.props = { MakeProp(10, 1.5f), MakeProp(11, 2.5f) };
		// Buildings (auberge éditable) : la carte stocke une RÉFÉRENCE (type +
		// variante + transform), pas les pièces.
		{
			engine::world::instances::BuildingPlacement pl;
			pl.guid = 5u;
			pl.templateType = "tavern";
			pl.variantId = "auberge_terrasse";
			pl.displayName = "Auberge";
			pl.worldPosition = { 88.0f, 0.0f, 100.0f };
			pl.worldYawDeg = 30.0f;
			in.buildings = { pl };
		}
		// Hazards.
		engine::world::hazard::HazardVolume hz;
		hz.type = engine::world::hazard::HazardType::Tar;
		hz.position = { 7.0f, 0.0f, 9.0f };
		in.hazards = { hz };
		// Interactives.
		engine::world::interactive::InteractivePropInstance it;
		it.id = 77; it.type = engine::world::interactive::InteractiveType::DoorHinge;
		it.openAngleDeg = 90.0f; it.audioOpenEvent = "creak";
		in.interactives = { it };
		// Zones.
		engine::world::zones::GameplayZone gz;
		gz.type = engine::world::zones::ZoneType::PvPZone;
		gz.name = "arena";
		gz.polygon = { {0,0,0}, {10,0,0}, {10,0,10} };
		in.zones = { gz };
		// Splines.
		engine::world::spline::Spline sp;
		sp.type = engine::world::spline::SplineType::Road;
		sp.nodes.push_back({ {1.0f, 0.0f, 1.0f}, 6.0f, 0.0f });
		sp.nodes.push_back({ {2.0f, 0.0f, 2.0f}, 5.0f, 0.0f });
		in.splines = { sp };
		// Wind zones.
		engine::world::wind::WindZone wz;
		wz.directionX = 0.0f; wz.directionZ = 1.0f; wz.forceMps = 7.5f;
		wz.polygon = { {0,0,0}, {5,0,0}, {5,0,5} };
		in.windZones = { wz };
		// Chunk : foliage + shade.
		ChunkExportData chunk;
		chunk.chunkX = 1; chunk.chunkZ = 2;
		chunk.foliage.push_back({ 0xABCDu, {1.0f, 0.0f, 2.0f}, 0.5f, 1.25f });
		chunk.shade.resolution = engine::world::thermal::kShadeMapResolution;
		chunk.shade.coverage.assign(
			static_cast<size_t>(chunk.shade.resolution) * chunk.shade.resolution, 128u);
		chunk.shade.coverage[0] = 200u;
		chunk.hasShade = true;
		in.chunks = { chunk };

		// Dossier temporaire unique.
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "lcdlln_export_zone_test";
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);

		std::string err;
		REQUIRE(SaveZone(dir.string(), in, err));
		if (!err.empty()) std::fprintf(stderr, "  SaveZone err: %s\n", err.c_str());

		// Relit chaque fichier via les Load*Bin (= ce que fait le client).
		const std::filesystem::path inst = dir / "instances";
		std::string lerr;

		std::vector<engine::world::instances::PropInstance> props2;
		REQUIRE(engine::world::instances::LoadPropsBin(ReadFile(inst / "props.bin"), props2, lerr));
		REQUIRE(props2.size() == 2);
		if (props2.size() == 2) { REQUIRE(props2[0].instanceId == 10); REQUIRE(Near(props2[1].position.x, 2.5f)); }

		std::vector<engine::world::instances::BuildingPlacement> bd2;
		REQUIRE(engine::world::instances::LoadBuildingsBin(ReadFile(inst / "buildings.bin"), bd2, lerr));
		REQUIRE(bd2.size() == 1);
		if (bd2.size() == 1)
		{
			REQUIRE(bd2[0].guid == 5u);
			REQUIRE(bd2[0].templateType == "tavern");
			REQUIRE(bd2[0].variantId == "auberge_terrasse");
			REQUIRE(bd2[0].displayName == "Auberge");
			REQUIRE(Near(bd2[0].worldYawDeg, 30.0f));
		}

		std::vector<engine::world::hazard::HazardVolume> hz2;
		REQUIRE(engine::world::hazard::LoadHazardsBin(ReadFile(inst / "hazards.bin"), hz2, lerr));
		REQUIRE(hz2.size() == 1);
		if (hz2.size() == 1) { REQUIRE(hz2[0].type == engine::world::hazard::HazardType::Tar); REQUIRE(Near(hz2[0].position.z, 9.0f)); }

		std::vector<engine::world::interactive::InteractivePropInstance> it2;
		REQUIRE(engine::world::interactive::LoadInteractivesBin(ReadFile(inst / "interactives.bin"), it2, lerr));
		REQUIRE(it2.size() == 1);
		if (it2.size() == 1) { REQUIRE(it2[0].id == 77); REQUIRE(it2[0].audioOpenEvent == "creak"); }

		std::vector<engine::world::zones::GameplayZone> gz2;
		REQUIRE(engine::world::zones::LoadZonesBin(ReadFile(inst / "zones.bin"), gz2, lerr));
		REQUIRE(gz2.size() == 1);
		if (gz2.size() == 1) { REQUIRE(gz2[0].type == engine::world::zones::ZoneType::PvPZone); REQUIRE(gz2[0].name == "arena"); REQUIRE(gz2[0].polygon.size() == 3); }

		std::vector<engine::world::spline::Spline> sp2;
		REQUIRE(engine::world::spline::LoadSplinesBin(ReadFile(inst / "splines.bin"), sp2, lerr));
		REQUIRE(sp2.size() == 1);
		if (sp2.size() == 1) { REQUIRE(sp2[0].nodes.size() == 2); REQUIRE(Near(sp2[0].nodes[1].widthMeters, 5.0f)); }

		std::vector<engine::world::wind::WindZone> wz2;
		REQUIRE(engine::world::wind::LoadWindZonesBin(ReadFile(inst / "wind_zones.bin"), wz2, lerr));
		REQUIRE(wz2.size() == 1);
		if (wz2.size() == 1) { REQUIRE(Near(wz2[0].forceMps, 7.5f)); REQUIRE(Near(wz2[0].directionZ, 1.0f)); }

		// Par-chunk.
		const std::filesystem::path chunkDir = dir / "chunks" / "chunk_1_2";
		std::vector<engine::world::foliage::FoliageInstance> fol2;
		REQUIRE(engine::world::foliage::LoadFoliageBin(ReadFile(chunkDir / "foliage.bin"), fol2, lerr));
		REQUIRE(fol2.size() == 1);
		if (fol2.size() == 1) { REQUIRE(fol2[0].assetIdHash == 0xABCDu); REQUIRE(Near(fol2[0].scale, 1.25f)); }

		engine::world::thermal::ShadeMap shade2;
		REQUIRE(engine::world::thermal::LoadShadeMapBin(ReadFile(chunkDir / "shade.bin"), shade2, lerr));
		REQUIRE(shade2.resolution == engine::world::thermal::kShadeMapResolution);
		REQUIRE(shade2.coverage.size() == chunk.shade.coverage.size());
		if (!shade2.coverage.empty()) REQUIRE(shade2.coverage[0] == 200u);

		std::filesystem::remove_all(dir, ec);
	}
}

int main()
{
	Test_Selection_Rect_SelectsContainedProps();
	Test_Selection_Lasso_SelectsContainedProps();
	Test_Layer_Visibility_HidesPropsInOutliner();
	Test_Layer_Lock_BlocksEdits();
	Test_Minimap_RendersCurrentChunk();
	Test_DeleteCommand_RoundTrip();
	Test_ExportZone_FullRoundtrip();

	if (g_failed == 0)
		std::printf("[export_zone_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[export_zone_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
