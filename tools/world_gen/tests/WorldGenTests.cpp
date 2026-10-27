// world_gen_tests — tests du générateur de monde « Royaumes » (CPU pur, CI Linux).
//
// 1. scatter.bin : round-trip bit-à-bit v1 et v2 (table de chunks) + rejets.
// 2. PNG : l'encodeur maison est relu par stb_image (décodeur du client).
// 3. Master réel (game/data/world/royaumes/world_master.json) : chargement OK,
//    échelle 10 km, villes de départ dans les cellules générées.
// 4. Génération de la cellule du hameau de Marn vers DEUX dossiers : octets
//    identiques (déterminisme), puis invariants (heightmap 2049², chunks,
//    départ humain, spawners en coordonnées globales DANS la cellule).
// 5. Génération de la cellule voisine (ouest) : la colonne de frontière de la
//    heightmap est IDENTIQUE des deux côtés (raccord sans couture).

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "lib/PngWriter.h"
#include "lib/WorldGen.h"
#include "lib/WorldGenContext.h"

#include "src/shared/world/WorldCells.h"

#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/ScatterInstances.h"

#include <zone_builder/JsonDocument.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "[FAIL] %s\n", what); ++g_failures; }
		else std::printf("[ OK ] %s\n", what);
	}

	std::vector<uint8_t> ReadBytes(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}
	std::string ReadText(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::stringstream ss; ss << f.rdbuf(); return ss.str();
	}

	void TestScatterRoundTrip()
	{
		using namespace engine::world::instances;
		ScatterFile a;
		a.meshPaths = { "meshes/props/CommonTree_1.gltf", "meshes/props/Rock_Medium_2.gltf" };
		for (int i = 0; i < 50; ++i)
		{
			ScatterInstance in;
			in.meshIndex = static_cast<uint16_t>(i % 2);
			in.flags = static_cast<uint8_t>(i % 8);
			in.cullClass = static_cast<uint8_t>(i % 3);
			in.x = 1.25f * i; in.z = -0.5f * i; in.yOffset = -0.1f; in.yawDeg = 7.0f * i;
			in.scale = 1.0f + 0.01f * i; in.collisionRadius = 0.5f; in.tiltDeg = 2.0f;
			a.instances.push_back(in);
		}
		const std::vector<uint8_t> bytes = SaveScatterBin(a);
		ScatterFile b; std::string err;
		Check(LoadScatterBin(bytes, b, err), "scatter.bin : relecture OK");
		Check(b.meshPaths == a.meshPaths && b.instances.size() == a.instances.size(), "scatter.bin : tables identiques");
		Check(SaveScatterBin(b) == bytes, "scatter.bin : round-trip bit-a-bit");
		std::vector<uint8_t> bad = bytes; bad[0] ^= 0xFF;
		Check(!LoadScatterBin(bad, b, err), "scatter.bin : magic invalide rejete");
		std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 5);
		Check(!LoadScatterBin(cut, b, err), "scatter.bin : fichier tronque rejete");
		ScatterFile c = a; c.instances[3].meshIndex = 9;
		Check(!LoadScatterBin(SaveScatterBin(c), b, err), "scatter.bin : meshIndex hors table rejete");
		// v2 : table de chunks 2x2 (instances triées par chunk).
		ScatterFile v2 = a;
		v2.chunks.originX = -10.0f; v2.chunks.originZ = 20.0f; v2.chunks.chunkSizeM = 500.0f;
		v2.chunks.cols = 2; v2.chunks.rows = 2;
		v2.chunks.ranges = { { 0, 10 }, { 10, 15 }, { 25, 0 }, { 25, 25 } };
		const std::vector<uint8_t> b2 = SaveScatterBin(v2);
		ScatterFile r2;
		Check(LoadScatterBin(b2, r2, err) && r2.chunks.cols == 2 && r2.chunks.ranges.size() == 4
			&& r2.chunks.ranges[3].first == 25 && r2.chunks.ranges[3].count == 25, "scatter.bin v2 : table de chunks relue");
		Check(SaveScatterBin(r2) == b2, "scatter.bin v2 : round-trip bit-a-bit");
		ScatterFile bad2 = v2; bad2.chunks.ranges[3].count = 99;
		Check(!LoadScatterBin(SaveScatterBin(bad2), r2, err), "scatter.bin v2 : plage hors instances rejetee");
		Check(SaveScatterBin(a)[4] == 1, "scatter.bin sans chunks : reste v1");
	}

	void TestPng()
	{
		const uint32_t W = 37, H = 23;
		std::vector<uint8_t> rgb(W * H * 3);
		for (uint32_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<uint8_t>((i * 7) ^ (i / 13));
		for (uint32_t i = 0; i < 300; ++i) rgb[i] = 200; // plage répétée (chemin LZ77)
		const std::vector<uint8_t> png = tools::world_gen::EncodePngRgb(rgb, W, H);
		int w = 0, h = 0, n = 0;
		unsigned char* px = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n, 3);
		Check(px != nullptr && w == static_cast<int>(W) && h == static_cast<int>(H), "PNG : decode par stb_image");
		bool same = px != nullptr;
		for (uint32_t i = 0; same && i < rgb.size(); ++i) same = px[i] == rgb[i];
		Check(same, "PNG : pixels identiques apres decodage");
		stbi_image_free(px);
	}

	void TestMaster()
	{
		tools::world_gen::WorldMaster m;
		std::string err;
		const bool ok = tools::world_gen::LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", m, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "master royaumes : chargement et validation");
		Check(m.cities.size() >= 30, "master : au moins 30 agglomerations");
		std::set<std::string> races;
		for (const auto& c : m.cities) for (const auto& r : c.startFor) races.insert(r);
		Check(races == std::set<std::string>{ "humains", "elfes", "orcs", "nains", "demons" },
			"master : une ville de depart par race jouable");
		Check(m.scale.cellSizeM == 10000.0 && m.scale.heightmapRes == 2049, "master : cellules de 10 km, heightmap 2049");
		Check(m.generatedCells.size() >= 5, "master : au moins les 5 cellules de depart");
	}

	std::vector<uint16_t> ReadHeights(const std::filesystem::path& p, uint32_t& n)
	{
		const std::vector<uint8_t> b = ReadBytes(p);
		n = 0;
		if (b.size() < 12) return {};
		std::memcpy(&n, b.data() + 4, 4);
		std::vector<uint16_t> h(static_cast<size_t>(n) * n);
		if (b.size() < 12 + h.size() * 2) return {};
		std::memcpy(h.data(), b.data() + 12, h.size() * 2);
		return h;
	}

	// Liaison des villes de départ : A —(route 1)— B —(route 2)— C, et une route
	// D—E isolée. Départs : A, C, E. A et C sont reliés par les routes 1 et 2 (les
	// cellules qu'elles traversent sont ajoutées) ; E est signalée isolée.
	void TestStartCorridors()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		g.master.corridorMarginM = 0.0;
		auto city = [&](const char* id, bool start) {
			CityDef c; c.id = id; if (start) c.startFor = { "humains" }; g.cities.push_back(c);
		};
		city("A", true); city("B", false); city("C", true); city("D", false); city("E", true);
		auto road = [&](const char* a, const char* b, std::vector<Vec2> pts) {
			Road r; r.fromId = a; r.toId = b; r.points = std::move(pts); g.roads.push_back(r);
		};
		// Cellule (0,0) = [−5000, 5000[ ; (0,1) à l'est ; (0,2) encore à l'est.
		road("A", "B", { { 0.0, 0.0 }, { 6000.0, 0.0 } });      // (0,0) → (0,1)
		road("B", "C", { { 6000.0, 0.0 }, { 16000.0, 0.0 } });  // (0,1) → (0,2)
		road("D", "E", { { 0.0, 50000.0 }, { 100.0, 50000.0 } });
		Road local; local.path = true; local.fromId = "A"; local.toId = "E";
		local.points = { { 0.0, 0.0 }, { 0.0, 50000.0 } };
		g.roads.push_back(local); // chemin local : ignoré
		std::vector<std::string> ids = { "N000_E000" };
		AddStartCorridors(g, ids);
		std::set<std::string> s(ids.begin(), ids.end());
		Check(s.count("N000_E001") && s.count("N000_E002"), "liaison : cellules des routes A-B-C ajoutees");
		Check(!s.count("S005_E000"), "liaison : chemin local ignore, ville isolee non reliee");
		Check(ids.front() == "N000_E000" && s.size() == ids.size(), "liaison : ordre du master conserve, sans doublon");
		bool warned = false;
		for (const std::string& w : g.report.warnings) warned = warned || w.find("isolee") != std::string::npos;
		Check(warned, "liaison : ville de depart isolee signalee");
	}

	void TestGenerationAndInvariants()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_tests";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		tools::world_gen::GenReport r1, r2, r3;
		std::string err;
		tools::world_gen::GenOptions o;
		o.contentRoot = LCDLLN_CONTENT_DIR;
		o.worldId = "royaumes";
		o.cells = { "N003_W009" };
		o.outputRoot = (tmp / "a").string();
		const bool ok1 = tools::world_gen::RunWorldGen(o, r1, err);
		if (!ok1) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok1, "generation cellule N003_W009 (passe 1)");
		o.outputRoot = (tmp / "b").string();
		const bool ok2 = tools::world_gen::RunWorldGen(o, r2, err);
		Check(ok2, "generation cellule N003_W009 (passe 2)");
		if (!ok1 || !ok2) return;

		bool identical = r1.writtenFiles == r2.writtenFiles;
		for (const std::string& f : r1.writtenFiles)
			if (ReadBytes(tmp / "a" / f) != ReadBytes(tmp / "b" / f))
			{
				std::fprintf(stderr, "  difference : %s\n", f.c_str());
				identical = false;
			}
		Check(identical, "determinisme : sorties identiques octet pour octet");

		const fs::path out = tmp / "a";
		const std::string cell = "cell_n003_w009";
		const double x0 = -95000.0, z0 = -35000.0; // coin min de N003_W009
		// Heightmap : 2049² HAMP.
		uint32_t n = 0;
		const std::vector<uint16_t> hA = ReadHeights(out / "zones" / cell / "terrain_height.r16h", n);
		Check(n == 2049 && hA.size() == 2049u * 2049u, "heightmap 2049x2049 HAMP");
		// Décor : v2, table de chunks 20x20 cohérente, instances dans la cellule.
		{
			engine::world::instances::ScatterFile sf; std::string e;
			const bool ok = engine::world::instances::LoadScatterBin(ReadBytes(out / "instances" / ("zone_" + cell) / "scatter.bin"), sf, e);
			Check(ok, "scatter.bin genere : relecture OK");
			Check(sf.chunks.cols == 20 && sf.chunks.rows == 20, "scatter.bin : table de chunks 20x20 (500 m)");
			bool inside = !sf.instances.empty();
			for (const auto& in : sf.instances)
				if (in.x < x0 || in.x >= x0 + 10000.0 || in.z < z0 || in.z >= z0 + 10000.0) { inside = false; break; }
			Check(inside, "scatter.bin : toutes les instances dans la cellule");
			uint64_t covered = 0;
			for (const auto& r : sf.chunks.ranges) covered += r.count;
			Check(covered == sf.instances.size(), "scatter.bin : les chunks couvrent toutes les instances");
		}
		// Bâtiments : références résolues dans la bibliothèque.
		{
			std::vector<engine::world::instances::BuildingPlacement> pls; std::string e;
			Check(engine::world::instances::LoadBuildingsBin(ReadBytes(out / "instances" / ("zone_" + cell) / "buildings.bin"), pls, e),
				"buildings.bin genere : relecture OK");
			Check(pls.size() > 60, "buildings.bin : hameau + lieux proceduraux batis");
			const std::string maison = ReadText(out / "buildings/templates/maison.json");
			bool allResolved = true;
			for (const auto& p : pls)
			{
				if (p.templateType == "maison" && maison.find("\"id\": \"" + p.variantId + "\"") == std::string::npos) allResolved = false;
				if (p.templateType != "maison" && p.templateType != "tavern" && p.templateType != "house") allResolved = false;
			}
			Check(allResolved, "buildings.bin : types/variantes connus");
		}
		// Départ humain (spawn par défaut de la cellule) : dans la cellule, au sec ;
		// --cells ne réécrit pas les sorties globales.
		{
			Check(!fs::exists(out / "world/royaumes/start_locations.json"), "--cells : sorties globales non reecrites");
			tools::zone_builder::JsonValue root; std::string e;
			const bool ok = tools::zone_builder::ParseJsonDocument(ReadText(out / "zones" / cell / "zone.json"), root, e);
			Check(ok, "zone.json : JSON valide");
			const auto* world = ok ? tools::zone_builder::FindObjectMember(root, "world") : nullptr;
			Check(world && world->type == tools::zone_builder::JsonType::Object && tools::zone_builder::FindObjectMember(*world, "dof") != nullptr,
				"zone.json : reglages world.* importes (dof, brouillard...)");
			const auto* sp = world ? tools::zone_builder::FindObjectMember(*world, "default_spawn") : nullptr;
			bool good = sp != nullptr;
			if (sp)
			{
				const double x = tools::zone_builder::FindObjectMember(*sp, "x")->numberValue;
				const double z = tools::zone_builder::FindObjectMember(*sp, "z")->numberValue;
				const double y = tools::zone_builder::FindObjectMember(*sp, "y")->numberValue;
				good = x >= x0 && x < x0 + 10000.0 && z >= z0 && z < z0 + 10000.0 && y > 41.0;
			}
			Check(good, "depart humain : dans sa cellule et au sec");
		}
		// Spawners : zoneId de la cellule, ids uniques, positions dans la cellule, routines valides.
		{
			tools::zone_builder::JsonValue root; std::string e;
			const bool ok = tools::zone_builder::ParseJsonDocument(ReadText(out / "zones" / cell / "spawners.json"), root, e);
			Check(ok, "spawners.json : JSON valide");
			const auto* zid = ok ? tools::zone_builder::FindObjectMember(root, "zoneId") : nullptr;
			Check(zid && static_cast<uint32_t>(zid->numberValue) == engine::world::cells::CellZoneId({ 3, -9 }), "spawners : zoneId = identifiant de la cellule");
			const auto* sp = ok ? tools::zone_builder::FindObjectMember(root, "spawners") : nullptr;
			std::set<std::string> ids;
			bool routinesOk = sp != nullptr, inside = sp != nullptr;
			size_t withRoutine = 0;
			size_t withAgenda = 0;
			bool agendasOk = true;
			if (sp)
				for (const auto& s : sp->arrayValue)
				{
					ids.insert(tools::zone_builder::FindObjectMember(s, "id")->stringValue);
					const auto& pos = tools::zone_builder::FindObjectMember(s, "position")->arrayValue;
					if (pos[0].numberValue < x0 || pos[0].numberValue >= x0 + 10000.0 || pos[2].numberValue < z0 || pos[2].numberValue >= z0 + 10000.0) inside = false;
					// ETAPE4.4 — agenda : métier connu du shard et domicile présent.
					if (const auto* ag = tools::zone_builder::FindObjectMember(s, "agenda"))
					{
						++withAgenda;
						const auto* prof = tools::zone_builder::FindObjectMember(*ag, "profession");
						const auto* places = tools::zone_builder::FindObjectMember(*ag, "places");
						const auto* home = places ? tools::zone_builder::FindObjectMember(*places, "home") : nullptr;
						const std::string p = prof ? prof->stringValue : std::string();
						if (!home || home->arrayValue.empty()
							|| (p != "fermier" && p != "artisan" && p != "marchand" && p != "aubergiste" && p != "pretre" && p != "flaneur"))
							agendasOk = false;
					}
					const auto* rt = tools::zone_builder::FindObjectMember(s, "routine");
					if (!rt) continue;
					++withRoutine;
					const std::string kind = tools::zone_builder::FindObjectMember(*rt, "kind")->stringValue;
					const auto* path = tools::zone_builder::FindObjectMember(*rt, "path");
					if ((kind == "tour" || kind == "patrol") && (!path || path->arrayValue.size() < 2)) routinesOk = false;
				}
			Check(sp && ids.size() == sp->arrayValue.size(), "spawners : ids uniques");
			Check(inside, "spawners : positions globales dans la cellule");
			Check(routinesOk, "spawners : routines tour/patrol avec chemin >= 2 points");
			Check(withRoutine > 20, "spawners : PNJ d'ambiance presents");
			Check(withAgenda > 5 && agendasOk, "spawners : agendas de metier (metier connu, domicile)");
		}

		// Raccord : la cellule voisine à l'ouest partage la colonne x = -95000.
		o.cells = { "N003_W010" };
		o.outputRoot = (tmp / "c").string();
		const bool ok3 = tools::world_gen::RunWorldGen(o, r3, err);
		if (!ok3) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok3, "generation cellule voisine N003_W010");
		if (ok3)
		{
			uint32_t nB = 0;
			const std::vector<uint16_t> hB = ReadHeights(tmp / "c" / "zones/cell_n003_w010/terrain_height.r16h", nB);
			bool seam = nB == n && !hB.empty() && !hA.empty();
			int diff = 0;
			for (uint32_t j = 0; seam && j < n; ++j)
				if (hA[static_cast<size_t>(j) * n + 0] != hB[static_cast<size_t>(j) * n + (n - 1)]) ++diff;
			if (diff) std::fprintf(stderr, "  %d echantillons differents sur la frontiere\n", diff);
			Check(seam && diff == 0, "raccord : colonne frontiere identique entre cellules voisines");
		}
		fs::remove_all(tmp, ec);
	}
}

int main()
{
	TestScatterRoundTrip();
	TestPng();
	TestMaster();
	TestStartCorridors();
	TestGenerationAndInvariants();
	if (g_failures)
	{
		std::fprintf(stderr, "world_gen_tests : %d echec(s)\n", g_failures);
		return 1;
	}
	std::printf("world_gen_tests : tout est vert\n");
	return 0;
}
