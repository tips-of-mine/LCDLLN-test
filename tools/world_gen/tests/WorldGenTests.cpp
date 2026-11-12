// world_gen_tests — tests du générateur de monde « Royaumes » (CPU pur, CI Linux).
//
// 1. scatter.bin : round-trip bit-à-bit v1 et v2 (table de chunks) + rejets.
// 2. PNG : l'encodeur maison est relu par stb_image (décodeur du client).
// 3. Master réel (game/data/world/royaumes/world_master.json) : chargement OK,
//    échelle 10 km, une ville de départ par faction (v1.0.61 : hameaux dédiés
//    voisins de leur ville), villes de départ dans les cellules générées.
// 4. Génération de la cellule du hameau de Marn vers DEUX dossiers : octets
//    identiques (déterminisme), puis invariants (heightmap 2049², chunks,
//    plus de départ à Marn, spawners en coordonnées globales DANS la cellule).
// 5. Génération de la cellule voisine (ouest) : la colonne de frontière de la
//    heightmap est IDENTIQUE des deux côtés (raccord sans couture) ; puis d'une
//    cellule de départ de faction (Haeldorf) : spawn par défaut au sec.
// 6. État « reconstruction » (Kendrhir) : maisons, ruines et chantiers
//    mêlés, auberge en service, étais autour des chantiers.
// 7. EAU.5 : cuvettes sous la mer isolées comblées (mer reliée conservée) ;
//    lac creusé d'après son polygone, rives au-dessus de l'eau même à flanc
//    de pente (digue), pas de digue sur un lit de rivière.
// 8. Seconde carte « enfers » : master (9 cellules 3×3 depuis la v1.0.71 : croix
//    volcanique et 4 coins montagneux ; départ de la Légion infernale, 5 portails
//    appariés aux Portes des enfers du continent), puis génération complète →
//    dossiers zones/enfers_*, zone ids 2 000 000+, cimetière neutre, mines des coins
//    tenues par la Légion (pas de nains), start_locations.json et portals.json.
// 9. v1.0.81 : lieux des Enfers des tomes 15-22 sur les bras de la croix (pas
//    sur la case de la Citadelle, toujours 5 portails) et 11 grottes de dragons
//    de Xygga dans le monde des Royaumes (Xygga en case volcanique, au sec, hors
//    des cases à ville, loin des routes).
// 10. v1.0.82 : entrée cachée des Ophidiens dans la jungle de Leviathan (N010_E010,
//    cellule générée) : salle au sec, hors case à ville, loin des routes et à plus de
//    15 km de toute grotte de dragon.
// 11. v1.0.83 : palette infernale des Enfers (aucune herbe, neige, forêt ni
//    broussaille ; lave jaune-orange sur la carte du monde) ; la cellule des
//    Royaumes N003_W009 garde herbe, masque d'herbe et forêt (palette historique).
// 12. v1.0.107 : gouffre caché de Deathtrap Canyon (S008_E010) : profondeur, fond plat,
//    parois, galerie, Thurvhal au fond (secrète, sans route), route 000074 intacte,
//    déterminisme, 3 cellules hors S008_E010 identiques octet pour octet au monde sans
//    gouffre, réapparition au cimetière du gouffre pour une mort dans le canyon (règle du
//    shard, RespawnRules.h) et inchangée ailleurs.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "lib/PngWriter.h"
#include "lib/WorldGen.h"
#include "lib/WorldGenContext.h"

#include "src/shared/world/RespawnRules.h"
#include "src/shared/world/WorldCells.h"

#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/ForestMask.h"
#include "src/client/world/instances/ScatterInstances.h"

#include <zone_builder/JsonDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
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
		// v1.0.61 : départs par FACTION (identifiants de game/data/races/factions.json) ;
		// la Légion infernale naît aux Enfers (world/enfers/world_master.json).
		std::set<std::string> factions;
		std::map<std::string, const tools::world_gen::CityDef*> startOf;
		for (const auto& c : m.cities) for (const auto& f : c.startFor) { factions.insert(f); startOf[f] = &c; }
		Check(factions == std::set<std::string>{ "lumiere", "justice", "dzorak", "dragons", "naine", "elfe", "legions_imperiales" },
			"master : une ville de depart par faction du continent (legion aux enfers)");
		// Chaque départ est un hameau dédié, dans une case voisine de sa ville de référence
		// (Palais des Maîtres des Dragons : sans ville au master, son sanctuaire, même case S003_W004).
		const std::map<std::string, std::string> nearCity = {
			{ "lumiere", "ville_altenberg" }, { "justice", "ville_sysigie" }, { "dzorak", "leviathan_jungle" },
			{ "dragons", "poi_sanctuaire_chevaliers_dragons" }, { "naine", "ville_ghrunke_dash" }, { "elfe", "ville_northind" },
			{ "legions_imperiales", "ville_lhynn" } };
		bool hamletsNear = true;
		for (const auto& [faction, cityId] : nearCity)
		{
			const tools::world_gen::CityDef* start = startOf.count(faction) ? startOf[faction] : nullptr;
			int refN = 0, refE = 0;
			bool found = false;
			for (const auto& c : m.cities)
				if (c.id == cityId) { found = tools::world_gen::ParseCellId(c.cell, refN, refE); }
			for (const auto& p : m.pois)
				if (!found && p.id == cityId) { found = tools::world_gen::ParseCellId(p.cell, refN, refE); }
			// v1.0.72 : Syllaërin (elfe) déplacé par le propriétaire deux cases à l'est de Northind.
			const int reach = faction == "elfe" ? 2 : 1;
			const bool near = start && found && start->kind == "hameau"
				&& std::abs(start->n - refN) <= reach && std::abs(start->e - refE) <= reach && (start->n != refN || start->e != refE);
			if (!near) std::fprintf(stderr, "  depart '%s' pas dans un hameau voisin de %s\n", faction.c_str(), cityId.c_str());
			hamletsNear = hamletsNear && near;
		}
		Check(hamletsNear, "master : departs = hameaux dedies voisins de leur ville (v1.0.61 ; Syllaërin a 2 cases, v1.0.72)");
		bool oldStartsCleared = true;
		for (const auto& c : m.cities)
			if (c.id == "hameau_marn" || c.id == "ville_whispers_hill" || c.id == "leviathan_jungle" || c.id == "ville_ghrunke_dash")
				oldStartsCleared = oldStartsCleared && c.startFor.empty();
		Check(oldStartsCleared, "master : anciens departs par race redevenus des villes normales");
		bool noTsaroth = true;
		for (const auto& c : m.cities) noTsaroth = noTsaroth && c.id != "ville_tsaroth";
		for (const auto& i : m.interactables) noTsaroth = noTsaroth && i.cityId != "ville_tsaroth";
		Check(noTsaroth, "master : camp de Tsaroth retire du continent (ville et PNJ)");
		Check(m.cellMap == engine::world::cells::CellMap::Royaumes, "master : carte des Royaumes");
		Check(m.scale.cellSizeM == 10000.0 && m.scale.heightmapRes == 2049, "master : cellules de 10 km, heightmap 2049");
		Check(m.generatedCells.size() >= 5, "master : au moins les 5 cellules de depart");
		// Les 5 Portes des enfers : cellule générée, lien vers un portail des Enfers.
		size_t linkedGates = 0;
		for (const auto& p : m.pois)
			if (p.kind == "portail" && p.link.rfind("enfers:", 0) == 0
				&& std::find(m.generatedCells.begin(), m.generatedCells.end(), p.cell) != m.generatedCells.end())
				++linkedGates;
		Check(linkedGates == 5, "master : 5 portes des enfers reliees, dans des cellules generees");
		// v1.0.52 : la grille couvre toute la carte du lore (marge de 2 colonnes de
		// mer de chaque cote) et chaque ville du master est dans le lore, hors de l'eau.
		namespace wc = engine::world::cells;
		Check(m.scale.northN == wc::kMaxN && m.scale.northN - static_cast<int>(m.gridRows.size()) + 1 == wc::kMinN,
			"master : rangees N(kMaxN)..S(kMinN) du lore");
		Check(m.scale.westE == wc::kMinE - 2 && m.scale.westE + m.scale.gridCols - 1 == wc::kMaxE + 2,
			"master : colonnes du lore + 2 colonnes de marge de chaque cote");
		bool citiesOk = true;
		for (const auto& c : m.cities)
		{
			const int r = m.scale.northN - c.n, col = c.e - m.scale.westE;
			if (!wc::IsInLoreGrid({ c.n, c.e }) || r < 0 || col < 0 || r >= static_cast<int>(m.gridRows.size()) || col >= m.scale.gridCols)
			{ citiesOk = false; std::fprintf(stderr, "  ville hors carte : %s\n", c.id.c_str()); continue; }
			const char code = m.gridRows[static_cast<size_t>(r)][static_cast<size_t>(col)];
			if (code == ' ' || code == '-') { citiesOk = false; std::fprintf(stderr, "  ville en mer : %s\n", c.id.c_str()); }
		}
		Check(citiesOk, "master : villes dans la carte du lore et hors de la mer");
		Check(m.cities.size() >= 80, "master : villes du lore integrees (v1.0.52)");
	}

	/// Seconde carte : master des Enfers (carré 3×3 de 9 cellules depuis la v1.0.71 :
	/// croix volcanique et 4 coins montagneux ; départ des démons, portails appariés
	/// aux 5 Portes des enfers du continent, tous sur la croix).
	void TestMasterEnfers()
	{
		namespace wc = engine::world::cells;
		tools::world_gen::WorldMaster m, royaumes;
		std::string err;
		const bool ok = tools::world_gen::LoadWorldMaster(LCDLLN_CONTENT_DIR, "enfers", m, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "master enfers : chargement et validation");
		if (!ok) return;
		Check(m.cellMap == wc::CellMap::Enfers && !m.connectStartCities, "master enfers : carte des Enfers, sans corridor");
		const std::set<std::string> cross{ "N000_E000", "N001_E000", "N000_E001", "S001_E000", "N000_W001" };
		const std::set<std::string> corners{ "N001_W001", "N001_E001", "S001_W001", "S001_E001" };
		const std::set<std::string> generated(m.generatedCells.begin(), m.generatedCells.end());
		std::set<std::string> square(cross);
		square.insert(corners.begin(), corners.end());
		Check(generated == square && m.generatedCells.size() == 9u, "master enfers : 9 cellules (carre 3x3)");
		// Croix volcanique, coins en chaîne de montagnes (v1.0.71), le tout dans la grille.
		bool inGrid = true, biomes = true;
		for (const std::string& id : m.generatedCells)
		{
			int n = 0, e = 0;
			inGrid = inGrid && tools::world_gen::ParseCellId(id, n, e) && wc::IsInLoreGrid({ n, e }, wc::CellMap::Enfers);
			biomes = biomes && tools::world_gen::LoreCode(m, n, e) == (cross.count(id) ? 'V' : 'M');
		}
		Check(inGrid && biomes, "master enfers : cellules dans la grille, croix volcanique et coins montagneux");
		const tools::world_gen::CityDef* citadel = nullptr;
		for (const auto& c : m.cities)
			if (std::find(c.startFor.begin(), c.startFor.end(), "legion") != c.startFor.end()) citadel = &c;
		Check(citadel && citadel->cell == "N000_E000" && citadel->kind == "camp" && citadel->race == "demons",
			"master enfers : depart de la Legion infernale (demons) a la Citadelle (centre)");
		bool heralds = false, succube = false;
		for (const auto& i : m.interactables)
		{
			heralds = heralds || (i.dialogueId == "demon_heraut" && citadel && i.cityId == citadel->id);
			succube = succube || (i.dialogueId == "demon_ysviel" && citadel && i.cityId == citadel->id);
		}
		Check(heralds && succube, "master enfers : Maltheus et Ysviel a la Citadelle");
		// Appariement 1-1 avec les Portes du continent.
		Check(tools::world_gen::LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", royaumes, err), "master royaumes recharge");
		size_t paired = 0, portals = 0;
		for (const auto& p : m.pois)
		{
			if (p.kind != "portail") continue;
			++portals;
			for (const auto& q : royaumes.pois)
				if ("royaumes:" + q.id == p.link && q.link == "enfers:" + p.id && cross.count(p.cell)) ++paired;
		}
		Check(portals == 5 && paired == 5, "master enfers : 5 portails apparies aux portes du continent");
		// v1.0.71 : la Citadelle reste la seule ville (pas de hameau) ; des POI dans les 4 coins.
		size_t cornersWithPoi = 0;
		for (const std::string& c : corners)
			if (std::any_of(m.pois.begin(), m.pois.end(), [&](const auto& p) { return p.cell == c && p.kind != "portail"; }))
				++cornersWithPoi;
		Check(m.cities.size() == 1u && cornersWithPoi == 4u, "master enfers : une seule ville, un POI dans chaque coin");
	}

	/// v1.0.83 — palette infernale (T15-22) des 9 cellules générées dans \p out :
	/// splat sans canal herbe (R) ni neige (A), masque d'herbe et densité forestière
	/// nuls, décor sans broussaille, fleur, herbe ni arbre vivant ; carte du monde
	/// avec de la lave jaune-orange (mer de lave de N000_W001) et sans vert.
	void TestPaletteEnfersV1083(const std::filesystem::path& out)
	{
		bool noGrassSnow = true, noGrassMask = true, noForest = true, noGreenProps = true, rockAndDirt = true;
		for (const char* c : { "n000_e000", "n001_e000", "n000_e001", "s001_e000", "n000_w001",
			"n001_w001", "n001_e001", "s001_w001", "s001_e001" })
		{
			const std::string cell = std::string("enfers_") + c;
			const std::vector<uint8_t> splat = ReadBytes(out / "zones" / cell / "terrain_splat.slap");
			uint64_t dirt = 0, rock = 0;
			noGrassSnow = noGrassSnow && splat.size() > 12;
			for (size_t k = 12; k + 3 < splat.size(); k += 4)
			{
				if (splat[k] != 0 || splat[k + 3] != 0) { noGrassSnow = false; break; }
				dirt += splat[k + 1]; rock += splat[k + 2];
			}
			rockAndDirt = rockAndDirt && dirt > 0 && rock > 0;
			const std::vector<uint8_t> grass = ReadBytes(out / "zones" / cell / "terrain_grass.grms");
			noGrassMask = noGrassMask && grass.size() > 12
				&& std::all_of(grass.begin() + 12, grass.end(), [](uint8_t v) { return v == 0; });
			engine::world::instances::ForestMask fm; std::string e;
			noForest = noForest && engine::world::instances::LoadForestMask(ReadBytes(out / "zones" / cell / "terrain_forest.frms"), fm, e)
				&& std::all_of(fm.density.begin(), fm.density.end(), [](uint8_t v) { return v == 0; });
			engine::world::instances::ScatterFile sf;
			const bool okS = engine::world::instances::LoadScatterBin(ReadBytes(out / "instances" / ("zone_" + cell) / "scatter.bin"), sf, e);
			noGreenProps = noGreenProps && okS;
			for (const std::string& m : sf.meshPaths)
				for (const char* green : { "Bush_", "Fern_", "Flower_", "Grass_", "Clover_", "Mushroom_", "Plant_1", "Pine_", "CommonTree_", "TwistedTree_" })
					if (m.find(green) != std::string::npos) noGreenProps = false;
		}
		Check(noGrassSnow && rockAndDirt, "palette v1.0.83 : splat des Enfers sans herbe ni neige (terre et roche)");
		Check(noGrassMask && noForest, "palette v1.0.83 : Enfers sans masque d'herbe ni foret");
		Check(noGreenProps, "palette v1.0.83 : decor des Enfers sans broussaille, herbe ni arbre vivant");
		// Carte du monde : lave jaune-orange dans la mer de lave, aucun pixel vert dans le carré 3×3.
		{
			tools::zone_builder::JsonValue root; std::string e;
			const bool okJ = tools::zone_builder::ParseJsonDocument(ReadText(out / "world/enfers/places.json"), root, e);
			const auto* t = okJ ? tools::zone_builder::FindObjectMember(root, "terrain") : nullptr;
			const std::vector<uint8_t> png = ReadBytes(out / "world/enfers/world_map.png");
			int w = 0, h = 0, n = 0;
			unsigned char* px = png.empty() ? nullptr : stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n, 3);
			size_t lavaPx = 0, green = 0;
			bool merDeLave = false;
			if (px && t)
			{
				const double ox = tools::zone_builder::FindObjectMember(*t, "origin_x")->numberValue;
				const double oz = tools::zone_builder::FindObjectMember(*t, "origin_z")->numberValue;
				const double size = tools::zone_builder::FindObjectMember(*t, "size_m")->numberValue;
				auto at = [&](double x, double z) {
					const int i = std::clamp(static_cast<int>((x - ox) / size * w), 0, w - 1);
					const int j = std::clamp(static_cast<int>((z - oz) / size * h), 0, h - 1);
					return px + (static_cast<size_t>(j) * w + i) * 3;
				};
				for (double z = -14950.0; z < 15000.0; z += 100.0)
					for (double x = -14950.0; x < 15000.0; x += 100.0)
					{
						const unsigned char* c = at(x, z);
						if (c[0] > 200 && c[1] > 70 && c[2] < 90) ++lavaPx;
						if (c[1] > c[0]) ++green;
					}
				// Mer de lave : N000_W001, décalage (−2 750, −1 250) depuis le centre de la case.
				const unsigned char* c = at(-12750.0, -1250.0);
				merDeLave = c[0] > 200 && c[1] > 70 && c[2] < 90;
			}
			if (px) stbi_image_free(px);
			Check(merDeLave && lavaPx > 300, "palette v1.0.83 : carte des Enfers avec mer et fleuves de lave jaune-orange");
			Check(px && green == 0, "palette v1.0.83 : carte des Enfers sans vert (ni herbe ni foret)");
		}
	}

	/// Seconde carte : génération complète des Enfers (9 cellules) → dossiers et
	/// identifiants propres à la carte, sorties globales sous world/enfers/.
	void TestGenerationEnfers()
	{
		namespace fs = std::filesystem;
		namespace wc = engine::world::cells;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_tests_enfers";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		tools::world_gen::GenOptions o;
		o.contentRoot = LCDLLN_CONTENT_DIR;
		o.worldId = "enfers";
		o.outputRoot = tmp.string();
		tools::world_gen::GenReport r;
		std::string err;
		const bool ok = tools::world_gen::RunWorldGen(o, r, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok && r.cells == 9, "enfers : generation des 9 cellules");
		if (!ok) return;
		Check(fs::exists(tmp / "zones/enfers_n000_e000/terrain_height.r16h") && !fs::exists(tmp / "zones/cell_n000_e000"),
			"enfers : dossiers zones/enfers_* (pas de cell_*)");
		// Spawners : zoneId de la cellule DANS la plage des Enfers ; gardes de la Légion.
		{
			tools::zone_builder::JsonValue root; std::string e;
			const bool okJ = tools::zone_builder::ParseJsonDocument(ReadText(tmp / "zones/enfers_n000_e000/spawners.json"), root, e);
			const auto* zid = okJ ? tools::zone_builder::FindObjectMember(root, "zoneId") : nullptr;
			Check(zid && static_cast<uint32_t>(zid->numberValue) == wc::CellZoneId({ 0, 0 }, wc::CellMap::Enfers)
				&& wc::MapIdOfZone(static_cast<uint32_t>(zid->numberValue)) == wc::kEnfersMapId,
				"enfers : spawners avec l'identifiant de zone des Enfers");
			const std::string text = ReadText(tmp / "zones/enfers_n000_e000/spawners.json");
			Check(text.find("\"archetypeId\": 241") != std::string::npos, "enfers : legionnaires a la Citadelle");
		}
		// v1.0.71 : faune de la tranche débutant (1 à 10) dans les 9 cellules, aucun
		// élémentaire de braise (164, niveau 12) ; diablotins et chiens des cendres présents.
		{
			bool noEmber = true, hasImp = false;
			for (const char* c : { "n000_e000", "n001_e000", "n000_e001", "s001_e000", "n000_w001",
				"n001_w001", "n001_e001", "s001_w001", "s001_e001" })
			{
				const std::string text = ReadText(tmp / ("zones/enfers_" + std::string(c) + "/spawners.json"));
				noEmber = noEmber && text.find("\"archetypeId\": 164,") == std::string::npos;
				hasImp = hasImp || text.find("\"archetypeId\": 150,") != std::string::npos
					|| text.find("\"archetypeId\": 151,") != std::string::npos;
			}
			Check(noEmber && hasImp, "enfers : faune de niveau 1 a 10 (pas d'elementaire de braise)");
		}
		// Cimetière neutre (toutes factions) à la Citadelle.
		{
			const std::string text = ReadText(tmp / "zones/enfers_n000_e000/respawn_points.txt");
			const std::string line = std::to_string(wc::CellZoneId({ 0, 0 }, wc::CellMap::Enfers)) + " graveyard ";
			const size_t at = text.find(line);
			const size_t eol = at == std::string::npos ? std::string::npos : text.find('\n', at);
			Check(at != std::string::npos && eol != std::string::npos && text[eol - 1] == '-',
				"enfers : cimetiere neutre a la Citadelle");
		}
		// Sorties globales de la carte.
		{
			const std::string starts = ReadText(tmp / "world/enfers/start_locations.json");
			Check(starts.find("\"world\": \"enfers\"") != std::string::npos && starts.find("\"demons\": {") != std::string::npos
				&& starts.find("\"start_factions\": \"legion\"") != std::string::npos && starts.find("\"legion\": {") != std::string::npos
				&& starts.find("\"race\": \"demons\"") != std::string::npos
				&& starts.find("enfers_n000_e000") != std::string::npos && starts.find("\"humains\": {") == std::string::npos,
				"enfers : start_locations.json (depart de la faction legion et repli demons, carte enfers)");
			tools::zone_builder::JsonValue root; std::string e;
			const bool okJ = tools::zone_builder::ParseJsonDocument(ReadText(tmp / "world/enfers/portals.json"), root, e);
			const auto* list = okJ ? tools::zone_builder::FindObjectMember(root, "portals") : nullptr;
			bool linked = list && list->type == tools::zone_builder::JsonType::Array && list->arrayValue.size() == 5u;
			for (size_t k = 0; linked && k < list->arrayValue.size(); ++k)
			{
				const auto* link = tools::zone_builder::FindObjectMember(list->arrayValue[k], "link");
				const auto* y = tools::zone_builder::FindObjectMember(list->arrayValue[k], "y");
				linked = link && link->stringValue.rfind("royaumes:poi_porte_", 0) == 0 && y && y->numberValue > 40.0;
			}
			Check(linked && r.portals == 5, "enfers : portals.json (5 portails relies, altitude du sol)");
			Check(fs::exists(tmp / "world/enfers/places.json") && fs::exists(tmp / "world/enfers/world_map.png"),
				"enfers : places.json et carte du monde");
			// Coins montagneux (v1.0.71) : générés, mines tenues par la Légion (pas de nains aux Enfers).
			const std::string places = ReadText(tmp / "world/enfers/places.json");
			Check(fs::exists(tmp / "zones/enfers_s001_e001/terrain_height.r16h")
				&& places.find("enfers_n001_w001_mine_") != std::string::npos
				&& places.find("\"race\": \"nains\"") == std::string::npos,
				"enfers : coins generes, mines de la Legion (aucun lieu nain)");
		}
		TestPaletteEnfersV1083(tmp);
		fs::remove_all(tmp, ec);
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
			CityDef c; c.id = id; if (start) c.startFor = { std::string("faction_") + id }; g.cities.push_back(c);
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
		// v1.0.83 : la palette infernale ne touche pas les Royaumes (herbe, masque
		// d'herbe et neige possibles, pas de lave).
		{
			tools::world_gen::WorldMaster rm; std::string e;
			const bool okM = tools::world_gen::LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", rm, e);
			bool noLava = okM && !tools::world_gen::IsInfernalMap(rm);
			for (double z = -35000.0; noLava && z < -25000.0; z += 250.0)
				for (double x = -95000.0; x < -85000.0; x += 250.0)
					if (tools::world_gen::InfernalLavaAt(rm, x, z) != 0.0) { noLava = false; break; }
			const std::vector<uint8_t> splat = ReadBytes(tmp / "a" / "zones/cell_n003_w009/terrain_splat.slap");
			const std::vector<uint8_t> grass = ReadBytes(tmp / "a" / "zones/cell_n003_w009/terrain_grass.grms");
			uint64_t g = 0, gm = 0;
			for (size_t k = 12; k + 3 < splat.size(); k += 4) g += splat[k];
			for (size_t k = 12; k < grass.size(); ++k) gm += grass[k];
			Check(noLava && g > 0 && gm > 0, "palette v1.0.83 : Royaumes inchanges (herbe et masque d'herbe, sans lave)");
		}

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
		// v1.0.61 : le hameau de Marn n'est plus un départ (départs par faction) : pas
		// de spawn par défaut dans sa cellule ; --cells ne réécrit pas les sorties globales.
		{
			Check(!fs::exists(out / "world/royaumes/start_locations.json"), "--cells : sorties globales non reecrites");
			tools::zone_builder::JsonValue root; std::string e;
			const bool ok = tools::zone_builder::ParseJsonDocument(ReadText(out / "zones" / cell / "zone.json"), root, e);
			Check(ok, "zone.json : JSON valide");
			const auto* world = ok ? tools::zone_builder::FindObjectMember(root, "world") : nullptr;
			Check(world && world->type == tools::zone_builder::JsonType::Object && tools::zone_builder::FindObjectMember(*world, "dof") != nullptr,
				"zone.json : reglages world.* importes (dof, brouillard...)");
			Check(world && tools::zone_builder::FindObjectMember(*world, "default_spawn") == nullptr,
				"hameau de Marn : plus un depart (pas de default_spawn)");
			// SND.4 — grille des biomes pour les ambiances sonores.
			const auto* audio = ok ? tools::zone_builder::FindObjectMember(root, "audio") : nullptr;
			const auto* grid = audio ? tools::zone_builder::FindObjectMember(*audio, "biome_grid") : nullptr;
			Check(grid && grid->type == tools::zone_builder::JsonType::String
				&& grid->stringValue.size() == static_cast<size_t>(tools::world_gen::kBiomeGridSize * tools::world_gen::kBiomeGridSize),
				"zone.json : grille des biomes (ambiances sonores)");
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
		// v1.0.61 : départ de faction (Légions Impériales, hameau de Haeldorf, N002_W010,
		// voisin de Lhynn) : spawn par défaut de la cellule dans la cellule, au sec.
		tools::world_gen::GenReport r4;
		o.cells = { "N002_W010" };
		o.outputRoot = (tmp / "d").string();
		const bool ok4 = tools::world_gen::RunWorldGen(o, r4, err);
		if (!ok4) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok4, "generation cellule de depart N002_W010 (Haeldorf)");
		if (ok4)
		{
			const double sx0 = -105000.0, sz0 = -25000.0; // coin min de N002_W010
			tools::zone_builder::JsonValue root; std::string e;
			const bool okZ = tools::zone_builder::ParseJsonDocument(ReadText(tmp / "d" / "zones/cell_n002_w010/zone.json"), root, e);
			const auto* world = okZ ? tools::zone_builder::FindObjectMember(root, "world") : nullptr;
			const auto* sp = world ? tools::zone_builder::FindObjectMember(*world, "default_spawn") : nullptr;
			bool good = sp != nullptr;
			if (sp)
			{
				const double x = tools::zone_builder::FindObjectMember(*sp, "x")->numberValue;
				const double z = tools::zone_builder::FindObjectMember(*sp, "z")->numberValue;
				const double y = tools::zone_builder::FindObjectMember(*sp, "y")->numberValue;
				good = x >= sx0 && x < sx0 + 10000.0 && z >= sz0 && z < sz0 + 10000.0 && y > 41.0;
			}
			Check(good, "depart de faction (Haeldorf) : dans sa cellule et au sec");
		}
		fs::remove_all(tmp, ec);
	}

	/// État « reconstruction » (Kendrhir, N015_E012) : la ville mêle maisons
	/// habitées, ruines et chantiers (étais), et garde son auberge.
	void TestReconstruction()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_tests_reconstruction";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		tools::world_gen::GenReport r;
		std::string err;
		tools::world_gen::GenOptions o;
		o.contentRoot = LCDLLN_CONTENT_DIR;
		o.worldId = "royaumes";
		o.cells = { "N015_E012" };
		o.outputRoot = tmp.string();
		const bool ok = tools::world_gen::RunWorldGen(o, r, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "reconstruction : generation cellule N015_E012 (Kendrhir)");
		if (!ok) return;

		const std::string cell = "cell_n015_e012";
		std::vector<engine::world::instances::BuildingPlacement> pls; std::string e;
		Check(engine::world::instances::LoadBuildingsBin(ReadBytes(tmp / "instances" / ("zone_" + cell) / "buildings.bin"), pls, e),
			"reconstruction : buildings.bin relu");
		const std::string suffix = "(Kendrhir)";
		int intactHouses = 0, ruins = 0, sites = 0, inns = 0;
		for (const auto& p : pls)
		{
			if (p.displayName.find(suffix) == std::string::npos) continue;
			const bool ruinVariant = p.variantId.rfind("ruine_", 0) == 0;
			if (p.templateType == "tavern") ++inns;
			else if (p.displayName.rfind("Chantier", 0) == 0 && ruinVariant) ++sites;
			else if (ruinVariant) ++ruins;
			else ++intactHouses;
		}
		Check(intactHouses > 0 && ruins > 0 && sites > 0, "reconstruction : maisons, ruines et chantiers melanges");
		Check(intactHouses > ruins && intactHouses > sites, "reconstruction : les maisons habitees restent majoritaires");
		Check(inns == 1, "reconstruction : auberge en service");

		engine::world::instances::ScatterFile sf;
		const bool okScatter = engine::world::instances::LoadScatterBin(ReadBytes(tmp / "instances" / ("zone_" + cell) / "scatter.bin"), sf, e);
		bool supports = false;
		for (const std::string& m : sf.meshPaths)
			if (m == "meshes/props/Prop_Support.gltf") supports = true;
		Check(okScatter && supports, "reconstruction : etais poses autour des chantiers");
		// v1.0.52 : champs cultives aussi autour des villes (Kendrhir est une ville).
		bool crops = false;
		for (const std::string& m : sf.meshPaths)
			if (m == "meshes/props/Carrot.gltf" || m == "meshes/props/Plant_7.gltf") crops = true;
		Check(okScatter && crops, "champs : cultures autour d'une ville");
		// FORET.1 — densité forestière écrite (Kendrhir est en forêt) et
		// cadrée sur la cellule ; plus aucun arbre de forêt dans scatter.bin.
		engine::world::instances::ForestMask fm;
		const bool okForest = engine::world::instances::LoadForestMask(
			ReadBytes(tmp / "zones" / cell / "terrain_forest.frms"), fm, e);
		int dense = 0;
		for (uint8_t v : fm.density) if (v >= 200) ++dense;
		Check(okForest && fm.width == engine::world::instances::kForestMaskResolution && fm.sizeM > 9000.0
			&& dense > 1000, "foret : carte de densite forestiere (pleine foret presente)");
		bool zoneKey = false;
		{
			std::ifstream z(tmp / "zones" / cell / "zone.json");
			std::stringstream ss; ss << z.rdbuf();
			zoneKey = ss.str().find("terrain_forest.frms") != std::string::npos;
		}
		Check(zoneKey, "foret : zone.json reference la densite forestiere");
		fs::remove_all(tmp, ec);
	}

	/// FIX.12 — tracés de la carte : routes globales, chemins locaux, rues
	/// (une arête par paire de noeuds) et bâtiments (emprise ou repli du rayon).
	void TestMapFeatures()
	{
		using namespace tools::world_gen;
		namespace mf = engine::world::mapfeatures;
		GlobalWorld g;
		Road major;
		major.major = true;
		major.widthM = 6.0;
		for (int i = 0; i <= 100; ++i) major.points.push_back({ static_cast<double>(i), 0.0 });
		g.roads.push_back(major);
		World w;
		Road clipped; // découpe d'une route globale : ne doit pas être réexportée
		clipped.points = { { 0.0, 0.0 }, { 50.0, 0.0 } };
		w.roads.push_back(clipped);
		Road path;
		path.path = true;
		path.points = { { 0.0, 10.0 }, { 30.0, 10.0 } };
		w.roads.push_back(path);
		SettlementLayout L;
		L.nodes = { { { 0.0, 0.0 }, { 1, 2 } }, { { 20.0, 0.0 }, { 0 } }, { { 0.0, 20.0 }, { 0 } } };
		w.layouts.push_back(L);
		BuildingPlacementDef house;
		house.position = { 5.0, 6.0 };
		house.yawDeg = 90.0;
		house.halfW = 2.0;
		house.halfD = 3.0;
		w.buildings.push_back(house);
		BuildingPlacementDef legacy; // historique : pas d'emprise, rayon 7
		legacy.footprintRadius = 7.0;
		w.buildings.push_back(legacy);
		// FIX.15 — une rivière, un lac du lore recopié dans la cellule (une seule
		// fois sur la carte) et un étang de cellule sans contour (cercle).
		River river;
		for (int i = 0; i <= 50; ++i) river.points.push_back({ static_cast<double>(i), 40.0 });
		river.widthM = { 4.0, 8.0, 12.0 };
		g.rivers.push_back(river);
		Lake lore;
		lore.name = "lac_du_lore";
		lore.center = { 100.0, 100.0 };
		lore.polygon = { { 90.0, 90.0 }, { 110.0, 90.0 }, { 110.0, 110.0 }, { 90.0, 110.0 } };
		g.lakes.push_back(lore);
		w.lakes.push_back(lore);
		Lake pond;
		pond.name = "etang";
		pond.center = { 200.0, 0.0 };
		pond.radiusM = 15.0;
		w.lakes.push_back(pond);
		const std::vector<World*> cells{ &w };
		const mf::MapFeatures f = BuildMapFeatures(g, cells);
		Check(f.roads.size() == 4u, "carte : 1 route + 1 chemin + 2 rues (decoupe ignoree, aretes uniques)");
		if (f.roads.size() == 4u)
		{
			Check(f.roads[0].kind == mf::MapRoadKind::Major && f.roads[0].PointCount() == 11u, "carte : route majeure allegee (pas 10 m)");
			Check(f.roads[1].kind == mf::MapRoadKind::Path, "carte : chemin local");
			Check(f.roads[2].kind == mf::MapRoadKind::Street && f.roads[3].kind == mf::MapRoadKind::Street, "carte : rues");
		}
		Check(f.buildings.size() == 2u, "carte : 2 batiments");
		if (f.buildings.size() == 2u)
		{
			Check(std::fabs(f.buildings[0].yawRad - 1.5707963f) < 1e-4f && f.buildings[0].halfD == 3.0f, "carte : emprise et cap du batiment");
			Check(std::fabs(f.buildings[1].halfW - 4.95f) < 0.01f, "carte : repli carre inscrit dans le rayon");
		}
		Check(f.waters.size() == 3u, "carte : 1 riviere + 2 lacs (lac du lore exporte une fois)");
		if (f.waters.size() == 3u)
		{
			Check(f.waters[0].kind == mf::MapWaterKind::River && f.waters[0].widthM == 8.0f, "carte : riviere, largeur mediane");
			Check(f.waters[1].kind == mf::MapWaterKind::Lake && f.waters[1].PointCount() == 4u, "carte : contour du lac du lore");
			Check(f.waters[2].kind == mf::MapWaterKind::Lake && f.waters[2].PointCount() == 24u, "carte : etang sans contour = cercle");
		}
	}
}

namespace
{
	/// v1.0.55 : fleuve de Sysigie (cours tracé à la main, île fluviale) et POI
	/// partageant une case (temple et Seuil de Lhynn) — couche macro seule.
	void TestRiverIslandAndSharedPois()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok && !g.master.riverCourses.empty(), "fleuve : river_courses charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		const CityDef* sys = g.FindCity("ville_sysigie");
		const River* mainArm = nullptr;
		const River* arm = nullptr;
		for (const River& r : g.rivers)
		{
			if (r.name == "fleuve_sysigie") mainArm = &r;
			if (r.name == "fleuve_sysigie_bras") arm = &r;
		}
		Check(sys && mainArm && arm, "fleuve : lit principal et second bras autour de Sysigie");
		if (!sys || !mainArm || !arm) return;
		// Angle balayé autour de la ville par chaque bras : ~+pi d'un côté, ~-pi de
		// l'autre ; leur différence ~2 pi = la ville est encerclée par l'eau.
		auto sweep = [&](const River& r) {
			double s = 0.0;
			for (size_t k = 1; k < r.points.size(); ++k)
			{
				const double a0 = std::atan2(r.points[k - 1].z - sys->center.z, r.points[k - 1].x - sys->center.x);
				const double a1 = std::atan2(r.points[k].z - sys->center.z, r.points[k].x - sys->center.x);
				double d = a1 - a0;
				while (d > 3.141592653589793) d -= 6.283185307179586;
				while (d < -3.141592653589793) d += 6.283185307179586;
				s += d;
			}
			return s;
		};
		const double loop = std::fabs(sweep(*mainArm) - sweep(*arm));
		Check(std::fabs(loop - 6.283185307179586) < 0.5, "fleuve : Sysigie sur une ile (deux bras qui l'encerclent)");
		double minD = 1e18;
		for (const River* r : { mainArm, arm })
			for (const Vec2& p : r->points) minD = std::min(minD, std::hypot(p.x - sys->center.x, p.z - sys->center.z));
		Check(minD > sys->radiusM + 6.0 + std::max(46.0, sys->radiusM * 0.6) + 40.0, "fleuve : bras hors du plateau urbain");
		Check(mainArm->waterY.back() < g.master.scale.seaLevelM + 0.5 && mainArm->widthM.back() >= 60.0,
			"fleuve : se jette dans la mer, largeur de grand fleuve");
		bool monotone = true;
		for (size_t k = 1; k < mainArm->waterY.size(); ++k) monotone = monotone && mainArm->waterY[k] <= mainArm->waterY[k - 1];
		Check(monotone, "fleuve : surface non croissante vers l'aval");
		// POI d'une même case écartés (case de Lhynn : temple et Seuil).
		const PoiDef* temple = nullptr;
		const PoiDef* seuil = nullptr;
		for (const PoiDef& p : g.pois)
		{
			if (p.id == "poi_temple_methraton") temple = &p;
			if (p.id == "poi_seuil_lhynn") seuil = &p;
		}
		Check(temple && seuil && temple->cell == seuil->cell
			&& std::hypot(temple->center.x - seuil->center.x, temple->center.z - seuil->center.z) >= 300.0,
			"POI : deux lieux de la meme case ecartes (temple et Seuil de Lhynn)");
	}

	/// v1.0.72 : reliefs locaux du master — Vaerdhun au sommet d'une colline, deux
	/// rivières qui naissent à son pied ; Vharholdt au fond d'une crevasse en
	/// cul-de-sac (routes par l'ouverture seulement, mine au fond) ; lieu décalé
	/// contre la côte ouest de sa case.
	void TestReliefs()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok && g.master.reliefs.size() >= 2, "reliefs : section reliefs chargee");
		if (!ok) return;
		const WorldMaster& m = g.master;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const CityDef* ver = g.FindCity("town_0119");
		const CityDef* war = g.FindCity("hamlet_wardholt");
		const ReliefDef* hill = ReliefOfCity(m, "town_0119");
		const ReliefDef* crev = ReliefOfCity(m, "hamlet_wardholt");
		Check(ver && war && hill && crev && hill->type == "colline" && crev->type == "crevasse",
			"reliefs : colline de Vaerdhun, crevasse de Vharholdt");
		if (!ver || !war || !hill || !crev) return;
		auto dist = [](Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); };
		// Colline : ville au centre de sa case, au sommet, nettement au-dessus de son pied.
		Check(dist(ver->center, hill->center) < 1e-6 && dist(war->center, crev->center) < 1e-6,
			"reliefs : villes ancrees au centre de leur case");
		const double top = MacroHeight(m, ver->center.x, ver->center.z);
		bool dominates = true;
		for (int k = 0; k < 8; ++k)
		{
			const double a = k * 0.7853981633974483, r = hill->footRadiusM + 300.0;
			dominates = dominates && top - MacroHeight(m, ver->center.x + std::cos(a) * r, ver->center.z + std::sin(a) * r) > 80.0;
		}
		Check(dominates && ver->plateauY > top - 15.0, "reliefs : Vaerdhun au sommet (plus de 80 m au-dessus du pied)");
		// Deux rivières tracées à la main : nées au pied (jamais sur le flanc), jusqu'à la mer.
		int born = 0;
		for (const River& r : g.rivers)
		{
			if (r.name != "riviere_verdansur_sud" && r.name != "riviere_verdansur_nord") continue;
			double dmin = 1e18;
			for (const Vec2& p : r.points) dmin = std::min(dmin, dist(p, ver->center));
			if (dmin >= hill->footRadiusM && dist(r.points.front(), ver->center) < hill->footRadiusM + 600.0
				&& r.waterY.back() < m.scale.seaLevelM + 0.5)
				++born;
		}
		Check(born == 2, "reliefs : deux rivieres naissent au pied de la colline et vont a la mer");
		// Crevasse : fond plat sous la ville et la mine, parois sur les côtés et au fond,
		// ouverte vers Ordhrann.
		const Vec2 c = war->center, d = crev->dir, nrm{ -d.z, d.x };
		const double off = crev->halfWidthM + crev->wallM + 80.0;
		const double H = crev->heightM;
		auto at = [&](Vec2 p) { return ReliefContribution(*crev, p.x, p.z); };
		Check(at(c) < 1e-9 && at(c - d * (crev->backM - 60.0)) < 1e-9 && at(c + nrm * off) > 0.9 * H && at(c - nrm * off) > 0.9 * H
			&& at(c - d * (crev->backM + off)) > 0.9 * H && at(c + d * (crev->footRadiusM * 0.6)) < 1e-9
			&& at(c + d * (crev->footRadiusM * 1.16 + CrevasseOuterSlopeM(*crev) + 50.0)) < 1e-9,
			"reliefs : Vharholdt au fond d'une crevasse en cul-de-sac");
		// Routes : aucune ne franchit le massif ; celles de Vharholdt sortent par l'ouverture.
		bool walled = true;
		size_t wardRoads = 0;
		for (const Road& r : g.roads)
		{
			if (r.fromId == war->id || r.toId == war->id) ++wardRoads;
			for (const Vec2& p : r.points) walled = walled && at(p) < 0.5 * H;
		}
		Check(walled && wardRoads >= 1, "reliefs : routes de Vharholdt par l'ouverture, massif jamais franchi");
		// Mine au fond de la crevasse (lieu de la cellule de Vharholdt).
		World w;
		InitCell(w, g, { war->n, war->e });
		BuildPlaces(w);
		const CityDef* mine = nullptr;
		for (const CityDef& p : w.cities)
			if (p.procedural == "mine" && p.id == w.cellName + "_" + crev->id + "_mine") mine = &p;
		Check(mine && mine->name == crev->mine && at(mine->center) < 1e-9
			&& dist(mine->center, c) > war->radiusM + 6.0 + std::max(46.0, war->radiusM * 0.6) + mine->radiusM,
			"reliefs : mine au fond de la crevasse, hors du plateau du hameau");
		// Lieu décalé (Sanctuaire des Brisants) : contre la côte ouest de sa case, au sec.
		const PoiDef* poi = nullptr;
		for (const PoiDef& p : g.pois)
			if (p.id == "poi_sanctuaire_des_brisants") poi = &p;
		const auto pc = poi ? engine::world::cells::CellCenter({ poi->n, poi->e }) : engine::world::cells::CellPoint{};
		Check(poi && poi->offsetX < -3000.0 && std::fabs(poi->center.x - (pc.x + poi->offsetX)) < 1e-6
			&& MacroHeight(m, poi->center.x, poi->center.z) > m.scale.seaLevelM + 3.0,
			"reliefs : POI decale contre la cote ouest de sa case, au sec");
	}

	/// v1.0.73 : trois villes moyennes (Hazhaëlin, Fhelvakt, Kholvard) sur des
	/// cellules générées, sites secs et reliés au réseau ; deux entrées de la Mine
	/// de la Traverse (galerie sous la chaîne N000_W008), décalées vers la chaîne,
	/// au sec et à l'écart des villes.
	void TestVillesV1073()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "villes v1.0.73 : master charge");
		if (!ok) return;
		const WorldMaster& m = g.master;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		auto dist = [](Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); };
		auto generated = [&](const std::string& cell) {
			return std::find(m.generatedCells.begin(), m.generatedCells.end(), cell) != m.generatedCells.end();
		};
		// Distance (m) du point à la rivière macro la plus proche.
		auto riverDist = [&](Vec2 p) {
			double best = 1e18;
			for (const River& r : g.rivers)
				for (const Vec2& q : r.points) best = std::min(best, dist(p, q));
			return best;
		};
		const std::pair<const char*, const char*> towns[] = {
			{ "town_hazelgate", "N007_E002" }, { "town_fellwatch", "N009_W001" }, { "town_hollowgate", "S001_W007" } };
		for (const auto& [id, cell] : towns)
		{
			const CityDef* c = g.FindCity(id);
			int roads = 0;
			for (const Road& r : g.roads)
				if (r.fromId == id || r.toId == id) ++roads;
			Check(c && c->kind == "ville" && c->cell == cell && generated(cell) && c->startFor.empty()
				&& MacroHeight(m, c->center.x, c->center.z) > m.scale.seaLevelM + 3.0
				&& riverDist(c->center) > c->radiusM + 200.0 && roads >= 1,
				(std::string("villes v1.0.73 : ") + id + " ville moyenne sur cellule generee, au sec, reliee").c_str());
		}
		// Entrées de la mine : nord dans la case de Gredhaim (sud de la case), sud dans
		// celle de Kholvard (coin nord-ouest), toutes deux vers la chaîne N000_W008.
		const CityDef* grey = g.FindCity("hamlet_greyhedge");
		const CityDef* hollow = g.FindCity("town_hollowgate");
		const PoiDef* north = nullptr;
		const PoiDef* south = nullptr;
		for (const PoiDef& p : g.pois)
		{
			if (p.id == "poi_mine_traverse_nord") north = &p;
			if (p.id == "poi_mine_traverse_sud") south = &p;
		}
		Check(grey && hollow && north && south && north->cell == grey->cell && south->cell == hollow->cell
			&& north->kind == "salle" && south->kind == "salle",
			"villes v1.0.73 : deux entrees de la Mine de la Traverse (salle) dans les cases de Gredhaim et Kholvard");
		if (!grey || !hollow || !north || !south) return;
		const auto chain = engine::world::cells::CellCenter({ 0, -8 });
		const Vec2 chainCenter{ chain.x, chain.z };
		Check(dist(north->center, chainCenter) < dist(grey->center, chainCenter) - 2000.0
			&& dist(south->center, chainCenter) < dist(hollow->center, chainCenter) - 2000.0,
			"villes v1.0.73 : entrees decalees vers la chaine N000_W008");
		Check(MacroHeight(m, north->center.x, north->center.z) > m.scale.seaLevelM + 3.0
			&& MacroHeight(m, south->center.x, south->center.z) > m.scale.seaLevelM + 3.0
			&& dist(north->center, grey->center) > 2000.0 && dist(south->center, hollow->center) > 2000.0,
			"villes v1.0.73 : entrees au sec, a l'ecart des villes");
	}

	/// v1.0.76 : corrections du propriétaire (lot 5). Der Hem Shelbem descendue en
	/// S010_E010, Bhridrak (S004_W006) et Krakhol (S004_W002) déplacés hors de la
	/// région 3×3 de Drakhenhal : cellules générées, au sec, reliées par le générateur.
	/// Colonne E020 : désert de S001 à S003, terres arides (zone rocheuse) en S004.
	void TestCorrectionsV1076()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "corrections v1.0.76 : master charge");
		if (!ok) return;
		const WorldMaster& m = g.master;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		auto generated = [&](const std::string& cell) {
			return std::find(m.generatedCells.begin(), m.generatedCells.end(), cell) != m.generatedCells.end();
		};
		const std::pair<const char*, const char*> moved[] = {
			{ "ville_der_hem_shelsem", "S010_E010" }, { "hamlet_bridlecrag", "S004_W006" }, { "hamlet_cragfold", "S004_W002" } };
		for (const auto& [id, cell] : moved)
		{
			const CityDef* c = g.FindCity(id);
			int roads = 0;
			for (const Road& r : g.roads)
				if (r.fromId == id || r.toId == id) ++roads;
			Check(c && c->cell == cell && generated(cell) && MacroHeight(m, c->center.x, c->center.z) > m.scale.seaLevelM + 3.0
				&& roads >= 1,
				(std::string("corrections v1.0.76 : ") + id + " en " + cell + ", cellule generee, au sec, reliee").c_str());
		}
		// Codes de la grille : rangée N - n, colonne e - W.
		auto code = [&](int n, int e) { return m.gridRows[static_cast<size_t>(m.scale.northN - n)][static_cast<size_t>(e - m.scale.westE)]; };
		Check(code(-1, 20) == '~' && code(-2, 20) == '~' && code(-3, 20) == '~' && code(-4, 20) == ':',
			"corrections v1.0.76 : S001_E020 a S003_E020 en desert, S004_E020 en terres arides (zone rocheuse)");
	}

	/// v1.0.74 : île du Nord (Île de la Trêve). Terre émergée sur les 20 cases de forêt
	/// isolées en mer, rivage entièrement dans les cellules générées (forêt + haut-fond),
	/// îlot central séparé par la baie, quatre villes au sec, ports au bord de l'eau,
	/// aucune route du générateur entre l'île et le continent ni vers l'îlot.
	void TestIleV1074()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "ile v1.0.74 : master charge");
		if (!ok) return;
		const WorldMaster& m = g.master;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const double sea = m.scale.seaLevelM;
		auto cellId = [](int n, int e) {
			char b[16];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		auto generated = [&](const std::string& cell) {
			return std::find(m.generatedCells.begin(), m.generatedCells.end(), cell) != m.generatedCells.end();
		};
		bool region = false;
		for (const RegionDef& r : m.regions)
			region = region || (r.id == "ile_de_la_treve" && r.eMin == -15 && r.eMax == -9 && r.nMin == 16 && r.nMax == 23);
		Check(region, "ile v1.0.74 : region ile_de_la_treve (W015-W009, N016-N023)");
		// Cases de l'île : forêt émergée, haut-fond avec de l'eau, toutes générées.
		int forest = 0, shallow = 0, forestOk = 0, shallowOk = 0;
		for (int n = 15; n <= 23; ++n)
			for (int e = -16; e <= -8; ++e)
			{
				const char code = LoreCode(m, n, e);
				const auto c = engine::world::cells::CellCenter({ n, e });
				double hi = -1e9, lo = 1e9;
				for (int dz = -4000; dz <= 4000; dz += 1000)
					for (int dx = -4000; dx <= 4000; dx += 1000)
					{
						const double h = MacroHeight(m, c.x + dx, c.z + dz);
						hi = std::max(hi, h);
						lo = std::min(lo, h);
					}
				if (code == 'T') { ++forest; forestOk += generated(cellId(n, e)) && hi > sea + 10.0; }
				if (code == '-') { ++shallow; shallowOk += generated(cellId(n, e)) && lo < sea; }
			}
		Check(forest == 20 && forestOk == 20, "ile v1.0.74 : 20 cases de foret generees, terre emergee dans chacune");
		Check(shallow == 34 && shallowOk == 34, "ile v1.0.74 : 34 cases de haut-fond generees, avec de l'eau");
		// Le rivage ne sort pas des cellules générées : pas d'île coupée net à un mur.
		int landOutside = 0;
		for (double z = -235000.0; z <= -145000.0; z += 250.0)
			for (double x = -165000.0; x <= -75000.0; x += 250.0)
				if (MacroHeight(m, x, z) >= sea)
				{
					const int e = static_cast<int>(std::lround(x / 10000.0)), n = -static_cast<int>(std::lround(z / 10000.0));
					if (!generated(cellId(n, e))) ++landOutside;
				}
		Check(landOutside == 0, "ile v1.0.74 : toute la terre de l'ile est dans les cellules generees (rivage compris)");
		// Villes au sec ; îlot séparé de Brenhaval et du bras ouest par la baie.
		const std::pair<const char*, const char*> towns[] = { { "town_brinehaven", "N017_W010" }, { "town_duskmoor", "N022_W010" },
			{ "town_truceholm", "N019_W010" }, { "town_lanternhold", "N021_W014" } };
		for (const auto& [id, cell] : towns)
		{
			const CityDef* c = g.FindCity(id);
			Check(c && c->cell == cell && generated(cell) && c->startFor.empty() && MacroHeight(m, c->center.x, c->center.z) > sea + 3.0,
				(std::string("ile v1.0.74 : ") + id + " sur cellule generee, au sec").c_str());
		}
		auto crossesWater = [&](Vec2 a, Vec2 b) {
			for (int k = 1; k < 200; ++k)
			{
				const Vec2 p = a + (b - a) * (k / 200.0);
				if (MacroHeight(m, p.x, p.z) < sea) return true;
			}
			return false;
		};
		const CityDef* brine = g.FindCity("town_brinehaven");
		const CityDef* truce = g.FindCity("town_truceholm");
		const auto westArm = engine::world::cells::CellCenter({ 19, -13 });
		if (brine && truce)
			Check(crossesWater(brine->center, truce->center) && crossesWater(truce->center, { westArm.x, westArm.z }),
				"ile v1.0.74 : ilot separe de Brenhaval et du bras ouest par la baie");
		// Ports : au sec, à moins de 400 m de l'eau.
		for (const char* id : { "poi_port_brinehaven", "poi_port_truceholm", "poi_port_hayfen", "poi_port_orderfen" })
		{
			const PoiDef* p = nullptr;
			for (const PoiDef& q : g.pois) if (q.id == id) p = &q;
			bool near = false;
			if (p)
				for (int a = 0; a < 32 && !near; ++a)
					for (double r = 50.0; r <= 400.0 && !near; r += 50.0)
						near = MacroHeight(m, p->center.x + std::cos(a * 0.19635) * r, p->center.z + std::sin(a * 0.19635) * r) < sea;
			Check(p && p->kind == "port" && MacroHeight(m, p->center.x, p->center.z) >= sea && near,
				(std::string("ile v1.0.74 : ") + id + " au sec, au bord de l'eau").c_str());
		}
		// Aucune route ne quitte l'île ni ne relie l'îlot aux villes de l'île (liaisons
		// maritimes du lore). v1.0.105 : les routes du lore finissent aussi sur des POI de
		// l'île ; le sentier du lore 000220 de l'îlot (Truceholm → Gouffre de la baie)
		// reste sur l'îlot.
		auto onIsland = [&](const std::string& id) {
			int n = 0, e = 0;
			if (const CityDef* c = g.FindCity(id)) { n = c->n; e = c->e; }
			else
			{
				const auto p = std::find_if(g.pois.begin(), g.pois.end(), [&](const PoiDef& q) { return q.id == id; });
				if (p == g.pois.end()) return false;
				n = p->n; e = p->e;
			}
			return n >= 16 && n <= 23 && e >= -15 && e <= -9;
		};
		int bad = 0, island = 0;
		for (const Road& r : g.roads)
		{
			if (onIsland(r.fromId) != onIsland(r.toId)) ++bad;
			const bool truce = r.fromId == "town_truceholm" || r.toId == "town_truceholm";
			if (truce && (g.FindCity(r.fromId) && g.FindCity(r.toId))) ++bad; // îlot relié à une ville
			if (onIsland(r.fromId) && onIsland(r.toId) && !truce) ++island;
		}
		Check(bad == 0 && island >= 2, "ile v1.0.74 : routes de l'ile entre ses villes, aucune vers le continent ni l'ilot");
	}

	/// v1.0.77 : relief de l'Île de la Trêve (lot 6). 11 massifs sans ville (section
	/// reliefs, type massif) sur des cases restées en forêt : côte inchangée (mêmes
	/// terres émergées qu'en v1.0.76), pics de plus de 400 m sur les 7 cases de montagne
	/// du lore, falaises sur la mer ; les quatre villes restent au sec sur un site plat
	/// et reliées ; 11 temples (POI temple / sanctuaire) au sec, ceux des sommets en hauteur.
	void TestReliefIleV1077()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "relief v1.0.77 : master charge");
		if (!ok) return;
		const WorldMaster& m = g.master;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const double sea = m.scale.seaLevelM;
		int massifs = 0, forestCode = 0;
		for (const ReliefDef& r : m.reliefs)
			if (r.type == "massif")
			{
				++massifs;
				forestCode += r.cityId.empty() && r.n >= 17 && r.n <= 22 && r.e >= -14 && r.e <= -10 && LoreCode(m, r.n, r.e) == 'T';
			}
		Check(massifs == 11 && forestCode == 11, "relief v1.0.77 : 11 massifs sur des cases de l'ile restees en foret (T)");
		// Côte inchangée : mêmes terres émergées avec et sans les massifs.
		WorldMaster flat = m;
		flat.reliefs.erase(std::remove_if(flat.reliefs.begin(), flat.reliefs.end(), [](const ReliefDef& r) { return r.type == "massif"; }),
			flat.reliefs.end());
		int coastDiff = 0, cliffs = 0;
		double top = 0.0;
		for (double z = -235000.0; z <= -155000.0; z += 250.0)
			for (double x = -160000.0; x <= -80000.0; x += 250.0)
			{
				const double h = MacroHeight(m, x, z);
				if ((h >= sea) != (MacroHeight(flat, x, z) >= sea)) ++coastDiff;
				top = std::max(top, h - sea);
				// Falaise : plus de 100 m au-dessus de la mer à moins de 500 m de l'eau.
				if (h > sea + 100.0)
					for (int a = 0; a < 8; ++a)
						if (MacroHeight(m, x + std::cos(a * 0.785) * 500.0, z + std::sin(a * 0.785) * 500.0) < sea) { ++cliffs; break; }
			}
		Check(coastDiff == 0, "relief v1.0.77 : cote inchangee (memes terres emergees qu'en v1.0.76)");
		Check(top > 500.0 && cliffs >= 20, "relief v1.0.77 : sommets de plus de 500 m, falaises sur la mer");
		for (auto [n, e] : { std::pair{ 22, -12 }, { 21, -13 }, { 20, -14 }, { 19, -13 }, { 18, -13 }, { 17, -12 }, { 20, -11 } })
		{
			const auto c = engine::world::cells::CellCenter({ n, e });
			double hi = 0.0;
			for (int dz = -4750; dz <= 4750; dz += 250)
				for (int dx = -4750; dx <= 4750; dx += 250) hi = std::max(hi, MacroHeight(m, c.x + dx, c.z + dz) - sea);
			Check(hi > 350.0, ("relief v1.0.77 : pic de plus de 350 m en N0" + std::to_string(n) + "_W0" + std::to_string(-e)).c_str());
		}
		// Villes : au sec, site plat (écart < 25 m au bord du plateau), reliées sauf l'îlot.
		for (const char* id : { "town_brinehaven", "town_duskmoor", "town_truceholm", "town_lanternhold" })
		{
			const CityDef* c = g.FindCity(id);
			if (!c) { Check(false, (std::string("relief v1.0.77 : ") + id + " introuvable").c_str()); continue; }
			const double h0 = MacroHeight(m, c->center.x, c->center.z);
			double worst = 0.0;
			for (int k = 0; k < 16; ++k)
			{
				const double a = k * 0.3927, R = c->radiusM + 60.0;
				worst = std::max(worst, std::fabs(MacroHeight(m, c->center.x + std::cos(a) * R, c->center.z + std::sin(a) * R) - h0));
			}
			int roads = 0;
			for (const Road& r : g.roads)
				if (r.fromId == id || r.toId == id) ++roads;
			Check(h0 > sea + 5.0 && worst < 25.0 && (roads >= 1 || std::string(id) == "town_truceholm"),
				(std::string("relief v1.0.77 : ") + id + " au sec, site plat, reliee").c_str());
		}
		// Temples : 8 temples et 3 sanctuaires sur l'île, au sec ; les sommets au-dessus de 250 m.
		int temples = 0, shrines = 0, dry = 0, high = 0;
		for (const PoiDef& p : g.pois)
		{
			if (p.id.rfind("poi_ile_", 0) != 0) continue;
			temples += p.kind == "temple";
			shrines += p.kind == "sanctuaire";
			const double h = MacroHeight(m, p.center.x, p.center.z) - sea;
			dry += h > 5.0;
			high += p.kind == "temple" && h > 250.0;
		}
		Check(temples == 8 && shrines == 3 && dry == 11 && high >= 7, "relief v1.0.77 : 11 temples de l'ile au sec, 7 au moins sur les sommets");
	}

	/// v1.0.78 : les cases de Vaerdhun (N010_W015) et du Sanctuaire des Brisants
	/// (N010_W016), dont le relief, les rivières et le POI existaient depuis la
	/// v1.0.72, sont générées : listées dans generated_cells, la ville et le lieu
	/// décalé sont bien rattachés à leur cellule.
	/// v1.0.79 : N010_W012 à N010_W014 sont aussi générées ; les 5 cases forment
	/// une rangée continue jusqu'à N010_W011 (déjà générée), Vaerdhun est donc
	/// accessible à pied depuis le reste du monde jouable.
	/// v1.0.80 : N009_W012, N011_W013 et N011_W014 sont aussi générées : le tracé
	/// de la route Syzygie → Vaerdhun (lore 000083) et celui de Vaerdhun →
	/// Treuvhal (lore 000024) sont entièrement dans des cellules générées, et
	/// Treuvhal (N011_W014) est accessible.
	void TestCellulesV1078()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "cellules v1.0.78 : master charge");
		if (!ok) return;
		const WorldMaster& m = g.master;
		auto generated = [&](const std::string& cell) {
			return std::find(m.generatedCells.begin(), m.generatedCells.end(), cell) != m.generatedCells.end();
		};
		Check(generated("N010_W015") && generated("N010_W016"), "cellules v1.0.78 : N010_W015 et N010_W016 generees");
		// v1.0.79 : raccordement par N010_W012..N010_W014 à N010_W011 (déjà générée).
		bool row = true;
		for (const char* c : { "N010_W011", "N010_W012", "N010_W013", "N010_W014", "N010_W015", "N010_W016" })
			row = row && generated(c);
		Check(row, "cellules v1.0.79 : rangee N010_W011 a N010_W016 generee (Vaerdhun relie a pied)");
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const CityDef* ver = g.FindCity("town_0119");
		Check(ver && ver->cell == "N010_W015", "cellules v1.0.78 : Vaerdhun en N010_W015");
		// v1.0.80 : cases de la route Syzygie → Vaerdhun et de Treuvhal.
		Check(generated("N009_W012") && generated("N011_W013") && generated("N011_W014"),
			"cellules v1.0.80 : N009_W012, N011_W013 et N011_W014 generees");
		const CityDef* tru = g.FindCity("hamlet_0148");
		Check(tru && tru->cell == "N011_W014", "cellules v1.0.80 : Treuvhal en N011_W014");
		// Chaque point des routes Syzygie → Vaerdhun et Vaerdhun → Treuvhal est
		// dans une cellule générée (pas de tronçon derrière le mur invisible).
		auto cellOf = [](const Vec2& p) {
			const int e = static_cast<int>(std::lround(p.x / 10000.0)), n = -static_cast<int>(std::lround(p.z / 10000.0));
			char b[32];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		int routes = 0, outside = 0;
		for (const Road& r : g.roads)
		{
			const bool syzVer = (r.fromId == "ville_sysigie" && r.toId == "town_0119") || (r.fromId == "town_0119" && r.toId == "ville_sysigie");
			const bool verTru = (r.fromId == "town_0119" && r.toId == "hamlet_0148") || (r.fromId == "hamlet_0148" && r.toId == "town_0119");
			if (!syzVer && !verTru) continue;
			++routes;
			for (const Vec2& p : r.points)
				outside += !generated(cellOf(p));
		}
		Check(routes == 2 && outside == 0, "cellules v1.0.80 : routes Syzygie - Vaerdhun et Vaerdhun - Treuvhal dans les cellules generees");
		// Lieu décalé : présent dans les lieux de sa cellule (N010_W016).
		World w;
		InitCell(w, g, { 10, -16 });
		bool found = false;
		for (const PoiDef& p : w.pois)
			found = found || p.id == "poi_sanctuaire_des_brisants";
		Check(found, "cellules v1.0.78 : Sanctuaire des Brisants dans les lieux de N010_W016");
	}

	/// v1.0.81 (lore, tomes 15-22) : lieux des Enfers et grottes des dragons de Xygga.
	/// Enfers : 7 POI de haut niveau sur les bras de la croix (palais de Lucifer,
	/// nef des âmes, plateau du rituel au nord ; mer de lave et Seuil des Plans à
	/// l'ouest ; canyon et plateaux des légions à l'est), aucun sur la case de la
	/// Citadelle hors du Portail V, aucun portail de plus (Seuil = « faille »).
	/// Royaumes : 11 grottes « salle » (Xygga dans une case volcanique 'V' et ses
	/// 10 premiers-nés), hors des cases à ville, au sec et à l'écart des routes.
	void TestLieuxV1081()
	{
		using namespace tools::world_gen;
		GlobalWorld hell;
		std::string err;
		const bool okE = LoadWorldMaster(LCDLLN_CONTENT_DIR, "enfers", hell.master, err);
		if (!okE) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(okE, "lieux v1.0.81 : master enfers charge");
		if (!okE) return;
		BuildMacroTerrain(hell);
		const std::set<std::string> generatedE(hell.master.generatedCells.begin(), hell.master.generatedCells.end());
		auto findE = [&](const char* id) -> const PoiDef* {
			for (const PoiDef& p : hell.pois)
				if (p.id == id) return &p;
			return nullptr;
		};
		struct Want { const char* id; const char* cell; const char* kind; };
		const Want wantE[] = {
			{ "poi_enfers_palais_de_lucifer", "N001_E000", "temple" }, { "poi_enfers_nef_des_ames", "N001_E000", "salle" },
			{ "poi_enfers_plateau_du_rituel", "N001_E000", "sanctuaire" }, { "poi_enfers_mer_de_lave", "N000_W001", "volcan" },
			{ "poi_enfers_seuil_des_plans", "N000_W001", "faille" }, { "poi_enfers_canyon_des_falaises_rouges", "N000_E001", "defile" },
			{ "poi_enfers_plateaux_des_legions", "N000_E001", "camp" } };
		bool allE = true;
		for (const Want& w : wantE)
		{
			const PoiDef* p = findE(w.id);
			allE = allE && p && p->cell == w.cell && p->kind == w.kind && generatedE.count(p->cell)
				&& (std::fabs(p->offsetX) > 1.0 || std::fabs(p->offsetZ) > 1.0);
		}
		Check(allE, "lieux v1.0.81 : 7 lieux des Enfers sur les bras de la croix, dans des cellules generees");
		bool citadelFree = true;
		int portals = 0;
		for (const PoiDef& p : hell.pois)
		{
			portals += p.kind == "portail";
			if (p.cell == "N000_E000" && p.kind != "portail") citadelFree = false;
		}
		Check(citadelFree && portals == 5 && hell.master.cities.size() == 1u,
			"lieux v1.0.81 : case de la Citadelle sans nouveau lieu, 5 portails, une seule ville");
		// Lieux de la même case à plus de 300 m les uns des autres et des portails.
		bool spaced = true;
		for (size_t i = 0; i < hell.pois.size(); ++i)
			for (size_t j = i + 1; j < hell.pois.size(); ++j)
				if (hell.pois[i].cell == hell.pois[j].cell
					&& std::hypot(hell.pois[i].center.x - hell.pois[j].center.x, hell.pois[i].center.z - hell.pois[j].center.z) < 300.0)
					spaced = false;
		Check(spaced, "lieux v1.0.81 : lieux des Enfers espaces (300 m)");

		GlobalWorld g;
		const bool okR = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		Check(okR, "lieux v1.0.81 : master royaumes charge");
		if (!okR) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const double sea = g.master.scale.seaLevelM;
		int caves = 0, dry = 0, farFromRoads = 0, noCity = 0;
		bool xyggaInVolcano = false;
		std::set<std::string> caveCells;
		for (const PoiDef& p : g.pois)
		{
			if (p.id != "poi_grotte_xygga" && p.id.rfind("poi_grotte_premier_ne_", 0) != 0) continue;
			++caves;
			caveCells.insert(p.cell);
			if (p.kind != "salle") continue;
			dry += MacroHeight(g.master, p.center.x, p.center.z) > sea + 5.0;
			bool city = false;
			for (const CityDef& c : g.cities) city = city || c.cell == p.cell;
			noCity += !city;
			double dmin = 1e18;
			for (const Road& r : g.roads)
				for (const Vec2& q : r.points) dmin = std::min(dmin, std::hypot(q.x - p.center.x, q.z - p.center.z));
			farFromRoads += dmin > 1500.0;
			if (p.id == "poi_grotte_xygga") xyggaInVolcano = LoreCode(g.master, p.n, p.e) == 'V';
		}
		Check(caves == 11 && caveCells.size() == 11u, "lieux v1.0.81 : 11 grottes de dragons dans 11 cases distinctes");
		Check(xyggaInVolcano, "lieux v1.0.81 : la grotte de Xygga est dans une case volcanique");
		Check(dry == 11 && noCity == 11 && farFromRoads == 11,
			"lieux v1.0.81 : grottes au sec, hors des cases a ville, a plus de 1,5 km des routes");
	}

	/// v1.0.82 (lore, tomes 15-22) : entrée cachée des Ophidiens, peuple ennemi souterrain.
	/// Une seule des trois entrées est dans une cellule générée (Puits des Lianes, N010_E010,
	/// jungle de Leviathan) : POI « salle » au sec, en forêt, hors des cases à ville, à plus de
	/// 1,5 km des routes (aucun chemin n'y mène) et à plus de 15 km de toute grotte de dragon
	/// (pas de conflit avec les dragons de Xygga, v1.0.81).
	void TestLieuxV1082()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "lieux v1.0.82 : master royaumes charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const std::set<std::string> generated(g.master.generatedCells.begin(), g.master.generatedCells.end());
		int entrees = 0;
		const PoiDef* puits = nullptr;
		for (const PoiDef& p : g.pois)
			if (p.id.rfind("poi_ophidiens_", 0) == 0)
			{
				++entrees;
				if (p.id == "poi_ophidiens_puits_des_lianes") puits = &p;
			}
		Check(entrees == 1 && puits && puits->cell == "N010_E010" && puits->kind == "salle" && generated.count(puits->cell),
			"lieux v1.0.82 : une entree ophidienne (Puits des Lianes, salle) en N010_E010 generee");
		if (!puits) return;
		const char code = LoreCode(g.master, puits->n, puits->e);
		bool city = false;
		for (const CityDef& c : g.cities) city = city || c.cell == puits->cell;
		double droad = 1e18;
		for (const Road& r : g.roads)
			for (const Vec2& q : r.points) droad = std::min(droad, std::hypot(q.x - puits->center.x, q.z - puits->center.z));
		double dcave = 1e18;
		for (const PoiDef& p : g.pois)
			if (p.id == "poi_grotte_xygga" || p.id.rfind("poi_grotte_premier_ne_", 0) == 0)
				dcave = std::min(dcave, std::hypot(p.center.x - puits->center.x, p.center.z - puits->center.z));
		Check((code == 'T' || code == 't' || code == 'R') && MacroHeight(g.master, puits->center.x, puits->center.z) > g.master.scale.seaLevelM + 5.0,
			"lieux v1.0.82 : Puits des Lianes au sec, en foret");
		Check(!city && droad > 1500.0 && dcave > 15000.0,
			"lieux v1.0.82 : Puits des Lianes hors case a ville, a plus de 1,5 km des routes et 15 km des grottes de dragons");
	}

	/// v1.0.85 (lore, lot 13) : balises de danger (POI « monolithe ») sur le passage vers un
	/// contenu de plus haut niveau, dans des cellules générées. Cairn des Os noircis (N010_E012,
	/// au sud de la case, à plus de 3 km de la grotte du dragon qu'il ne désigne pas) ; Stèle des
	/// Flèches blanches (N008_W007, à plus de 2 km de la Porte 5) ; trois Bornes des Géants aux
	/// sorties de la poche de Whispers Hill (N011_E008, N012_E008), à moins de 500 m d'une route.
	void TestLieuxV1085()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "lieux v1.0.85 : master royaumes charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		const std::set<std::string> generated(g.master.generatedCells.begin(), g.master.generatedCells.end());
		auto find = [&](const char* id) -> const PoiDef* {
			for (const PoiDef& p : g.pois)
				if (p.id == id) return &p;
			return nullptr;
		};
		// Balises posées le long du réseau du GÉNÉRATEUR (v1.0.85-86). v1.0.105 : ce réseau
		// n'est plus tracé en entier (routes du lore, LORE_BD §16.34). v1.0.106 : les balises
		// de route ont une position manuelle au bord d'une route du lore (§16.35) ; le test
		// mesure de nouveau sur les routes réellement tracées en jeu (g.roads).
		auto roadDist = [&](const PoiDef& p) {
			double dmin = 1e18;
			for (const Road& r : g.roads)
				for (const Vec2& q : r.points) dmin = std::min(dmin, std::hypot(q.x - p.center.x, q.z - p.center.z));
			return dmin;
		};
		int balises = 0, valid = 0;
		for (const PoiDef& p : g.pois)
		{
			if (p.id.rfind("poi_balise_", 0) != 0) continue;
			++balises;
			valid += p.kind == "monolithe" && generated.count(p.cell)
				&& MacroHeight(g.master, p.center.x, p.center.z) > g.master.scale.seaLevelM + 5.0;
		}
		Check(balises == 5 && valid == 5, "lieux v1.0.85 : 5 balises monolithe au sec dans des cellules generees");
		const PoiDef* cairn = find("poi_balise_cairn_os_noircis");
		const PoiDef* grotte = find("poi_grotte_premier_ne_noir_ailes_brunes");
		Check(cairn && grotte && cairn->cell == "N010_E012" && grotte->cell == cairn->cell
				&& std::hypot(cairn->center.x - grotte->center.x, cairn->center.z - grotte->center.z) > 3000.0,
			"lieux v1.0.85 : Cairn des Os noircis en N010_E012, a plus de 3 km de la grotte");
		const PoiDef* stele = find("poi_balise_stele_fleches_blanches");
		const PoiDef* porte = find("poi_porte_5");
		Check(stele && porte && stele->cell == "N008_W007"
				&& std::hypot(stele->center.x - porte->center.x, stele->center.z - porte->center.z) > 2000.0,
			"lieux v1.0.85 : Stele des Fleches blanches en N008_W007, a plus de 2 km de la Porte 5");
		int bornes = 0;
		for (const char* id : { "poi_balise_borne_geants_nord", "poi_balise_borne_geants_ouest", "poi_balise_borne_geants_sud" })
		{
			const PoiDef* b = find(id);
			bornes += b && (b->cell == "N011_E008" || b->cell == "N012_E008") && roadDist(*b) < 500.0;
		}
		Check(bornes == 3, "lieux v1.0.85 : 3 Bornes des Geants dans la poche de Whispers Hill, a moins de 500 m d'une route");
		// Le balisage ne rapproche aucune grotte de dragon d'une route (v1.0.81).
		Check(grotte && roadDist(*grotte) > 1500.0, "lieux v1.0.85 : la grotte de N010_E012 reste a plus de 1,5 km des routes");
	}

	/// v1.0.86 (lore, lot 14) : un lieu [GD] (POI réel) sur chaque case jouable vide de la Baronnie
	/// de Moork (11), des Terres désolées de Tsaroth (15) et du Désert de l'Oracle (10). Jouable =
	/// `generated_cells` du master + corridors des départs (AddStartCorridors), comme en génération.
	/// Chaque POI est seul sur sa case (aucune ville), au sec, dans la boîte `regions` de sa région
	/// (sauf S004_E016 et S005_E016, Baronnie de Moork par le lore mais hors de la boîte du master).
	/// Balises : Borne des Monts de Moork et Stèle des Deux Fois Nés à moins de 500 m d'une route,
	/// Cairn des Coulées en case volcanique, Cairn des Caravaniers loin de la grotte du premier-né
	/// qu'il ne désigne pas. Tsaroth (S009_E011) reste sans POI ni ville (lieu sans service).
	void TestLieuxV1086()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "lieux v1.0.86 : master royaumes charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = g.master.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		struct Expected { const char* poi; const char* cell; const char* region; };
		static const Expected kLieux[] = {
			{ "poi_tour_guet_route_shadehold", "N002_E011", "baronnie_de_moorks" },
			{ "poi_ferme_fortifiee_pillee", "N000_E011", "baronnie_de_moorks" },
			{ "poi_camp_clans_insoumis", "S001_E011", "baronnie_de_moorks" },
			{ "poi_tertre_chefs_de_clan", "S003_E011", "baronnie_de_moorks" },
			{ "poi_puits_piste_du_sel", "S002_E012", "baronnie_de_moorks" },
			{ "poi_salines_baronnie", "S002_E013", "baronnie_de_moorks" },
			{ "poi_fosses_aux_vers", "S002_E014", "baronnie_de_moorks" },
			{ "poi_enclos_betes_de_guerre", "S003_E015", "baronnie_de_moorks" },
			{ "poi_fortin_imperial_ruine", "S003_E016", "baronnie_de_moorks" },
			{ "poi_relais_convois_ghrunke_dash", "S004_E016", nullptr },
			{ "poi_borne_monts_de_moork", "S005_E016", nullptr },
			{ "poi_tour_guet_effondree", "S006_E008", "terres_desolees" },
			{ "poi_puits_de_la_lisiere", "S006_E009", "terres_desolees" },
			{ "poi_ferme_fortifiee_abandonnee", "S007_E008", "terres_desolees" },
			{ "poi_bivouac_des_deserteurs", "S007_E009", "terres_desolees" },
			{ "poi_relais_piste_des_morts", "S008_E008", "terres_desolees" },
			{ "poi_mine_de_soufre_abandonnee", "S008_E009", "terres_desolees" },
			{ "poi_grotte_aux_chauves_souris", "S008_E011", "terres_desolees" },
			{ "poi_statues_voie_antique", "S009_E009", "terres_desolees" },
			{ "poi_stele_des_deux_fois_nes", "S009_E010", "terres_desolees" },
			{ "poi_plaine_des_cendres", "S009_E012", "terres_desolees" },
			{ "poi_lande_pierres_dressees", "S010_E011", "terres_desolees" },
			{ "poi_ravin_de_l_ichor", "S010_E012", "terres_desolees" },
			{ "poi_galerie_naine_abandonnee", "S006_E015", "terres_desolees" },
			{ "poi_forge_engloutie", "S007_E015", "terres_desolees" },
			{ "poi_cairn_des_coulees", "S008_E015", "terres_desolees" },
			{ "poi_campement_des_pelerins", "N000_E001", "desert_central" },
			{ "poi_cairn_des_caravaniers", "N000_E002", "desert_central" },
			{ "poi_menhirs_plaine_oracle", "N000_W001", "desert_central" },
			{ "poi_echoppes_faux_devins", "N001_W001", "desert_central" },
			{ "poi_oasis_palmes_noires", "N001_W002", "desert_central" },
			{ "poi_relais_des_caravanes", "N002_W002", "desert_central" },
			{ "poi_repaire_pillards_desert", "N002_W003", "desert_central" },
			{ "poi_tour_effondree_des_mages", "N003_W004", "desert_central" },
			{ "poi_puits_des_pelerins", "S001_E002", "desert_central" },
			{ "poi_tour_guet_garundel", "S001_E003", "desert_central" },
		};
		auto find = [&](const char* id) -> const PoiDef* {
			for (const PoiDef& p : g.pois)
				if (p.id == id) return &p;
			return nullptr;
		};
		// Balises posées le long du réseau du GÉNÉRATEUR (v1.0.85-86). v1.0.105 : ce réseau
		// n'est plus tracé en entier (routes du lore, LORE_BD §16.34). v1.0.106 : les balises
		// de route ont une position manuelle au bord d'une route du lore (§16.35) ; le test
		// mesure de nouveau sur les routes réellement tracées en jeu (g.roads).
		auto roadDist = [&](const PoiDef& p) {
			double dmin = 1e18;
			for (const Road& r : g.roads)
				for (const Vec2& q : r.points) dmin = std::min(dmin, std::hypot(q.x - p.center.x, q.z - p.center.z));
			return dmin;
		};
		// region nullptr : case de la Baronnie de Moork dans le lore (region_id des cellules), hors de
		// la boîte `regions` du master (n_min = -3) : S004_E016 et S005_E016.
		auto inRegion = [&](const PoiDef& p, const char* region) {
			if (!region) return true;
			for (const auto& r : g.master.regions)
				if (r.id == region) return p.e >= r.eMin && p.e <= r.eMax && p.n >= r.nMin && p.n <= r.nMax;
			return false;
		};
		int valid = 0;
		for (const Expected& x : kLieux)
		{
			const PoiDef* p = find(x.poi);
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.n == p->n && c.e == p->e) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone && inRegion(*p, x.region)
				&& MacroHeight(g.master, p->center.x, p->center.z) > g.master.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.86 invalide : %s\n", x.poi);
			valid += good;
		}
		Check(valid == 36, "lieux v1.0.86 : 36 lieux au sec, seuls sur leur case jouable, dans leur region");
		const PoiDef* borne = find("poi_borne_monts_de_moork");
		const PoiDef* stele = find("poi_stele_des_deux_fois_nes");
		const PoiDef* coulees = find("poi_cairn_des_coulees");
		const PoiDef* caravaniers = find("poi_cairn_des_caravaniers");
		const PoiDef* grotte = find("poi_grotte_premier_ne_dore_enflamme");
		Check(borne && stele && coulees && caravaniers && borne->kind == "monolithe" && stele->kind == "monolithe"
				&& coulees->kind == "monolithe" && caravaniers->kind == "monolithe",
			"lieux v1.0.86 : 4 balises monolithe");
		Check(borne && stele && roadDist(*borne) < 500.0 && roadDist(*stele) < 500.0,
			"lieux v1.0.86 : Borne des Monts de Moork et Stele des Deux Fois Nes a moins de 500 m d'une route");
		Check(coulees && LoreCode(g.master, coulees->n, coulees->e) == 'V', "lieux v1.0.86 : Cairn des Coulees en case volcanique");
		Check(caravaniers && grotte && grotte->cell == "N002_E004"
				&& std::hypot(caravaniers->center.x - grotte->center.x, caravaniers->center.z - grotte->center.z) > 15000.0
				&& roadDist(*grotte) > 1500.0,
			"lieux v1.0.86 : Cairn des Caravaniers a plus de 15 km de la grotte, grotte toujours loin des routes");
		bool tsarothVide = true;
		for (const PoiDef& p : g.pois) tsarothVide = tsarothVide && p.cell != "S009_E011";
		for (const CityDef& c : g.cities) tsarothVide = tsarothVide && !(c.n == -9 && c.e == 11);
		Check(tsarothVide, "lieux v1.0.86 : Tsaroth (S009_E011) reste sans POI ni ville");
	}

	/// v1.0.87 (lore, lot 15) : un lieu [GD] (POI réel) sur chaque case jouable vide des Terres des
	/// Gigantomorphes (6 cases autour de l'enclave de Whispers Hill) et de la Via Magna (5). La Marche
	/// de l'Est n'a aucune case jouable vide (quêtes seulement). Jouable = `generated_cells` du master
	/// + corridors des départs (AddStartCorridors). Chaque POI est seul sur sa case (aucune ville, aucun
	/// autre POI), au sec, dans la boîte `regions` de sa région quand la case y est (les cases de la
	/// lisière sud des Gigantomorphes, N004_E010, S002_E003 et S002_E004 sont rattachées par le lore,
	/// hors de la boîte du master). L'enclave de Whispers Hill garde ses POI (aucun ajout en N011_E008
	/// ni N012_E008) et le Puits des Lianes (entrée ophidienne de N010_E010) reste à plus de 10 km du
	/// Cercle de pierres des géants.
	void TestLieuxV1087()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "lieux v1.0.87 : master royaumes charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = g.master.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		struct Expected { const char* poi; const char* cell; const char* region; };
		static const Expected kLieux[] = {
			{ "poi_coupe_des_bucherons", "N010_E007", nullptr },
			{ "poi_poste_avance_abandonne", "N010_E008", nullptr },
			{ "poi_carriere_des_geants", "N011_E007", nullptr },
			{ "poi_cercle_pierres_des_geants", "N011_E009", nullptr },
			{ "poi_camp_des_trappeurs", "N012_E007", nullptr },
			{ "poi_taniere_des_trolls", "N012_E009", nullptr },
			{ "poi_poste_relais_imperial", "N004_E010", nullptr },
			{ "poi_camp_des_detrousseurs", "N004_W003", "via_magna" },
			{ "poi_caravane_ensablee", "N004_W004", "via_magna" },
			{ "poi_bivouac_de_la_legion", "S002_E003", nullptr },
			{ "poi_sanctuaire_profane", "S002_E004", nullptr },
		};
		auto find = [&](const char* id) -> const PoiDef* {
			for (const PoiDef& p : g.pois)
				if (p.id == id) return &p;
			return nullptr;
		};
		auto inRegion = [&](const PoiDef& p, const char* region) {
			if (!region) return true;
			for (const auto& r : g.master.regions)
				if (r.id == region) return p.e >= r.eMin && p.e <= r.eMax && p.n >= r.nMin && p.n <= r.nMax;
			return false;
		};
		int valid = 0;
		for (const Expected& x : kLieux)
		{
			const PoiDef* p = find(x.poi);
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.n == p->n && c.e == p->e) alone = false;
			for (const PoiDef& q : g.pois)
				if (p && &q != p && q.cell == p->cell) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone && inRegion(*p, x.region)
				&& MacroHeight(g.master, p->center.x, p->center.z) > g.master.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.87 invalide : %s\n", x.poi);
			valid += good;
		}
		Check(valid == 11, "lieux v1.0.87 : 11 lieux au sec, seuls sur leur case jouable");
		int enclave = 0;
		for (const PoiDef& p : g.pois)
			if (p.cell == "N011_E008" || p.cell == "N012_E008") ++enclave;
		Check(enclave == 5, "lieux v1.0.87 : enclave de Whispers Hill inchangee (5 POI)");
		const PoiDef* cercle = find("poi_cercle_pierres_des_geants");
		const PoiDef* puits = find("poi_ophidiens_puits_des_lianes");
		Check(cercle && puits && std::hypot(cercle->center.x - puits->center.x, cercle->center.z - puits->center.z) > 10000.0,
			"lieux v1.0.87 : Cercle de pierres des geants a plus de 10 km du Puits des Lianes");
	}

	/// v1.0.88 (lot 16) : ouverture de la Marche de l'Est. 45 cases ajoutées à `generated_cells`
	/// (cases des villes Horkher, Nykhfeld, Vlakmorh, Ebhrazul et Dhazkmer, routes qui les relient
	/// entre elles et au monde déjà jouable, marge d'une case autour des villes, trous intérieurs).
	/// Vérifie : les 45 cases sont générées et terrestres ; chacune touche (4-voisinage) une case jouable
	/// et toutes sont atteintes à pied depuis le départ de Grakhul (N008_E012) sans quitter les cases
	/// jouables ; les 5 villes sont dans leurs cases ; chaque point des 6 routes du générateur entre ces
	/// villes et Moork, Thokrul et Gharzuk est dans une case jouable ; chaque case vide reçoit un lieu
	/// [GD] (40 POI au sec, seuls sur leur case) ; les cases de villes n'ont aucun POI ; les Ruines du
	/// fortin des Marches restent en N007_E013.
	void TestCellulesLieuxV1088()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "cellules et lieux v1.0.88 : master royaumes charge");
		if (!ok) return;
		static const char* kCells[] = {
			"N007_E015", "N006_E013", "N006_E014", "N006_E015", "N006_E019", "N006_E020",
			"N005_E013", "N005_E014", "N005_E015", "N005_E016", "N005_E017", "N005_E018",
			"N005_E019", "N005_E020", "N004_E012", "N004_E013", "N004_E014", "N004_E015",
			"N004_E016", "N004_E017", "N004_E018", "N004_E019", "N004_E020", "N003_E013",
			"N003_E014", "N003_E015", "N003_E016", "N003_E017", "N003_E018", "N003_E019",
			"N003_E020", "N002_E013", "N002_E014", "N002_E015", "N002_E016", "N002_E019",
			"N002_E020", "N001_E012", "N001_E015", "N001_E019", "N001_E020", "N000_E015",
			"N000_E019", "N000_E020", "S001_E015",
		};
		const WorldMaster& m = g.master;
		const std::set<std::string> listed(m.generatedCells.begin(), m.generatedCells.end());
		auto parse = [](const std::string& c, int& n, int& e) {
			n = std::stoi(c.substr(1, 3)) * (c[0] == 'N' ? 1 : -1);
			e = std::stoi(c.substr(6, 3)) * (c[5] == 'E' ? 1 : -1);
		};
		auto name = [](int n, int e) {
			char b[32];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		int inList = 0, land = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			inList += listed.count(c) ? 1 : 0;
			const char code = LoreCode(m, n, e);
			land += (code != ' ' && code != '-') ? 1 : 0;
		}
		Check(inList == 45 && land == 45, "cellules et lieux v1.0.88 : 45 cases de la Marche de l'Est generees, toutes terrestres");
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = m.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		// Connexité : chaque nouvelle case touche une case jouable, et toutes sont atteintes depuis Grakhul.
		int touching = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			bool t = false;
			for (const auto& d : { std::pair<int, int>{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
				t = t || playable.count(name(n + d.first, e + d.second)) > 0;
			touching += t ? 1 : 0;
		}
		std::set<std::string> seen{ "N008_E012" };
		std::vector<std::string> front{ "N008_E012" };
		while (!front.empty())
		{
			const std::string c = front.back();
			front.pop_back();
			int n = 0, e = 0;
			parse(c, n, e);
			for (const auto& d : { std::pair<int, int>{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
			{
				const std::string x = name(n + d.first, e + d.second);
				if (playable.count(x) && seen.insert(x).second) front.push_back(x);
			}
		}
		int reached = 0;
		for (const char* c : kCells) reached += seen.count(c) ? 1 : 0;
		Check(touching == 45 && reached == 45, "cellules et lieux v1.0.88 : aucune case isolee, toutes reliees a pied au depart de Grakhul");
		// Villes de la Marche dans leurs cases (générées).
		struct Ville { const char* id; const char* cell; };
		static const Ville kVilles[] = { { "city_horkher", "N006_E014" }, { "hamlet_0201", "N002_E015" }, { "town_0131", "N004_E018" },
			{ "town_0115", "N005_E020" }, { "town_0126", "N001_E020" } };
		int villes = 0;
		for (const Ville& v : kVilles)
		{
			const CityDef* c = g.FindCity(v.id);
			villes += (c && c->cell == v.cell && playable.count(c->cell)) ? 1 : 0;
		}
		Check(villes == 5, "cellules et lieux v1.0.88 : Horkher, Nykhfeld, Vlakmorh, Ebhrazul et Dhazkmer jouables");
		// Routes du générateur entre les villes de la Marche et vers Moork, Thokrul, Gharzuk.
		const std::set<std::string> marche{ "city_horkher", "hamlet_0201", "town_0131", "town_0115", "town_0126" };
		const std::set<std::string> ends{ "city_horkher", "hamlet_0201", "town_0131", "town_0115", "town_0126", "ville_moork", "hamlet_thokrul", "hamlet_gharzuk" };
		auto cellOf = [&](const Vec2& p) {
			return name(-static_cast<int>(std::lround(p.z / 10000.0)), static_cast<int>(std::lround(p.x / 10000.0)));
		};
		int routes = 0, outside = 0;
		for (const Road& r : g.roads)
		{
			if (!(marche.count(r.fromId) || marche.count(r.toId)) || !ends.count(r.fromId) || !ends.count(r.toId)) continue;
			++routes;
			for (const Vec2& p : r.points) outside += playable.count(cellOf(p)) ? 0 : 1;
		}
		Check(routes == 6 && outside == 0, "cellules et lieux v1.0.88 : 6 routes de la Marche entierement dans des cases jouables");
		// Lieux [GD] : un POI par case vide.
		struct Expected { const char* poi; const char* cell; };
		static const Expected kLieux[] = {
			{ "poi_barrage_de_peage_lune_noire", "N004_E012" }, { "poi_relais_incendie_via_magna", "N005_E013" },
			{ "poi_camp_de_troc_des_orcs", "N006_E013" }, { "poi_cabanes_des_charbonniers", "N007_E015" },
			{ "poi_bergerie_fortifiee", "N006_E015" }, { "poi_moulin_a_vent_pille", "N005_E014" },
			{ "poi_repaire_des_braconniers", "N005_E015" }, { "poi_champ_de_bataille_de_la_conquete", "N004_E013" },
			{ "poi_tertre_des_legionnaires", "N003_E013" }, { "poi_grenier_a_ble_requisitionne", "N004_E014" },
			{ "poi_poste_de_garde_lune_noire", "N003_E014" }, { "poi_chapelle_imperiale_desaffectee", "N004_E015" },
			{ "poi_grange_incendiee", "N003_E015" }, { "poi_bivouac_des_colporteurs", "N002_E013" },
			{ "poi_enclos_a_betail", "N002_E014" }, { "poi_campement_des_pecheurs", "N001_E012" },
			{ "poi_relais_de_la_route_de_moork", "N001_E015" }, { "poi_tour_a_signaux_de_la_baronnie", "N000_E015" },
			{ "poi_cimetiere_des_captifs", "S001_E015" }, { "poi_croix_de_carrefour_profanee", "N002_E016" },
			{ "poi_haras_de_la_lune_noire", "N003_E016" }, { "poi_camp_des_refugies", "N003_E017" },
			{ "poi_tumulus_de_la_plaine", "N003_E018" }, { "poi_scierie_abandonnee", "N004_E016" },
			{ "poi_tour_de_guet_de_la_lisiere", "N004_E017" }, { "poi_taniere_du_loup_geant", "N005_E016" },
			{ "poi_autel_sylvestre_lune_noire", "N005_E017" }, { "poi_repaire_des_trolls_de_la_foret", "N005_E018" },
			{ "poi_camp_de_la_patrouille_noire", "N004_E019" }, { "poi_ermitage_en_ruine", "N005_E019" },
			{ "poi_mine_de_fer_abandonnee", "N006_E019" }, { "poi_poste_frontiere_du_nord", "N006_E020" },
			{ "poi_domaine_seigneurial_confisque", "N004_E020" }, { "poi_fosse_aux_ossements", "N003_E019" },
			{ "poi_camp_de_chasse_orque", "N003_E020" }, { "poi_menhir_du_carrefour", "N002_E019" },
			{ "poi_arene_de_lutte_des_orcs", "N002_E020" }, { "poi_enclos_des_sangliers_de_guerre", "N001_E019" },
			{ "poi_tour_ensablee", "N000_E019" }, { "poi_camp_des_nomades", "N000_E020" },
		};
		int valid = 0;
		for (const Expected& x : kLieux)
		{
			const PoiDef* p = nullptr;
			for (const PoiDef& q : g.pois)
				if (q.id == x.poi) p = &q;
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.n == p->n && c.e == p->e) alone = false;
			for (const PoiDef& q : g.pois)
				if (p && &q != p && q.cell == p->cell) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone
				&& MacroHeight(m, p->center.x, p->center.z) > m.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.88 invalide : %s\n", x.poi);
			valid += good ? 1 : 0;
		}
		Check(valid == 40, "cellules et lieux v1.0.88 : 40 lieux au sec, seuls sur leur case jouable");
		int poiVilles = 0;
		for (const Ville& v : kVilles)
			for (const PoiDef& q : g.pois) poiVilles += q.cell == v.cell ? 1 : 0;
		Check(poiVilles == 0, "cellules et lieux v1.0.88 : aucun POI sur les cases des villes de la Marche");
		bool fortin = false;
		for (const PoiDef& q : g.pois) fortin = fortin || (q.id == "poi_ruines_fortin_des_marches" && q.cell == "N007_E013");
		Check(fortin, "cellules et lieux v1.0.88 : Ruines du fortin des Marches toujours en N007_E013");
	}

	/// v1.0.89 (lore, lot 17) : Royaume des nains de l'est. Aucune case jouable vide : les cases jouables
	/// (`generated_cells` + corridors des départs) de la boîte `royaume_des_nains_de_lest` du master
	/// (E015-E024 × S016-S004) sont les 14 déjà générées (départ 3×3 de Durnhalde, Ghrunke Dash, Monts de
	/// Moork, coulées de l'ouest), et chacune porte une ville ou un POI. Le lot n'ouvre aucune case et
	/// n'ajoute aucun POI (quêtes seulement, dans le lore) : le test le garde. v1.0.91 : la route Ysenkhal -
	/// Vozmarkh ajoute 7 cases jouables dans la boîte (21 au total) ; v1.0.104 : les routes de Vozmarkh en
	/// ajoutent 29 (50 au total) ; v1.0.106 : 2 trous intérieurs de la route Ghrunke Dash - Ghrimfeth
	/// (S004_E015, S005_E015, Baronnie de Moork par le lore) en ajoutent 2 (52), chacune avec une ville
	/// ou un POI. Il vérifie aussi que les
	/// 5 lieux du départ que relient les quêtes sont dans leur case.
	void TestLieuxV1089()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "lieux v1.0.89 : master royaumes charge");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = g.master.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		const RegionDef* box = nullptr;
		for (const auto& r : g.master.regions)
			if (r.id == "royaume_des_nains_de_lest") box = &r;
		Check(box != nullptr, "lieux v1.0.89 : boite royaume_des_nains_de_lest presente");
		if (!box) return;
		auto name = [](int n, int e) {
			char b[32];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		int inBox = 0, occupied = 0;
		for (int n = box->nMin; n <= box->nMax; ++n)
			for (int e = box->eMin; e <= box->eMax; ++e)
			{
				const std::string c = name(n, e);
				if (!playable.count(c)) continue;
				++inBox;
				bool used = false;
				for (const CityDef& v : g.cities) used = used || v.cell == c;
				for (const PoiDef& p : g.pois) used = used || p.cell == c;
				if (!used) std::fprintf(stderr, "  case jouable vide (v1.0.89) : %s\n", c.c_str());
				occupied += used ? 1 : 0;
			}
		Check(inBox == 52, "lieux v1.0.89 : 52 cases jouables dans la boite du Royaume des nains de l'est (14 + 7 de la route de Vozmarkh, v1.0.91, + 29 des routes de Vozmarkh, v1.0.104, + S004_E015 et S005_E015, trous de la route de Ghrunke Dash, v1.0.106)");
		Check(occupied == inBox, "lieux v1.0.89 : chaque case jouable du Royaume des nains de l'est porte une ville ou un POI");
		struct Expected { const char* poi; const char* cell; };
		static const Expected kLieux[] = {
			{ "poi_puits_des_apprentis", "S006_E017" }, { "poi_autel_des_ancetres", "S006_E016" },
			{ "poi_relais_des_convois", "S008_E016" }, { "poi_galeries_effondrees_vieille_veine", "S006_E018" },
			{ "poi_camp_des_prospecteurs", "S007_E018" },
		};
		int found = 0;
		for (const Expected& x : kLieux)
			for (const PoiDef& p : g.pois)
				found += (p.id == x.poi && p.cell == x.cell && playable.count(p.cell)) ? 1 : 0;
		Check(found == 5, "lieux v1.0.89 : les 5 lieux des quetes du depart de Durnhalde dans leurs cases jouables");
	}

	/// v1.0.91 (lot 19) : ouverture de la route Ysenkhal - Vozmarkh (suite du départ nain) et des 4 routes
	/// de la Marche de l'Est coupées au mur invisible (Ebhrazul - Rhivkhar, Ebhrazul - Ryvaldh, Ebhrazul -
	/// Grhavelt, Dhazkmer - Dhuzvelt). 57 cases ajoutées à `generated_cells` (tracés du lore et du
	/// générateur, villes d'arrivée, marge d'une case, trous intérieurs).
	/// Vérifie : les 57 cases sont générées et terrestres ; chacune touche (4-voisinage) une case jouable ;
	/// les 7 cases de Vozmarkh sont atteintes à pied depuis Durnhalde (S007_E017) et les 50 de la Marche
	/// depuis Grakhul (N008_E012) sans quitter les cases jouables ; Vozmarkh, Rhivkhar, Ryvaldh,
	/// Grhavelt et Dhuzvelt sont jouables ; chaque point des routes du générateur entre ces villes et
	/// Ysenkhal, Ebhrazul, Dhazkmer (et entre Ryvaldh, Grhavelt et Dhuzvelt) est dans une case jouable,
	/// de même que chaque case des 5 tracés du lore ; un POI [GD] par case vide (52, au sec, seuls sur leur
	/// case) ; aucun POI sur les cases des villes d'arrivée ; S008_E020 et S008_E021 restent fermées.
	void TestCellulesLieuxV1091()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "cellules et lieux v1.0.91 : master royaumes charge");
		if (!ok) return;
		static const char* kCells[] = {
			"N009_E017", "N009_E018", "N009_E019", "N008_E017", "N008_E018", "N008_E019",
			"N007_E017", "N007_E018", "N007_E019", "N007_E020", "N007_E021", "N007_E022",
			"N007_E023", "N007_E024", "N006_E017", "N006_E018", "N006_E021", "N006_E022",
			"N006_E023", "N006_E024", "N005_E021", "N005_E022", "N005_E023", "N005_E024",
			"N004_E021", "N004_E022", "N004_E023", "N004_E024", "N003_E021", "N003_E022",
			"N003_E023", "N003_E024", "N002_E021", "N002_E022", "N002_E023", "N002_E024",
			"N001_E021", "N001_E022", "N001_E023", "N001_E024", "N000_E021", "N000_E022",
			"N000_E023", "N000_E024", "S001_E022", "S001_E023", "S001_E024", "S002_E022",
			"S002_E023", "S002_E024", "S008_E019", "S009_E019", "S009_E020", "S009_E021",
			"S010_E019", "S010_E020", "S010_E021",
		};
		const WorldMaster& m = g.master;
		const std::set<std::string> listed(m.generatedCells.begin(), m.generatedCells.end());
		auto parse = [](const std::string& c, int& n, int& e) {
			n = std::stoi(c.substr(1, 3)) * (c[0] == 'N' ? 1 : -1);
			e = std::stoi(c.substr(6, 3)) * (c[5] == 'E' ? 1 : -1);
		};
		auto name = [](int n, int e) {
			char b[32];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		int inList = 0, land = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			inList += listed.count(c) ? 1 : 0;
			const char code = LoreCode(m, n, e);
			land += (code != ' ' && code != '-') ? 1 : 0;
		}
		Check(inList == 57 && land == 57, "cellules et lieux v1.0.91 : 57 cases generees, toutes terrestres");
		Check(!listed.count("S008_E020") && !listed.count("S008_E021"), "cellules et lieux v1.0.91 : S008_E020 et S008_E021 restent fermees");
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = m.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		// Connexité : chaque nouvelle case touche une case jouable ; parcours à pied depuis deux départs.
		static const std::pair<int, int> kDirs[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		int touching = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			bool t = false;
			for (const auto& d : kDirs) t = t || playable.count(name(n + d.first, e + d.second)) > 0;
			touching += t ? 1 : 0;
		}
		auto reach = [&](const std::string& start) {
			std::set<std::string> seen{ start };
			std::vector<std::string> front{ start };
			while (!front.empty())
			{
				const std::string c = front.back();
				front.pop_back();
				int n = 0, e = 0;
				parse(c, n, e);
				for (const auto& d : kDirs)
				{
					const std::string x = name(n + d.first, e + d.second);
					if (playable.count(x) && seen.insert(x).second) front.push_back(x);
				}
			}
			return seen;
		};
		const std::set<std::string> fromDurnhalde = reach("S007_E017");
		const std::set<std::string> fromGrakhul = reach("N008_E012");
		int reached = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			reached += (n < -3 ? fromDurnhalde.count(c) : fromGrakhul.count(c)) ? 1 : 0;
		}
		Check(touching == 57 && reached == 57, "cellules et lieux v1.0.91 : aucune case isolee, Vozmarkh relie a Durnhalde et la Marche a Grakhul");
		// Villes d'arrivée jouables.
		struct Ville { const char* id; const char* cell; };
		static const Ville kVilles[] = { { "town_0125", "S009_E020" }, { "town_0132", "N008_E018" }, { "town_riverfold", "N006_E023" },
			{ "town_greyfield", "N003_E023" }, { "hamlet_dustfield", "S001_E023" } };
		int villes = 0;
		for (const Ville& v : kVilles)
		{
			const CityDef* c = g.FindCity(v.id);
			villes += (c && c->cell == v.cell && playable.count(c->cell)) ? 1 : 0;
		}
		Check(villes == 5, "cellules et lieux v1.0.91 : Vozmarkh, Rhivkhar, Ryvaldh, Grhavelt et Dhuzvelt jouables");
		// Routes du générateur entre les villes concernées.
		static const std::pair<const char*, const char*> kPairs[] = {
			{ "hamlet_eisenkehl", "town_0125" }, { "town_0115", "town_0132" }, { "town_0115", "town_riverfold" },
			{ "town_0126", "hamlet_dustfield" }, { "town_riverfold", "town_greyfield" }, { "town_0126", "town_greyfield" },
			{ "town_greyfield", "hamlet_dustfield" },
		};
		auto cellOf = [&](const Vec2& p) {
			return name(-static_cast<int>(std::lround(p.z / 10000.0)), static_cast<int>(std::lround(p.x / 10000.0)));
		};
		int routes = 0, outside = 0;
		// Réseau du générateur (v1.0.105 : g.generatorRoads, base des corridors et du choix des cases).
		for (const Road& r : g.generatorRoads)
		{
			bool match = false;
			for (const auto& pr : kPairs)
				match = match || (r.fromId == pr.first && r.toId == pr.second) || (r.fromId == pr.second && r.toId == pr.first);
			if (!match) continue;
			++routes;
			for (const Vec2& p : r.points) outside += playable.count(cellOf(p)) ? 0 : 1;
		}
		Check(routes == 7 && outside == 0, "cellules et lieux v1.0.91 : 7 routes du generateur entierement dans des cases jouables");
		// Tracés du lore (waypoints, ou ligne droite pour Ysenkhal - Vozmarkh), case par case.
		static const char* kLoreCells[] = {
			"S008_E018", "S008_E019", "S009_E020",                 // Ysenkhal - Vozmarkh
			"N008_E018", "N007_E019", "N006_E019", "N005_E020",    // Ebhrazul - Rhivkhar (000104)
			"N006_E023", "N005_E022", "N004_E021",                 // Ebhrazul - Ryvaldh (000160)
			"N003_E023", "N003_E022",                              // Ebhrazul - Grhavelt (000161)
			"S001_E023", "S001_E022", "N000_E021", "N001_E020",    // Dhazkmer - Dhuzvelt (000162)
		};
		int lore = 0;
		for (const char* c : kLoreCells) lore += playable.count(c) ? 1 : 0;
		Check(lore == 16, "cellules et lieux v1.0.91 : chaque case des 5 traces du lore est jouable");
		// Lieux [GD] : un POI par case vide.
		struct Expected { const char* poi; const char* cell; };
		static const Expected kLieux[] = {
			{ "poi_halte_des_muletiers_du_col", "S008_E019" }, { "poi_borne_frontiere_renversee", "S009_E019" },
			{ "poi_carriere_des_forcats", "S009_E021" }, { "poi_ferme_des_chevriers_nains", "S010_E019" },
			{ "poi_corps_de_garde_de_la_route_du_sud", "S010_E020" }, { "poi_puits_de_mine_inonde", "S010_E021" },
			{ "poi_cairn_des_voyageurs", "N009_E017" }, { "poi_ferme_abandonnee_du_nord", "N009_E018" },
			{ "poi_camp_des_chasseurs_de_loups", "N009_E019" }, { "poi_chantier_de_flottage", "N008_E017" },
			{ "poi_relais_de_la_route_du_nord", "N008_E019" }, { "poi_chene_aux_pendus", "N007_E017" },
			{ "poi_repaire_de_l_ogre_de_la_foret", "N007_E018" }, { "poi_bac_du_passeur", "N007_E019" },
			{ "poi_moulin_a_eau_de_la_riviere", "N007_E020" }, { "poi_pavillon_de_chasse_imperial", "N006_E017" },
			{ "poi_camp_des_deserteurs", "N006_E018" }, { "poi_pont_effondre", "N007_E021" },
			{ "poi_paturages_de_la_cote", "N007_E022" }, { "poi_tour_de_guet_cotiere", "N007_E023" },
			{ "poi_epave_echouee", "N007_E024" }, { "poi_relais_de_la_route_de_riverfold", "N006_E021" },
			{ "poi_caravanserail_des_marchands", "N006_E022" }, { "poi_marais_salants", "N006_E024" },
			{ "poi_ferme_des_colons", "N005_E021" }, { "poi_oratoire_des_voyageurs", "N005_E022" },
			{ "poi_chemin_de_halage", "N005_E023" }, { "poi_cimetiere_des_marins", "N005_E024" },
			{ "poi_gibet_du_carrefour", "N004_E021" }, { "poi_prairie_des_chevaux_sauvages", "N004_E022" },
			{ "poi_grange_aux_dimes", "N004_E023" }, { "poi_pecherie_de_la_cote", "N004_E024" },
			{ "poi_hutte_de_la_rebouteuse", "N003_E021" }, { "poi_repaire_des_coupe_jarrets", "N003_E022" },
			{ "poi_dolmen_de_la_cote", "N003_E024" }, { "poi_ossuaire_du_geant", "N002_E021" },
			{ "poi_rucher_de_la_foret", "N002_E022" }, { "poi_menhirs_des_geants", "N002_E023" },
			{ "poi_cabanes_de_pecheurs_abandonnees", "N002_E024" }, { "poi_source_sacree_de_la_foret", "N001_E021" },
			{ "poi_fosse_a_loups", "N001_E022" }, { "poi_ferme_fortifiee_de_la_cote", "N001_E023" },
			{ "poi_anse_des_naufrageurs", "N001_E024" }, { "poi_puits_de_la_piste", "N000_E021" },
			{ "poi_bergerie_des_chevriers", "N000_E022" }, { "poi_rocher_des_vautours", "N000_E023" },
			{ "poi_fortin_cotier_abandonne", "N000_E024" }, { "poi_carriere_de_la_plaine_aride", "S001_E022" },
			{ "poi_tour_a_feu_de_la_cote", "S001_E024" }, { "poi_tombeau_des_sables", "S002_E022" },
			{ "poi_campement_des_pillards_du_desert", "S002_E023" }, { "poi_crique_des_contrebandiers", "S002_E024" },
		};
		int valid = 0;
		for (const Expected& x : kLieux)
		{
			const PoiDef* p = nullptr;
			for (const PoiDef& q : g.pois)
				if (q.id == x.poi) p = &q;
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.n == p->n && c.e == p->e) alone = false;
			for (const PoiDef& q : g.pois)
				if (p && &q != p && q.cell == p->cell) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone
				&& MacroHeight(m, p->center.x, p->center.z) > m.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.91 invalide : %s\n", x.poi);
			valid += good ? 1 : 0;
		}
		Check(valid == 52, "cellules et lieux v1.0.91 : 52 lieux au sec, seuls sur leur case jouable");
		int poiVilles = 0;
		for (const Ville& v : kVilles)
			for (const PoiDef& q : g.pois) poiVilles += q.cell == v.cell ? 1 : 0;
		Check(poiVilles == 0, "cellules et lieux v1.0.91 : aucun POI sur les cases des villes d'arrivee");
	}

	/// v1.0.104 (lot 29) : ouverture des 5 routes restées coupées au mur invisible en v1.0.91 (Vozmarkh -
	/// Mhordaal, Vozmarkh - Shaddrek, Vozmarkh - Ghrondak, Rhivkhar - Hilvakh, Farvhenn - Rhivkhar) et, en
	/// cascade, des routes entre villes devenues jouables et villes proches (Mhordaal - Khrozrad, Shaddrek -
	/// Khrozrad, Mhordaal - Ghrondak, Shaddrek - Mhordaal, Hilvakh - Gharzuk). 55 cases ajoutées à
	/// `generated_cells` (tracés du lore et du générateur, villes d'arrivée, marge d'une case, trous intérieurs).
	/// Vérifie : les 55 cases sont générées et terrestres ; chacune touche (4-voisinage) une case jouable ;
	/// les 32 cases du sud sont atteintes à pied depuis Durnhalde (S007_E017) et les 23 du nord depuis Grakhul
	/// (N008_E012) sans quitter les cases jouables ; Mhordaal, Shaddrek, Ghrondak, Khrozrad, Hilvakh et Farvhenn
	/// sont jouables ; chaque point des 10 routes du générateur entre ces villes est dans une case jouable, de
	/// même que chaque case des 6 tracés du lore ; un POI [GD] par case vide (48, au sec, seuls sur leur case) ;
	/// aucun POI sur les cases des villes d'arrivée ; la case de Khurden ne porte que son POI ; S008_E022,
	/// S012_E016 et N013_E019 restent fermées.
	void TestCellulesLieuxV1104()
	{
		using namespace tools::world_gen;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "cellules et lieux v1.0.104 : master royaumes charge");
		if (!ok) return;
		static const char* kCells[] = {
			"N012_E019", "N012_E020", "N012_E021", "N011_E015", "N011_E016", "N011_E017",
			"N011_E018", "N011_E019", "N011_E020", "N011_E021", "N010_E015", "N010_E016",
			"N010_E017", "N010_E018", "N010_E019", "N010_E020", "N010_E021", "N009_E014",
			"N009_E015", "N009_E016", "N008_E014", "N008_E015", "N008_E016", "S007_E023",
			"S007_E024", "S007_E025", "S008_E023", "S008_E024", "S008_E025", "S009_E017",
			"S009_E018", "S009_E022", "S009_E023", "S009_E024", "S009_E025", "S010_E017",
			"S010_E018", "S010_E022", "S010_E023", "S011_E017", "S011_E018", "S011_E019",
			"S011_E020", "S011_E021", "S012_E017", "S012_E018", "S012_E019", "S012_E020",
			"S012_E021", "S013_E019", "S013_E020", "S013_E021", "S014_E019", "S014_E020",
			"S014_E021",
		};

		const WorldMaster& m = g.master;
		const std::set<std::string> listed(m.generatedCells.begin(), m.generatedCells.end());
		auto parse = [](const std::string& c, int& n, int& e) {
			n = std::stoi(c.substr(1, 3)) * (c[0] == 'N' ? 1 : -1);
			e = std::stoi(c.substr(6, 3)) * (c[5] == 'E' ? 1 : -1);
		};
		auto name = [](int n, int e) {
			char b[32];
			std::snprintf(b, sizeof(b), "%c%03d_%c%03d", n >= 0 ? 'N' : 'S', std::abs(n), e >= 0 ? 'E' : 'W', std::abs(e));
			return std::string(b);
		};
		int inList = 0, land = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			inList += listed.count(c) ? 1 : 0;
			const char code = LoreCode(m, n, e);
			land += (code != ' ' && code != '-') ? 1 : 0;
		}
		Check(inList == 55 && land == 55, "cellules et lieux v1.0.104 : 55 cases generees, toutes terrestres");
		Check(!listed.count("S008_E022") && !listed.count("S012_E016") && !listed.count("N013_E019"),
			"cellules et lieux v1.0.104 : S008_E022, S012_E016 et N013_E019 restent fermees");
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = m.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		// Connexité : chaque nouvelle case touche une case jouable ; parcours à pied depuis deux départs.
		static const std::pair<int, int> kDirs[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		int touching = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			bool t = false;
			for (const auto& d : kDirs) t = t || playable.count(name(n + d.first, e + d.second)) > 0;
			touching += t ? 1 : 0;
		}
		// Parcours en largeur dans les cases jouables depuis la case \p start (4-voisinage).
		auto reach = [&](const std::string& start) {
			std::set<std::string> seen{ start };
			std::vector<std::string> front{ start };
			while (!front.empty())
			{
				const std::string c = front.back();
				front.pop_back();
				int n = 0, e = 0;
				parse(c, n, e);
				for (const auto& d : kDirs)
				{
					const std::string x = name(n + d.first, e + d.second);
					if (playable.count(x) && seen.insert(x).second) front.push_back(x);
				}
			}
			return seen;
		};
		const std::set<std::string> fromDurnhalde = reach("S007_E017");
		const std::set<std::string> fromGrakhul = reach("N008_E012");
		int reached = 0;
		for (const char* c : kCells)
		{
			int n = 0, e = 0;
			parse(c, n, e);
			reached += (n < -3 ? fromDurnhalde.count(c) : fromGrakhul.count(c)) ? 1 : 0;
		}
		Check(touching == 55 && reached == 55, "cellules et lieux v1.0.104 : aucune case isolee, le sud relie a Durnhalde et le nord a Grakhul");
		// Villes d'arrivée jouables.
		struct Ville { const char* id; const char* cell; };
		static const Ville kVilles[] = { { "town_0108", "S011_E020" }, { "shadowfen", "S011_E018" },
			{ "town_ironbrook", "S008_E024" }, { "crossroads", "S013_E020" }, { "town_0133", "N010_E016" },
			{ "farwatch", "N011_E020" } };
		int villes = 0;
		for (const Ville& v : kVilles)
		{
			const CityDef* c = g.FindCity(v.id);
			villes += (c && c->cell == v.cell && playable.count(c->cell)) ? 1 : 0;
		}
		Check(villes == 6, "cellules et lieux v1.0.104 : Mhordaal, Shaddrek, Ghrondak, Khrozrad, Hilvakh et Farvhenn jouables");
		// Routes du générateur entre les villes concernées.
		static const std::pair<const char*, const char*> kPairs[] = {
			{ "town_0125", "town_0108" }, { "town_0125", "shadowfen" }, { "town_0125", "town_ironbrook" },
			{ "town_0132", "town_0133" }, { "farwatch", "town_0132" }, { "town_0108", "crossroads" },
			{ "shadowfen", "crossroads" }, { "town_0108", "town_ironbrook" }, { "shadowfen", "town_0108" },
			{ "town_0133", "hamlet_gharzuk" },
		};
		auto cellOf = [&](const Vec2& p) {
			return name(-static_cast<int>(std::lround(p.z / 10000.0)), static_cast<int>(std::lround(p.x / 10000.0)));
		};
		int routes = 0, outside = 0;
		// Réseau du générateur (v1.0.105 : g.generatorRoads, base des corridors et du choix des cases).
		for (const Road& r : g.generatorRoads)
		{
			bool match = false;
			for (const auto& pr : kPairs)
				match = match || (r.fromId == pr.first && r.toId == pr.second) || (r.fromId == pr.second && r.toId == pr.first);
			if (!match) continue;
			++routes;
			for (const Vec2& p : r.points) outside += playable.count(cellOf(p)) ? 0 : 1;
		}
		Check(routes == 10 && outside == 0, "cellules et lieux v1.0.104 : 10 routes du generateur entierement dans des cases jouables");
		// Tracés du lore (waypoints, sinon ligne droite entre les deux villes), case par case.
		static const char* kLoreCells[] = {
			"S011_E020", "S010_E020", "S009_E020",                              // Mhordaal - Vozmarkh (000034)
			"N010_E016", "N009_E017", "N008_E018",                              // Hilvakh - Rhivkhar (000103)
			"N011_E020", "N010_E019", "N009_E019", "N008_E018",                 // Farvhenn - Rhivkhar (000105)
			"S013_E020", "S012_E020", "S011_E020",                              // Khrozrad - Mhordaal (000026)
			"S008_E024", "S009_E023", "S010_E022", "S011_E021", "S011_E020",    // Ghrondak - Mhordaal (000163)
			"S011_E018", "S011_E019", "S011_E020",                              // Shaddrek - Mhordaal (000033)
		};
		int lore = 0;
		for (const char* c : kLoreCells) lore += playable.count(c) ? 1 : 0;
		Check(lore == 21, "cellules et lieux v1.0.104 : chaque case des 6 traces du lore est jouable");
		// Lieux [GD] : un POI par case vide.
		struct Expected { const char* poi; const char* cell; };
		static const Expected kLieux[] = {
			{ "poi_enclos_des_bouviers", "N012_E019" }, { "poi_sechoir_a_lin", "N012_E020" },
			{ "poi_moulin_des_maraichers", "N011_E019" }, { "poi_vigie_de_la_crete", "N011_E018" },
			{ "poi_coupe_de_bois_des_orcs", "N012_E021" }, { "poi_campement_des_maquignons", "N011_E021" },
			{ "poi_tertre_des_chefs_de_guerre", "N010_E021" }, { "poi_relais_de_la_route_de_farvhenn", "N010_E020" },
			{ "poi_poste_de_peage_orque", "N010_E019" }, { "poi_cercle_des_menhirs", "N010_E018" },
			{ "poi_taniere_des_loups_de_guerre", "N011_E015" }, { "poi_camp_des_marchands_d_esclaves", "N011_E016" },
			{ "poi_hutte_du_chaman_orque", "N010_E015" }, { "poi_campement_du_lac", "N010_E017" },
			{ "poi_abattis_de_la_lisiere", "N009_E014" }, { "poi_halte_des_caravanes_orques", "N009_E015" },
			{ "poi_poteaux_de_guerre_du_carrefour", "N009_E016" }, { "poi_granges_de_gharzuk", "N008_E014" },
			{ "poi_charbonniere_de_la_lisiere", "N008_E015" }, { "poi_arbre_aux_cranes", "N008_E016" },
			{ "poi_refuge_de_montagne", "S009_E017" }, { "poi_galerie_de_prospection", "S009_E018" },
			{ "poi_belvedere_nain", "S010_E017" }, { "poi_mine_eboulee_de_shaddrek", "S010_E018" },
			{ "poi_col_des_contrebandiers", "S011_E017" }, { "poi_gite_des_carriers", "S012_E017" },
			{ "poi_aiguille_des_vautours", "S012_E018" }, { "poi_relais_des_trois_routes", "S011_E019" },
			{ "poi_champs_d_orge_des_nains", "S012_E019" }, { "poi_rive_des_lavandieres", "S011_E021" },
			{ "poi_potence_de_la_lune_noire", "S012_E020" }, { "poi_puits_d_extraction_de_mhordaal", "S012_E021" },
			{ "poi_carriere_de_khrozrad", "S013_E019" }, { "poi_forge_de_la_crete", "S013_E021" },
			{ "poi_caveau_des_anciens_nains", "S014_E019" }, { "poi_pont_de_corde_de_la_gorge", "S014_E020" },
			{ "poi_taniere_de_l_ours", "S014_E021" }, { "poi_auberge_brulee_de_la_route_de_l_est", "S010_E022" },
			{ "poi_chariots_renverses", "S010_E023" }, { "poi_ermitage_de_la_montagne", "S009_E022" },
			{ "poi_ferme_des_mineurs", "S009_E023" }, { "poi_salines_de_ghrondak", "S009_E024" },
			{ "poi_galerie_de_ghrondak", "S008_E023" }, { "poi_poste_de_guet_nain", "S007_E023" },
			{ "poi_sepultures_des_mineurs", "S007_E024" }, { "poi_phare_nain_eteint", "S007_E025" },
			{ "poi_fumoir_a_poisson", "S008_E025" }, { "poi_plage_d_echouage_des_barques", "S009_E025" },
		};
		int valid = 0;
		for (const Expected& x : kLieux)
		{
			const PoiDef* p = nullptr;
			for (const PoiDef& q : g.pois)
				if (q.id == x.poi) p = &q;
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.n == p->n && c.e == p->e) alone = false;
			for (const PoiDef& q : g.pois)
				if (p && &q != p && q.cell == p->cell) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone
				&& MacroHeight(m, p->center.x, p->center.z) > m.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.104 invalide : %s\n", x.poi);
			valid += good ? 1 : 0;
		}
		Check(valid == 48, "cellules et lieux v1.0.104 : 48 lieux au sec, seuls sur leur case jouable");
		int poiVilles = 0;
		for (const Ville& v : kVilles)
			for (const PoiDef& q : g.pois) poiVilles += q.cell == v.cell ? 1 : 0;
		Check(poiVilles == 0, "cellules et lieux v1.0.104 : aucun POI sur les cases des villes d'arrivee");
		int khurden = 0, onKhurden = 0;
		for (const PoiDef& q : g.pois)
		{
			onKhurden += q.cell == "N011_E017" ? 1 : 0;
			khurden += (q.id == "poi_khurden" && q.cell == "N011_E017") ? 1 : 0;
		}
		Check(khurden == 1 && onKhurden == 1 && playable.count("N011_E017"),
			"cellules et lieux v1.0.104 : case de Khurden jouable, avec son seul POI");
	}

	void TestWaterShores()
	{
		using namespace tools::world_gen;
		// 1) Cuvettes : grille 40×40 à 45 m ; mer (biome '-') à gauche, une cuvette
		//    isolée au centre, une autre au bord droit (prudence : conservée).
		Grid<double> h;
		Grid<char> b;
		h.Init(40, 40, 0.0, 0.0, 5.0, 45.0);
		b.Init(40, 40, 0.0, 0.0, 5.0, '.');
		for (int j = 0; j < 40; ++j)
			for (int i = 0; i < 6; ++i) { h.At(i, j) = 30.0; b.At(i, j) = '-'; }
		for (int j = 5; j < 10; ++j) h.At(6, j) = 39.0;           // chenal relié à la mer
		for (int j = 18; j < 22; ++j)
			for (int i = 18; i < 22; ++i) h.At(i, j) = 38.0;     // cuvette isolée
		for (int j = 30; j < 33; ++j) h.At(39, j) = 39.5;        // touche le bord
		const size_t n = FillLandlockedSea(h, b, 40.0, 40.5);
		Check(n == 16, "EAU.5 : cuvette isolee comblee (16 echantillons)");
		Check(h.At(19, 19) == 40.5 && h.At(3, 3) == 30.0 && h.At(6, 7) == 39.0,
			"EAU.5 : mer et chenal relie conserves, cuvette relevee");
		Check(h.At(39, 31) == 39.5, "EAU.5 : cuvette au bord de la cellule conservee");
		Grid<char> noBiome;
		Grid<double> h2 = h;
		h2.At(20, 20) = 39.0;
		Check(FillLandlockedSea(h2, noBiome, 40.0, 40.5) == 1, "EAU.5 : sans biome, seul le bord relie a la mer");

		// 2) Lac sur une pente (sol de 60 m à l'ouest à 20 m à l'est), eau à 40 m.
		Lake lake;
		lake.center = { 500.0, 500.0 };
		lake.radiusM = 100.0;
		lake.waterY = 40.0;
		for (int k = 0; k < 32; ++k)
		{
			const double a = 6.283185307179586 * k / 32;
			const double rr = 100.0 * (k % 2 ? 1.08 : 0.92);
			lake.polygon.push_back({ 500.0 + std::cos(a) * rr, 500.0 + std::sin(a) * rr });
		}
		Check(std::fabs(LakeRadiusAt(lake, 0.0) - 92.0) < 1e-6
			&& std::fabs(LakeRadiusAt(lake, 6.283185307179586 / 32) - 108.0) < 1e-6
			&& std::fabs(LakeRadiusAt(lake, 6.283185307179586 / 64) - 100.0) < 1e-6
			&& std::fabs(LakeRadiusAt(lake, -6.283185307179586 / 32) - 108.0) < 1e-6,
			"EAU.5 : rayon du lac interpole entre les sommets du polygone");
		Grid<double> slope;
		slope.Init(201, 201, 0.0, 0.0, 5.0, 0.0);
		for (int j = 0; j < 201; ++j)
			for (int i = 0; i < 201; ++i) slope.At(i, j) = 60.0 - 40.0 * (slope.WorldX(i) / 1000.0);
		Grid<double> withRiver = slope;
		Grid<float> rivers;
		rivers.Init(201, 201, 0.0, 0.0, 5.0, 1e9f);
		for (int i = 0; i < 201; ++i) rivers.At(i, 100) = 0.0f;   // lit le long de z = 500
		CarveLake(slope, Grid<float>{}, Grid<uint8_t>{}, lake, 0.0);
		CarveLake(withRiver, rivers, Grid<uint8_t>{}, lake, 0.0);
		bool bedOk = true, rimOk = true;
		for (int k = 0; k < 64; ++k)
		{
			const double a = 6.283185307179586 * k / 64;
			const double rr = LakeRadiusAt(lake, a);
			const Vec2 in{ 500.0 + std::cos(a) * (rr - 13.0), 500.0 + std::sin(a) * (rr - 13.0) };
			const Vec2 rim{ 500.0 + std::cos(a) * (rr + 3.0), 500.0 + std::sin(a) * (rr + 3.0) };
			if (SampleBilinear(slope, in.x, in.z) >= lake.waterY) bedOk = false;
			if (SampleBilinear(slope, rim.x, rim.z) < lake.waterY) rimOk = false;
		}
		Check(bedOk, "EAU.5 : fond sous l'eau dans le polygone (hors bande du bord)");
		Check(rimOk, "EAU.5 : rive au-dessus de l'eau tout autour (digue cote aval)");
		Check(std::fabs(SampleBilinear(slope, 900.0, 500.0) - (60.0 - 40.0 * 0.9)) < 1e-9,
			"EAU.5 : relief d'origine retrouve au-dela de la digue");
		Check(SampleBilinear(slope, 600.0, 500.0) >= lake.waterY && SampleBilinear(withRiver, 600.0, 500.0) < lake.waterY,
			"EAU.5 : digue sauf sur un lit de riviere");
		Grid<double> flat;
		flat.Init(201, 201, 0.0, 0.0, 5.0, 45.0);
		CarveLake(flat, Grid<float>{}, Grid<uint8_t>{}, lake, 39.0);
		Check(std::fabs(SampleBilinear(flat, 500.0, 500.0) - 39.0) < 1e-9, "EAU.5 : fond du lac jamais sous la mer");
	}

	/// v1.0.95 : placement manuel des villes et des lieux (« tu places, le jeu suit »).
	///  1. Master du dépôt (aucune position) : sites identiques à ceux du générateur
	///     v1.0.94, relevés avant la modification (ville cherchée, ville ancrée sur un
	///     relief, hameau de départ, POI écartés, POI décalé).
	///  2. Master temporaire où Altenberg et le temple de Methraton ont une `position` :
	///     posés exactement là (tolérance 1 m), plateau aplani autour ; toutes les
	///     autres villes et les POI des autres cases sont inchangés.
	///  3. Positions refusées : hors de la case (> 4300 m), ville ancrée sur un relief.
	/// Effet de bord : écrit puis efface un dossier temporaire (master modifié).
	void TestPlacementManuelV1095()
	{
		using namespace tools::world_gen;
		namespace fs = std::filesystem;
		auto near = [](Vec2 a, double x, double z, double tol) { return std::fabs(a.x - x) <= tol && std::fabs(a.z - z) <= tol; };

		// 1. Sans position : comportement inchangé.
		GlobalWorld g0;
		std::string err;
		const bool ok0 = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g0.master, err);
		if (!ok0) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok0, "placement v1.0.95 : master charge");
		if (!ok0) return;
		// v1.0.106 : premières positions manuelles du dépôt, les 4 balises de route replacées au
		// bord des routes du lore (LORE_BD §16.35) ; aucune ville, aucun POI des sites ci-dessous.
		// v1.0.107 : Thurvhal, au fond du gouffre de Deathtrap Canyon (relief qui suit la
		// ville), est la seule ville à position manuelle.
		bool none = true;
		for (const CityDef& c : g0.master.cities) none = none && (!c.hasPosition || c.id == "ville_thurvhal");
		const std::set<std::string> balises = { "poi_balise_borne_geants_nord", "poi_balise_borne_geants_ouest",
			"poi_balise_borne_geants_sud", "poi_stele_des_deux_fois_nes" };
		size_t posees = 0;
		for (const PoiDef& p : g0.master.pois)
		{
			if (!p.hasPosition) continue;
			none = none && balises.count(p.id);
			++posees;
		}
		Check(none && posees == balises.size(), "placement v1.0.95 : aucune ville deplacee (sauf Thurvhal, v1.0.107) ; seules les 4 balises de route ont une position manuelle (v1.0.106)");
		BuildMacroTerrain(g0);
		const CityDef* lhynn = g0.FindCity("ville_lhynn");
		const CityDef* alt0 = g0.FindCity("ville_altenberg");
		const CityDef* ver = g0.FindCity("town_0119");
		const CityDef* orh = g0.FindCity("hameau_dawnmere");
		Check(lhynn && near(lhynn->center, -99750.0, -10250.0, 1e-4) && std::fabs(lhynn->plateauY - 55.419153) < 1e-4,
			"placement v1.0.95 : sans position, Lhynn au site choisi en v1.0.94");
		Check(alt0 && near(alt0->center, -120000.0, -50000.0, 1e-4) && std::fabs(alt0->plateauY - 63.038415) < 1e-4,
			"placement v1.0.95 : sans position, Altenberg au site choisi en v1.0.94");
		Check(ver && near(ver->center, -150000.0, -100000.0, 1e-4), "placement v1.0.95 : Vaerdhun (colline) au centre de sa case");
		Check(orh && near(orh->center, -110000.0, -50000.0, 1e-4), "placement v1.0.95 : Orhendal (depart) au centre de sa case");
		const PoiDef* temple0 = nullptr;
		bool poisRef = true;
		for (const PoiDef& p : g0.pois)
		{
			if (p.id == "poi_temple_methraton") { temple0 = &p; poisRef = poisRef && near(p.center, -100099.310750, -9900.689250, 1e-4); }
			if (p.id == "poi_seuil_lhynn") poisRef = poisRef && near(p.center, -100099.310750, -10599.310750, 1e-4);
			if (p.id == "poi_sanctuaire_des_brisants") poisRef = poisRef && near(p.center, -163200.0, -100700.0, 1e-4);
		}
		Check(temple0 && poisRef, "placement v1.0.95 : sans position, POI ecartes ou decales comme en v1.0.94");

		// 2. Master temporaire avec deux positions manuelles.
		const std::string text = ReadText(fs::path(LCDLLN_CONTENT_DIR) / "world/royaumes/world_master.json");
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_placement_v1095";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		// Écrit `content` comme master royaumes sous tmp/<sub> et le charge dans \p gw.
		auto loadWith = [&](const std::string& sub, const std::string& content, GlobalWorld& gw, std::string& e) {
			const fs::path dir = tmp / sub / "world" / "royaumes";
			fs::create_directories(dir, ec);
			{ std::ofstream f(dir / "world_master.json", std::ios::binary); f << content; }
			return LoadWorldMaster((tmp / sub).string(), "royaumes", gw.master, e);
		};
		// Insère `"position": {…}` juste après l'identifiant de l'entrée \p id.
		auto withPosition = [](std::string t, const std::string& id, const std::string& pos) {
			const std::string key = "\"id\": \"" + id + "\",";
			const size_t at = t.find(key);
			if (at != std::string::npos) t.insert(at + key.size(), " \"position\": " + pos + ",");
			return t;
		};
		std::string moved = withPosition(text, "ville_altenberg", "{ \"x_m\": 1234.5, \"z_m\": -2100 }");
		moved = withPosition(moved, "poi_temple_methraton", "{ \"x_m\": -3000, \"z_m\": 2500 }");
		moved = withPosition(moved, "ville_lhynn", "null");
		GlobalWorld g;
		const bool ok = loadWith("a", moved, g, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "placement v1.0.95 : master avec positions charge (position null admise)");
		if (ok)
		{
			BuildMacroTerrain(g);
			const CityDef* alt = g.FindCity("ville_altenberg");
			Check(alt && alt->hasPosition && near(alt->center, -120000.0 + 1234.5, -50000.0 - 2100.0, 1.0),
				"placement v1.0.95 : Altenberg posee a sa position manuelle (1 m)");
			const CityDef* lh = g.FindCity("ville_lhynn");
			Check(lh && !lh->hasPosition && near(lh->center, -99750.0, -10250.0, 1e-4), "placement v1.0.95 : position null = site automatique");
			const PoiDef* temple = nullptr;
			for (const PoiDef& p : g.pois) if (p.id == "poi_temple_methraton") temple = &p;
			Check(temple && near(temple->center, -100000.0 - 3000.0, -10000.0 + 2500.0, 1.0),
				"placement v1.0.95 : temple de Methraton pose a sa position manuelle (1 m)");
			// Autres villes et POI des autres cases : exactement comme sans position.
			bool same = g.cities.size() == g0.cities.size() && g.pois.size() == g0.pois.size();
			for (size_t k = 0; same && k < g.cities.size(); ++k)
				if (g.cities[k].id != "ville_altenberg")
					same = near(g.cities[k].center, g0.cities[k].center.x, g0.cities[k].center.z, 0.0) && g.cities[k].plateauY == g0.cities[k].plateauY;
			for (size_t k = 0; same && k < g.pois.size(); ++k)
				if (g.pois[k].cell != "N005_W012" && g.pois[k].cell != "N001_W010")
					same = near(g.pois[k].center, g0.pois[k].center.x, g0.pois[k].center.z, 0.0);
			Check(same, "placement v1.0.95 : autres villes et POI des autres cases inchanges");
			// Cellule d'Altenberg : plateau aplani autour de la position manuelle.
			if (alt)
			{
				World w;
				InitCell(w, g, { alt->n, alt->e });
				BuildPlaces(w);
				BuildCellTerrain(w);
				FlattenSettlements(w);
				const double h = w.HeightAt(alt->center);
				const int i = w.urban.IndexX(alt->center.x), j = w.urban.IndexZ(alt->center.z);
				Check(std::fabs(h - alt->plateauY) < 0.5 && w.urban.InBounds(i, j) && w.urban.At(i, j) == 1,
					"placement v1.0.95 : plateau urbain aplani a la position manuelle");
			}
		}

		// 3. Positions refusées.
		GlobalWorld gBad;
		std::string e1, e2;
		const bool out = loadWith("b", withPosition(text, "ville_altenberg", "{ \"x_m\": 4400, \"z_m\": 0 }"), gBad, e1);
		Check(!out && e1.find("hors de la case") != std::string::npos, "placement v1.0.95 : position hors de la case refusee (> 4300 m)");
		const bool relief = loadWith("c", withPosition(text, "town_0119", "{ \"x_m\": 500, \"z_m\": 500 }"), gBad, e2);
		Check(!relief && e2.find("relief") != std::string::npos, "placement v1.0.95 : ville ancree sur un relief non deplacable");
		fs::remove_all(tmp, ec);
	}

	/// v1.0.105 : le générateur suit les routes du lore (`lore_roads` du master, recopiées
	/// par tools/lore/sync_routes.py). Chaque route passe par chaque case de son tracé ;
	/// un embranchement se raccorde à sa route hôte ; la Via Magna est tracée de Shezhold
	/// à Moork par Horkher et Nykhfeld dans des cellules jouables ; seules les routes du
	/// générateur des corridors de départ restent (pas de route parasite) ; cellules
	/// jouables inchangées ; deux constructions identiques.
	void TestRoutesLoreV1105()
	{
		using namespace tools::world_gen;
		namespace wc = engine::world::cells;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok && g.master.loreRoads.size() >= 150, "routes du lore v1.0.105 : lore_roads charge (>= 150 routes)");
		if (!ok) return;
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		auto cellOf = [](Vec2 p) { return wc::LoreCellId(wc::CellFromPosition(p.x, p.z)); };
		std::map<std::string, std::vector<const Road*>> byLore;
		for (const Road& r : g.roads)
			if (!r.loreId.empty()) byLore[r.loreId].push_back(&r);
		// 1) Chaque route du lore est tracée et passe DANS chaque case de son tracé. Jusqu'à la
		//    v1.0.105, 000017 (Atmosphère → case enclavée du Palais des Maîtres des Dragons)
		//    ne l'était pas ; v1.0.106 : tracé imposé par la Passe des Dragons (S002_W003).
		size_t traced = 0, missed = 0;
		std::set<std::string> untraced;
		for (const LoreRoadDef& d : g.master.loreRoads)
		{
			const auto it = byLore.find(d.id);
			if (it == byLore.end()) { untraced.insert(d.id); continue; }
			++traced;
			std::set<std::string> cells;
			for (const Road* r : it->second)
				for (const Vec2& p : r->points) cells.insert(cellOf(p));
			for (size_t k = 1; k + 1 < d.waypoints.size(); ++k)
				if (!cells.count(d.waypoints[k])) { ++missed; std::fprintf(stderr, "  %s ne passe pas par %s\n", d.id.c_str(), d.waypoints[k].c_str()); }
		}
		Check(traced == g.master.loreRoads.size() && untraced.empty() && missed == 0,
			"routes du lore v1.0.105 : toutes tracees (000017 comprise depuis v1.0.106), chacune passe dans chaque case de son trace");
		// 2) Embranchements : 000155 part d'un point de 000079 (en S007_E006), 000130 arrive
		//    sur un point de la Via Magna 000120 (en S001_W009).
		auto startsOn = [&](const char* branch, const char* host, bool atStart, const char* cell) {
			const auto b = byLore.find(branch), h = byLore.find(host);
			if (b == byLore.end() || h == byLore.end()) return false;
			const Vec2 e = atStart ? b->second.front()->points.front() : b->second.back()->points.back();
			if (cellOf(e) != cell) return false;
			for (const Road* r : h->second)
				for (const Vec2& p : r->points)
					if (std::hypot(p.x - e.x, p.z - e.z) < 1e-6) return true;
			return false;
		};
		Check(startsOn("000155", "000079", true, "S007_E006") && startsOn("000130", "000120", false, "S001_W009"),
			"routes du lore v1.0.105 : embranchements raccordes a leur route hote, dans la case de jonction");
		// 3) Via Magna : coupée aux villes traversées ; de Shezhold à Moork par Horkher et
		//    Nykhfeld, entièrement dans des cellules jouables.
		std::vector<std::string> ids = g.master.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		Check(playable.size() == 481, "routes du lore v1.0.105 : 481 cellules jouables (413 + 68 cases de passage ouvertes en v1.0.106)");
		const std::pair<const char*, const char*> via[] = {
			{ "town_0136", "city_horkher" }, { "city_horkher", "hamlet_0201" }, { "hamlet_0201", "ville_moork" } };
		int viaOk = 0;
		for (const auto& [a, b] : via)
			for (const Road* r : byLore["000120"])
				if (r->fromId == a && r->toId == b && r->major && r->widthM >= 7.0)
				{
					bool in = true;
					for (const Vec2& p : r->points) in = in && playable.count(cellOf(p));
					viaOk += in;
				}
		Check(viaOk == 3, "routes du lore v1.0.105 : Via Magna Shezhold - Horkher - Nykhfeld - Moork tracee dans des cellules jouables");
		// 4) Pas de route parasite : hors routes du lore, seules des routes du générateur
		//    des corridors de départ, à l'identique ; N019_W012 (haut-fond de la baie de l'Île
		//    de la Trêve, route Brenhaval - Duzhmoroth du générateur, sans route au lore) n'a
		//    plus de route. (v1.0.105 : N011_W013, qui a une route du lore depuis v1.0.106.)
		const std::vector<size_t> corridor = StartCorridorRoads(g, g.generatorRoads, false);
		size_t generator = 0, parasite = 0;
		for (const Road& r : g.roads)
		{
			if (!r.loreId.empty()) continue;
			++generator;
			bool found = false;
			for (size_t k : corridor)
				found = found || (g.generatorRoads[k].fromId == r.fromId && g.generatorRoads[k].toId == r.toId
					&& g.generatorRoads[k].points.size() == r.points.size());
			parasite += !found;
		}
		bool hadRoad = false, hasRoad = false;
		for (const Road& r : g.generatorRoads) for (const Vec2& p : r.points) hadRoad = hadRoad || cellOf(p) == "N019_W012";
		for (const Road& r : g.roads) for (const Vec2& p : r.points) hasRoad = hasRoad || cellOf(p) == "N019_W012";
		Check(generator > 0 && parasite == 0 && playable.count("N019_W012") && hadRoad && !hasRoad,
			"routes du lore v1.0.105 : routes du generateur gardees = corridors de depart ; case sans route du lore sans route parasite");
		// 5) Sentiers et voies : types respectés.
		bool types = true;
		for (const LoreRoadDef& d : g.master.loreRoads)
			for (const Road* r : byLore[d.id])
				types = types && (r->path == (d.type == "path")) && (d.type != "imperial_road" || r->major);
		Check(types, "routes du lore v1.0.105 : sentiers (3,2 m) et voie imperiale (7 m) selon le type du lore");
		// 6) Déterminisme : une seconde construction donne exactement le même réseau.
		GlobalWorld h;
		LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", h.master, err);
		BuildMacroTerrain(h);
		BuildMacroRivers(h);
		BuildMacroRoads(h);
		bool same = g.roads.size() == h.roads.size();
		for (size_t i = 0; same && i < g.roads.size(); ++i)
		{
			const Road& a = g.roads[i];
			const Road& b = h.roads[i];
			same = a.loreId == b.loreId && a.fromId == b.fromId && a.toId == b.toId && a.points.size() == b.points.size()
				&& a.profileY == b.profileY;
			for (size_t k = 0; same && k < a.points.size(); ++k) same = a.points[k].x == b.points[k].x && a.points[k].z == b.points[k].z;
		}
		Check(same, "routes du lore v1.0.105 : deux constructions identiques (deterministe)");
	}
	/// v1.0.106 (lot 30b) : routes manquantes après le passage aux routes du lore (LORE_BD §16.35).
	/// (a) chaque ville et hameau d'une cellule jouable touche une route du lore tracée en jeu (extrémité
	///     ou à moins de 400 m) ; tous sont reliés à pied, par des routes entièrement jouables, à l'un des
	///     départs, sauf les villes de l'Île de la Trêve (île, réseau propre) ;
	/// (b) les 5 balises de route (3 Bornes des Géants, Stèle des Deux Fois Nés, Borne des Monts de Moork)
	///     sont à moins de 500 m d'une route tracée, au sec ;
	/// (c) aucune route du lore ne sort des cellules jouables pour y revenir (passage par une case non
	///     générée) ; exceptions documentées : les routes qui mènent à une zone fermée (elles s'arrêtent
	///     au mur invisible) ;
	/// et : 68 cases ouvertes, terrestres ; 11 routes créées tracées ; 000017 par la Passe des Dragons ;
	/// 64 POI au sec, seuls sur leur case ; les 5 cases de haut-fond de la baie de l'Île de la Trêve
	/// (exception documentée) restent sans route.
	void TestRoutesManquantesV1106()
	{
		using namespace tools::world_gen;
		namespace wc = engine::world::cells;
		GlobalWorld g;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "routes v1.0.106 : master royaumes charge");
		if (!ok) return;
		static const char* kCells[] = {
			"N000_W009", "S001_W009", "S001_W008", "S001_W010", "S002_W007", "S002_W008",
			"N000_W007", "N000_W006", "N001_W006", "N001_W005", "N002_W005", "N002_W004",
			"N001_W003", "N000_W003", "S001_W003", "S002_W003", "N000_E004", "N000_E005",
			"N000_E006", "N001_E006", "N002_E006", "N002_E007", "N003_E007", "N003_E008",
			"N004_E008", "S006_E014", "S005_E014", "S005_E013", "S004_E013", "S003_E013",
			"S003_E012", "S002_E005", "N009_E000", "N008_E000", "N008_E001", "N007_E001",
			"N009_E007", "N008_E008", "N008_E009", "N002_E012", "N002_W001", "N003_W001",
			"N003_E000", "N004_E000", "N004_E001", "N005_E001", "N005_E002", "N006_E002",
			"N000_E003", "N000_W002", "N001_W004", "N003_E012", "N003_W002", "N006_E001",
			"N009_E008", "S001_E005", "S002_W009", "S003_E005", "S003_E014", "S003_W007",
			"S004_E011", "S005_E015", "S003_W006", "S004_E012", "S004_E014", "S004_E015",
			"S005_E010", "S005_E012",
		};
		const WorldMaster& m = g.master;
		const std::set<std::string> listed(m.generatedCells.begin(), m.generatedCells.end());
		int inList = 0, land = 0, nCells = 0;
		for (const char* c : kCells)
		{
			++nCells;
			const int n = std::stoi(std::string(c).substr(1, 3)) * (c[0] == 'N' ? 1 : -1);
			const int e = std::stoi(std::string(c).substr(6, 3)) * (c[5] == 'E' ? 1 : -1);
			inList += listed.count(c) ? 1 : 0;
			const char code = LoreCode(m, n, e);
			land += (code != ' ' && code != '-') ? 1 : 0;
		}
		Check(nCells == 68 && inList == 68 && land == 68, "routes v1.0.106 : 68 cases de passage ouvertes, toutes terrestres");
		BuildMacroTerrain(g);
		BuildMacroRivers(g);
		BuildMacroRoads(g);
		std::vector<std::string> ids = m.generatedCells;
		AddStartCorridors(g, ids);
		const std::set<std::string> playable(ids.begin(), ids.end());
		auto cellOf = [](Vec2 p) { return wc::LoreCellId(wc::CellFromPosition(p.x, p.z)); };
		std::map<std::string, std::vector<const Road*>> byLore;
		for (const Road& r : g.roads)
			if (!r.loreId.empty()) byLore[r.loreId].push_back(&r);

		// Routes créées et 000017.
		int created = 0;
		for (const char* id : { "000228", "000229", "000230", "000231", "000232", "000233", "000234", "000235", "000236", "000237", "000238" })
			created += byLore.count(id) ? 1 : 0;
		bool pass = false;
		for (const Road* r : byLore["000017"])
			for (const Vec2& p : r->points) pass = pass || cellOf(p) == "S002_W003";
		Check(created == 11 && pass, "routes v1.0.106 : 11 routes creees tracees ; 000017 par la Passe des Dragons (S002_W003)");

		// (a) Villes et hameaux jouables touchés par une route du lore.
		auto distTo = [](const Road& r, Vec2 c) {
			double d = 1e18;
			for (const Vec2& q : r.points) d = std::min(d, std::hypot(q.x - c.x, q.z - c.z));
			return d;
		};
		// v1.0.107 : une ville secrète (Thurvhal, au fond du gouffre de Deathtrap Canyon) n'a
		// volontairement aucune route (test « gouffre v1.0.107 ») : hors de ce décompte.
		int villes = 0, touched = 0;
		for (const CityDef& c : g.cities)
		{
			if (!c.procedural.empty() || !playable.count(c.cell) || c.secret) continue;
			++villes;
			bool t = false;
			for (const auto& [id, rs] : byLore)
				for (const Road* r : rs) t = t || r->fromId == c.id || r->toId == c.id || distTo(*r, c.center) < 400.0;
			if (!t) std::fprintf(stderr, "  ville jouable sans route du lore : %s\n", c.id.c_str());
			touched += t ? 1 : 0;
		}
		Check(villes > 70 && touched == villes, "routes v1.0.106 : (a) chaque ville et hameau jouable touche une route du lore tracee");
		// Réseau à pied : routes entièrement dans des cellules jouables, liées par une extrémité
		// commune (ville) ou une extrémité à moins de 120 m d'une autre route.
		std::vector<const Road*> net;
		for (const Road& r : g.roads)
		{
			bool in = true;
			for (const Vec2& p : r.points) in = in && playable.count(cellOf(p));
			if (in) net.push_back(&r);
		}
		std::vector<size_t> par(net.size());
		for (size_t i = 0; i < par.size(); ++i) par[i] = i;
		std::function<size_t(size_t)> root = [&](size_t a) { return par[a] == a ? a : par[a] = root(par[a]); };
		for (size_t i = 0; i < net.size(); ++i)
			for (size_t j = 0; j < net.size(); ++j)
			{
				if (i == j || root(i) == root(j)) continue;
				const Road& a = *net[i];
				const Road& b = *net[j];
				bool link = (!a.fromId.empty() && (a.fromId == b.fromId || a.fromId == b.toId))
					|| (!a.toId.empty() && (a.toId == b.fromId || a.toId == b.toId));
				for (const Vec2& e : { a.points.front(), a.points.back() })
					for (size_t k = 0; !link && k < b.points.size(); k += 4)
						link = std::hypot(b.points[k].x - e.x, b.points[k].z - e.z) < 120.0;
				if (link) par[root(i)] = root(j);
			}
		std::set<size_t> startRoots;
		for (size_t i = 0; i < net.size(); ++i)
			for (const CityDef& c : g.cities)
				if (!c.startFor.empty() && (net[i]->fromId == c.id || net[i]->toId == c.id)) startRoots.insert(root(i));
		int reliees = 0, ile = 0;
		for (const CityDef& c : g.cities)
		{
			if (!c.procedural.empty() || !playable.count(c.cell) || c.secret) continue;
			if (c.n >= 17 && c.e <= -9) { ++ile; continue; } // Île de la Trêve
			bool r = false;
			for (size_t i = 0; i < net.size(); ++i)
				r = r || ((net[i]->fromId == c.id || net[i]->toId == c.id) && startRoots.count(root(i)));
			if (!r) std::fprintf(stderr, "  ville jouable hors du reseau des departs : %s\n", c.id.c_str());
			reliees += r ? 1 : 0;
		}
		Check(startRoots.size() == 1 && reliees + ile == villes && ile == 4,
			"routes v1.0.106 : (a) villes jouables reliees a pied aux departs (Orkh, Marn, Fhelvakt, Hazhaelin, Feyhin, Kholvard compris ; ile a part)");

		// (b) Balises de route.
		int balises = 0;
		for (const char* id : { "poi_balise_borne_geants_nord", "poi_balise_borne_geants_ouest", "poi_balise_borne_geants_sud",
			"poi_stele_des_deux_fois_nes", "poi_borne_monts_de_moork" })
			for (const PoiDef& p : g.pois)
			{
				if (p.id != id) continue;
				double d = 1e18;
				for (const Road& r : g.roads) d = std::min(d, distTo(r, p.center));
				const bool good = d < 500.0 && playable.count(p.cell) && MacroHeight(m, p.center.x, p.center.z) > m.scale.seaLevelM + 5.0;
				if (!good) std::fprintf(stderr, "  balise %s a %.0f m d'une route\n", id, d);
				balises += good ? 1 : 0;
			}
		Check(balises == 5, "routes v1.0.106 : (b) 5 balises de route a moins de 500 m d'une route tracee, au sec");

		// (c) Aucun passage d'une route du lore par une case non générée entre deux points jouables.
		int passages = 0, versFerme = 0;
		for (const auto& [id, rs] : byLore)
			for (const Road* r : rs)
			{
				bool seenIn = false, out = false, any = false;
				for (const Vec2& p : r->points)
				{
					const bool in = playable.count(cellOf(p)) > 0;
					if (in && out && seenIn) { ++passages; std::fprintf(stderr, "  passage hors zone : %s (%s)\n", id.c_str(), cellOf(p).c_str()); out = false; }
					if (in) seenIn = true; else if (seenIn) out = true;
					any = any || !in;
				}
				versFerme += any ? 1 : 0;
			}
		Check(passages == 0 && versFerme > 0, "routes v1.0.106 : (c) aucune route du lore ne traverse une case non generee entre deux zones jouables");

		// Lieux [GD] : au sec, seuls sur leur case jouable.
		struct Expected { const char* poi; const char* cell; };
		static const Expected kLieux[] = {
			{ "poi_bornes_milliaires_de_la_via_magna", "N000_W009" }, { "poi_relais_de_poste_imperial", "S001_W009" },
			{ "poi_champs_de_la_couronne", "S001_W008" }, { "poi_pres_de_la_route_de_dhuzmar", "S001_W010" },
			{ "poi_moulin_a_vent_abandonne", "S002_W009" }, { "poi_halte_des_charrois_de_kholvard", "S002_W007" },
			{ "poi_fours_a_minerai_de_kholvard", "S002_W008" }, { "poi_ancienne_galerie_de_kholvard", "S003_W007" },
			{ "poi_abri_des_bergers_de_la_corniche", "S003_W006" }, { "poi_croix_des_pelerins_de_moros", "N000_W006" },
			{ "poi_puits_de_moros", "N001_W006" }, { "poi_tombeaux_des_caravaniers", "N001_W005" },
			{ "poi_fortin_de_la_via_magna", "N002_W005" }, { "poi_citerne_imperiale", "N002_W004" },
			{ "poi_nid_des_scarabees_geants", "N001_W004" }, { "poi_camp_des_pelerins_de_la_passe", "N001_W003" },
			{ "poi_bivouac_sous_l_atmosphere", "N000_W003" }, { "poi_passe_des_dragons", "S002_W003" },
			{ "poi_mas_abandonne_de_la_plaine", "N000_W002" }, { "poi_scierie_de_la_lisiere", "N000_E003" },
			{ "poi_lisiere_des_bucherons_de_garundel", "N000_E004" }, { "poi_chapelle_de_la_via_magna", "N000_E005" },
			{ "poi_campement_des_gardes_de_la_voie", "N000_E006" }, { "poi_relais_des_sables", "N001_E006" },
			{ "poi_arche_de_la_voie", "N002_E007" }, { "poi_tertre_des_voyageurs_perdus", "N003_E007" },
			{ "poi_bivouac_des_caravaniers_d_harrodh", "N003_E008" }, { "poi_postes_de_guet_d_harrodh", "N004_E008" },
			{ "poi_fumerolles_de_la_baronnie", "S006_E014" }, { "poi_gibet_de_la_baronnie", "S005_E014" },
			{ "poi_caravanserail_pille", "S005_E013" }, { "poi_camp_des_esclavagistes_de_moork", "S004_E013" },
			{ "poi_puits_amer", "S003_E013" }, { "poi_menhirs_du_sel", "S003_E012" },
			{ "poi_tranchee_des_forcats", "S003_E014" }, { "poi_tour_de_signaux_de_moork", "S004_E012" },
			{ "poi_bergerie_des_nomades", "S004_E014" }, { "poi_autel_de_la_lune_noire", "S004_E015" },
			{ "poi_abri_sous_roche_des_contrebandiers", "S005_E015" }, { "poi_tombes_ensablees", "S005_E012" },
			{ "poi_cercle_des_fees", "S002_E005" }, { "poi_souche_creuse_des_lutins", "S001_E005" },
			{ "poi_clairiere_des_mantes", "S003_E005" }, { "poi_ferme_des_orties", "S004_E011" },
			{ "poi_chaumiere_de_l_herboriste", "S005_E010" }, { "poi_empreintes_du_geant", "N009_E000" },
			{ "poi_camp_des_chasseurs_de_geants", "N008_E000" }, { "poi_ossements_de_geant", "N008_E001" },
			{ "poi_poste_de_guet_elfe", "N007_E001" }, { "poi_abattis_des_geants", "N009_E007" },
			{ "poi_fosse_aux_betes_de_feyhin", "N008_E008" }, { "poi_camp_des_rabatteurs_orcs", "N008_E009" },
			{ "poi_affut_des_chasseurs_de_feyhin", "N009_E008" }, { "poi_tour_de_guet_orque", "N002_E012" },
			{ "poi_parc_a_chevaux_des_orcs", "N003_E012" }, { "poi_tumulus_de_la_plaine_de_magistrya", "N003_W002" },
			{ "poi_halte_des_pelerins_de_l_oracle", "N002_W001" }, { "poi_vergers_abandonnes", "N003_W001" },
			{ "poi_champs_d_orge_d_arkhanyra", "N003_E000" }, { "poi_moulin_d_arkhanyra", "N004_E000" },
			{ "poi_menhir_aux_elfes", "N005_E001" }, { "poi_clairiere_des_chasseurs_elfes", "N005_E002" },
			{ "poi_ruines_du_domaine_elfique", "N006_E001" }, { "poi_cabane_du_garde_forestier", "N006_E002" },
		};
		int valid = 0, nLieux = 0;
		for (const Expected& x : kLieux)
		{
			++nLieux;
			const PoiDef* p = nullptr;
			for (const PoiDef& q : g.pois)
				if (q.id == x.poi) p = &q;
			bool alone = true;
			for (const CityDef& c : g.cities)
				if (p && c.procedural.empty() && c.n == p->n && c.e == p->e) alone = false;
			for (const PoiDef& q : g.pois)
				if (p && &q != p && q.cell == p->cell) alone = false;
			const bool good = p && p->cell == x.cell && playable.count(p->cell) && alone
				&& MacroHeight(m, p->center.x, p->center.z) > m.scale.seaLevelM + 5.0;
			if (!good) std::fprintf(stderr, "  lieu v1.0.106 invalide : %s\n", x.poi);
			valid += good ? 1 : 0;
		}
		Check(nLieux == 64 && valid == 64, "routes v1.0.106 : 64 lieux au sec, seuls sur leur case jouable");

		// Exception documentée : haut-fond de la baie de l'Île de la Trêve, sans route.
		int baie = 0;
		for (const char* c : { "N018_W012", "N019_W012", "N020_W012", "N021_W011", "N021_W012" })
		{
			bool road = false;
			for (const Road& r : g.roads) for (const Vec2& p : r.points) road = road || cellOf(p) == c;
			baie += (playable.count(c) && !road) ? 1 : 0;
		}
		Check(baie == 5, "routes v1.0.106 : 5 cases de haut-fond de la baie de l'Ile de la Treve jouables, sans route (exception documentee)");
	}

	/// v1.0.107 — Choix du cimetière de réapparition, copie de la règle du shard
	/// (ServerApp::HandleRespawnRequest) : cimetière ÉLIGIBLE (RespawnRules.h) le plus
	/// proche du lieu de mort en distance HORIZONTALE (X, Z), le premier à égalité.
	/// \param neutralRadiusM rayon neutre par défaut du shard (globals.*, 500 m).
	/// \return indice dans \p points, -1 si aucun (repli du shard : point d'entrée).
	int PickGraveyard(const std::vector<tools::world_gen::RespawnDef>& points, tools::world_gen::Vec2 death,
		const std::string& playerFaction, float neutralRadiusM = 500.0f)
	{
		int best = -1;
		float bestDistSq = std::numeric_limits<float>::max();
		for (size_t k = 0; k < points.size(); ++k)
		{
			const tools::world_gen::RespawnDef& p = points[k];
			if (p.type != "graveyard") continue;
			const float dx = static_cast<float>(p.position.x - death.x), dz = static_cast<float>(p.position.z - death.z);
			const float d2 = dx * dx + dz * dz;
			// Colonne faction du fichier : « - » = cimetière neutre (faction vide au shard).
			const std::string owner = p.faction == "-" ? std::string() : p.faction;
			if (!engine::world::IsGraveyardEligibleForRespawn(std::sqrt(d2), neutralRadiusM, owner, playerFaction)) continue;
			if (d2 < bestDistSq) { bestDistSq = d2; best = static_cast<int>(k); }
		}
		return best;
	}

	/// v1.0.107 : le gouffre caché de Deathtrap Canyon et sa ville (lot 32 du propriétaire,
	/// LORE_BD §16.36). Master (ville secrète, relief `gouffre`), profondeur et parois,
	/// galerie, ville au fond, route 000074 et tout le réseau identiques au monde sans
	/// gouffre, cellule déterministe, 3 cellules hors S008_E010 identiques octet pour octet,
	/// torches et ambiance en données, places.json sans la ville secrète, réapparition.
	void TestGouffreV1107()
	{
		using namespace tools::world_gen;
		namespace fs = std::filesystem;
		namespace wc = engine::world::cells;
		GlobalWorld g1, g0;
		std::string err;
		const bool ok = LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g1.master, err)
			&& LoadWorldMaster(LCDLLN_CONTENT_DIR, "royaumes", g0.master, err);
		if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok, "gouffre v1.0.107 : master charge");
		if (!ok) return;
		const WorldMaster& m = g1.master;
		const ReliefDef* r = nullptr;
		for (const ReliefDef& x : m.reliefs) if (x.id == "gouffre_des_revenus") r = &x;
		const CityDef* cm = nullptr;
		for (const CityDef& c : m.cities) if (c.id == "ville_thurvhal") cm = &c;
		Check(r && cm && r->type == "gouffre" && r->cityId == cm->id && cm->cell == "S008_E010" && cm->secret
			&& cm->startFor.empty() && cm->hasPosition && std::fabs(r->center.x - (100000.0 + cm->positionXM)) < 1e-9
			&& std::fabs(r->center.z - (80000.0 + cm->positionZM)) < 1e-9,
			"gouffre v1.0.107 : Thurvhal secrete en S008_E010, gouffre centre sur sa position manuelle");
		if (!r || !cm) return;
		// Monde de référence SANS gouffre (même master, ville et relief retirés).
		g0.master.cities.erase(std::remove_if(g0.master.cities.begin(), g0.master.cities.end(),
			[](const CityDef& c) { return c.id == "ville_thurvhal"; }), g0.master.cities.end());
		g0.master.reliefs.erase(std::remove_if(g0.master.reliefs.begin(), g0.master.reliefs.end(),
			[](const ReliefDef& x) { return x.type == "gouffre"; }), g0.master.reliefs.end());

		// 1. Profondeur : fond plat à floorY, depth_m sous le point le plus bas du pourtour,
		//    80 à 150 m sous le pourtour moyen ; parois d'au moins 80 m juste derrière le fond.
		const double rim = r->floorRadiusM * 1.13 + r->wallM + 100.0;
		double lowest = 1e18, mean = 0.0;
		for (int k = 0; k < 64; ++k)
		{
			const double a = k * 6.283185307179586 / 64.0;
			const double h = MacroHeight(g0.master, r->center.x + std::cos(a) * rim, r->center.z + std::sin(a) * rim);
			lowest = std::min(lowest, h); mean += h / 64.0;
		}
		Check(std::fabs(lowest - r->depthM - r->floorY) < 1e-6 && mean - r->floorY >= 80.0 && mean - r->floorY <= 150.0,
			"gouffre v1.0.107 : fond depth_m sous le bord le plus bas, 80 a 150 m sous le pourtour moyen");
		bool flat = true;
		for (int k = 0; k < 48; ++k)
		{
			const double a = k * 0.6180339887 * 6.283185307179586, d = r->floorRadiusM * 0.8 * std::sqrt((k + 0.5) / 48.0);
			flat = flat && std::fabs(MacroHeight(m, r->center.x + std::cos(a) * d, r->center.z + std::sin(a) * d) - r->floorY) < 1e-6;
		}
		double wallMin = 1e18;
		for (int k = 0; k < 64; ++k)
		{
			const double a = k * 6.283185307179586 / 64.0, d = GouffreFloorRadiusAt(*r, a) + r->wallM + 3.0;
			const Vec2 p{ r->center.x + std::cos(a) * d, r->center.z + std::sin(a) * d };
			if (GouffreGalleryDistance(*r, p, nullptr) < r->galleryHalfWidthM + r->galleryWallM + 30.0) continue; // embouchure
			wallMin = std::min(wallMin, MacroHeight(m, p.x, p.z) - r->floorY);
		}
		Check(flat && wallMin >= 80.0, "gouffre v1.0.107 : fond plat, parois abruptes (au moins 80 m a wall_m + 3 m du fond)");
		// 2. Galerie : sol au niveau du fond jusqu'à sa sortie, puis pente régulière ≤ 30 %
		//    jusqu'à l'entrée (les 60 derniers mètres suivent le terrain de l'entrée).
		const double total = r->galleryAlong.back(), run = total - r->galleryExitM;
		bool ramp = run > 0.0 && (r->entranceY - r->floorY) / run <= 0.30 && r->entranceY > r->floorY + 80.0;
		for (double a = 0.0; ramp && a <= total - 60.0; a += 5.0)
		{
			size_t k = 0;
			while (k + 2 < r->gallery.size() && r->galleryAlong[k + 1] < a) ++k;
			const Vec2 p = r->gallery[k] + (r->gallery[k + 1] - r->gallery[k]) * ((a - r->galleryAlong[k]) / (r->galleryAlong[k + 1] - r->galleryAlong[k]));
			const double want = r->floorY + (r->entranceY - r->floorY) * std::clamp((a - r->galleryExitM) / run, 0.0, 1.0);
			ramp = std::fabs(MacroHeight(m, p.x, p.z) - want) < 0.05;
		}
		Check(ramp && std::fabs(MacroHeight(m, r->gallery.back().x, r->gallery.back().z) - r->entranceY) < 1e-6,
			"gouffre v1.0.107 : galerie de la Fente en pente reguliere (<= 30 %) du fond a l'entree cachee");
		// Le gouffre ne creuse que dans sa boîte : ailleurs, relief identique au bit près.
		bool untouched = true;
		for (int k = 0; k < 400; ++k)
		{
			const double x = 85000.0 + (k % 20) * 1500.0 + 37.0, z = 65000.0 + (k / 20) * 1500.0 + 11.0;
			const bool inBox = std::fabs(x - r->center.x) <= r->footRadiusM && std::fabs(z - r->center.z) <= r->footRadiusM;
			if (!inBox) untouched = untouched && MacroHeight(m, x, z) == MacroHeight(g0.master, x, z);
		}
		Check(untouched, "gouffre v1.0.107 : relief macro identique hors de la boite du gouffre");

		// 3. Couche macro : ville au fond ; réseau routier, autres villes et POI identiques au
		//    monde sans gouffre (route 000074 comprise) ; rien ne passe dans le gouffre.
		BuildMacroTerrain(g1); BuildMacroRivers(g1); BuildMacroRoads(g1);
		BuildMacroTerrain(g0); BuildMacroRivers(g0); BuildMacroRoads(g0);
		const CityDef* town = g1.FindCity("ville_thurvhal");
		Check(town && std::hypot(town->center.x - r->center.x, town->center.z - r->center.z) < 1e-9
			&& std::fabs(town->plateauY - r->floorY) < 0.05 && town->radiusM + 90.0 < r->floorRadiusM * 0.87,
			"gouffre v1.0.107 : Thurvhal au centre du fond plat, cimetiere compris");
		auto sameRoads = [](const std::vector<Road>& a, const std::vector<Road>& b) {
			if (a.size() != b.size()) return false;
			for (size_t k = 0; k < a.size(); ++k)
			{
				if (a[k].loreId != b[k].loreId || a[k].fromId != b[k].fromId || a[k].toId != b[k].toId || a[k].points.size() != b[k].points.size()) return false;
				for (size_t q = 0; q < a[k].points.size(); ++q)
					if (a[k].points[q].x != b[k].points[q].x || a[k].points[q].z != b[k].points[q].z || a[k].profileY[q] != b[k].profileY[q]) return false;
			}
			return true;
		};
		const Road* r74 = nullptr;
		for (const Road& x : g1.roads) if (x.loreId == "000074") r74 = &x;
		bool r74InCanyon = false;
		if (r74) for (const Vec2& p : r74->points) r74InCanyon = r74InCanyon || (std::fabs(p.x - 100000.0) < 5000.0 && std::fabs(p.z - 80000.0) < 5000.0);
		Check(r74 && r74InCanyon && sameRoads(g1.roads, g0.roads) && sameRoads(g1.generatorRoads, g0.generatorRoads),
			"gouffre v1.0.107 : route 000074 par S008_E010 et tout le reseau routier identiques au monde sans gouffre");
		bool clear = true, linked = false;
		double entranceRoad = 1e18;
		for (const std::vector<Road>* rs : { &g1.roads, &g1.generatorRoads })
			for (const Road& x : *rs)
			{
				linked = linked || x.fromId == town->id || x.toId == town->id;
				for (const Vec2& p : x.points)
				{
					clear = clear && !InGouffre(*r, p, 150.0);
					entranceRoad = std::min(entranceRoad, std::hypot(p.x - r->gallery.back().x, p.z - r->gallery.back().z));
				}
			}
		Check(clear && !linked && entranceRoad > 500.0 && entranceRoad < 1000.0,
			"gouffre v1.0.107 : aucune route dans le gouffre ni vers Thurvhal ; entree cachee a 500-1000 m de la route");
		bool others = g1.cities.size() == g0.cities.size() + 1 && g1.pois.size() == g0.pois.size();
		for (size_t k = 0, j = 0; others && k < g1.cities.size(); ++k)
		{
			if (g1.cities[k].id == "ville_thurvhal") continue;
			others = g1.cities[k].id == g0.cities[j].id && g1.cities[k].center.x == g0.cities[j].center.x
				&& g1.cities[k].center.z == g0.cities[j].center.z && g1.cities[k].plateauY == g0.cities[j].plateauY;
			++j;
		}
		for (size_t k = 0; others && k < g1.pois.size(); ++k)
			others = g1.pois[k].center.x == g0.pois[k].center.x && g1.pois[k].center.z == g0.pois[k].center.z;
		std::vector<std::string> c1 = m.generatedCells, c0 = g0.master.generatedCells;
		AddStartCorridors(g1, c1); AddStartCorridors(g0, c0);
		Check(others && c1 == c0, "gouffre v1.0.107 : autres villes, POI et cellules jouables inchanges");

		// 4. Cellules (pipeline de RunWorldGen, sorties écrites pour comparaison octet pour octet).
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_gouffre_v1107";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		g1.contentRoot = g0.contentRoot = LCDLLN_CONTENT_DIR;
		auto gen = [&](GlobalWorld& g, const std::string& id, const std::string& sub) {
			auto w = std::make_unique<World>();
			int n = 0, e = 0;
			ParseCellId(id, n, e);
			g.outputRoot = (tmp / sub).string();
			InitCell(*w, g, { n, e });
			BuildPlaces(*w); BuildCellTerrain(*w); FlattenSettlements(*w); CarveWater(*w); ApplyRoads(*w); FillSeaPockets(*w);
			std::string e2;
			ImportLegacyContent(*w, e2);
			BuildSettlements(*w); BuildForestMask(*w); BuildVegetation(*w); BuildPopulation(*w);
			if (!WriteCellOutputs(*w, e2)) std::fprintf(stderr, "  %s\n", e2.c_str());
			return w;
		};
		auto sameDir = [&](const fs::path& a, const fs::path& b) {
			size_t n = 0;
			bool same = true;
			for (const auto& f : fs::recursive_directory_iterator(a))
			{
				if (!f.is_regular_file()) continue;
				const fs::path rel = fs::relative(f.path(), a);
				same = same && fs::exists(b / rel) && ReadBytes(f.path()) == ReadBytes(b / rel);
				++n;
			}
			return same && n > 0;
		};
		std::unique_ptr<World> w1 = gen(g1, "S008_E010", "a");
		std::unique_ptr<World> w1b = gen(g1, "S008_E010", "b");
		Check(sameDir(tmp / "a", tmp / "b"), "gouffre v1.0.107 : cellule S008_E010 deterministe (deux generations identiques)");
		const double hTown = w1->HeightAt(town->center);
		bool noPlace = true, noFauna = true;
		for (const CityDef& c : w1->cities)
			if (!c.procedural.empty()) noPlace = noPlace && !InGouffre(*r, c.center, c.radiusM);
		for (const SpawnerDef& sp : w1->spawners)
			if (sp.id.rfind("faune_", 0) == 0) noFauna = noFauna && !InGouffre(*r, sp.position, 0.0);
		Check(std::fabs(hTown - r->floorY) < 0.6 && noPlace && noFauna,
			"gouffre v1.0.107 : terrain de Thurvhal au fond ; ni lieu procedural ni faune dans le gouffre");
		const RespawnDef* gy = nullptr;
		for (const RespawnDef& x : w1->respawns)
			if (x.type == "graveyard" && std::hypot(x.position.x - r->center.x, x.position.z - r->center.z) < r->floorRadiusM * 0.8) gy = &x;
		Check(gy && gy->faction == "-" && std::fabs(gy->y - (r->floorY + 1.0)) < 0.6,
			"gouffre v1.0.107 : cimetiere NEUTRE au fond du gouffre (respawn_points.txt)");
		bool torchesOk = w1->pointLights.size() >= 20;
		size_t inGallery = 0, onFloor = 0;
		for (const PointLightDef& l : w1->pointLights)
		{
			const double dc = std::hypot(l.position.x - r->center.x, l.position.z - r->center.z);
			const bool floor = dc < r->floorRadiusM * 1.13 + 8.0;
			const bool gal = GouffreGalleryDistance(*r, l.position, nullptr) < r->galleryHalfWidthM + 2.0;
			onFloor += floor ? 1 : 0; inGallery += (!floor && gal) ? 1 : 0;
			torchesOk = torchesOk && (floor || gal) && std::hypot(l.position.x - r->gallery.back().x, l.position.z - r->gallery.back().z) > 55.0;
		}
		size_t torchMeshes = 0;
		for (const ScatterDef& sd : w1->scatter) torchMeshes += sd.mesh == "meshes/props/Torch_Metal.gltf" && InGouffre(*r, sd.position, 5.0) ? 1 : 0;
		const std::string zone = ReadText(tmp / "a" / "zones" / "cell_s008_e010" / "zone.json");
		Check(torchesOk && onFloor >= 8 && inGallery >= 8 && torchMeshes == w1->pointLights.size() && w1->ambianceZones.size() == 1
			&& zone.find("\"point_lights\"") != std::string::npos && zone.find("\"ambiance_zones\"") != std::string::npos,
			"gouffre v1.0.107 : torches (decor) au fond et dans la galerie, aucune dehors ; lumieres et ambiance sombre en donnees (zone.json)");

		// 5. Monde inchangé ailleurs : 3 cellules hors S008_E010 (voisines est et sud, nord-ouest),
		//    octets identiques avec et sans gouffre.
		std::vector<std::unique_ptr<World>> around;
		bool sameAround = true;
		for (const char* id : { "S008_E011", "S009_E010", "S007_E009" })
		{
			around.push_back(gen(g1, id, "c1"));
			gen(g0, id, "c0");
		}
		sameAround = sameDir(tmp / "c1", tmp / "c0");
		Check(sameAround, "gouffre v1.0.107 : S008_E011, S009_E010 et S007_E009 identiques octet pour octet au monde sans gouffre");

		// 6. places.json : Thurvhal hors de la liste publique, dans secret_cities.
		{
			std::vector<World*> cells = { w1.get() };
			g1.outputRoot = (tmp / "g").string();
			std::string e3;
			const bool okG = WriteGlobalOutputs(g1, cells, e3);
			const std::string places = ReadText(tmp / "g" / "world" / "royaumes" / "places.json");
			const size_t secretAt = places.find("\"secret_cities\"");
			const size_t townAt = places.find("\"id\": \"ville_thurvhal\"");
			Check(okG && secretAt != std::string::npos && townAt != std::string::npos && townAt > secretAt
				&& places.find("\"id\": \"ville_thurvhal\"", townAt + 1) == std::string::npos,
				"gouffre v1.0.107 : places.json sans Thurvhal dans cities (carte publique), Thurvhal dans secret_cities");
		}

		// 7. Réapparition (règle du shard) : graveyards des cellules jouables assez proches
		//    pour concurrencer le gouffre sur la zone du canyon = disque de 3 km autour du
		//    POI Deathtrap Canyon (gorge et crête de la route 000074) et le gouffre lui-même.
		std::vector<Vec2> zonePts;
		for (double dz = -3000.0; dz <= 3000.0; dz += 100.0)
			for (double dx = -3000.0; dx <= 3000.0; dx += 100.0)
				if (dx * dx + dz * dz <= 3000.0 * 3000.0) zonePts.push_back({ 100000.0 + dx, 80000.0 + dz });
		for (double dz = -600.0; dz <= 600.0; dz += 50.0)
			for (double dx = -600.0; dx <= 600.0; dx += 50.0)
				if (InGouffre(*r, { r->center.x + dx, r->center.z + dz }, 0.0)) zonePts.push_back({ r->center.x + dx, r->center.z + dz });
		for (const Vec2& p : r74->points)
			if (std::hypot(p.x - 100000.0, p.z - 80000.0) <= 3000.0) zonePts.push_back(p);
		double dmax = 0.0;
		for (const Vec2& p : zonePts) dmax = std::max(dmax, std::hypot(p.x - gy->position.x, p.z - gy->position.z));
		// Toute cellule jouable dont le carré s'approche de la zone à moins de dmax peut
		// porter un concurrent : on les génère toutes (les autres sont forcément plus loin).
		std::vector<RespawnDef> all = w1->respawns, without;
		std::set<std::string> extra = { "S008_E010", "S008_E011", "S009_E010", "S007_E009" };
		for (const std::unique_ptr<World>& w : around) all.insert(all.end(), w->respawns.begin(), w->respawns.end());
		size_t competitors = 0;
		for (const std::string& id : c1)
		{
			int n = 0, e = 0;
			if (extra.count(id) || !ParseCellId(id, n, e)) continue;
			const wc::CellPoint mn = wc::CellMinCorner({ n, e });
			const double ddx = std::max({ 0.0, (100000.0 - 3000.0) - (mn.x + 10000.0), mn.x - (r->center.x + 600.0) });
			const double ddz = std::max({ 0.0, (80000.0 - 3000.0) - (mn.z + 10000.0), mn.z - (80000.0 + 3000.0) });
			if (std::hypot(ddx, ddz) >= dmax) continue;
			extra.insert(id);
			std::unique_ptr<World> w = gen(g1, id, "r");
			all.insert(all.end(), w->respawns.begin(), w->respawns.end());
			++competitors;
		}
		for (const RespawnDef& x : all)
			if (!(x.type == gy->type && x.position.x == gy->position.x && x.position.z == gy->position.z)) without.push_back(x);
		size_t toGouffre = 0;
		for (const Vec2& p : zonePts)
			for (const char* fac : { "lumiere", "lune_noire", "" })
			{
				const int k = PickGraveyard(all, p, fac);
				toGouffre += (k >= 0 && all[static_cast<size_t>(k)].position.x == gy->position.x && all[static_cast<size_t>(k)].position.z == gy->position.z) ? 1 : 0;
			}
		Check(competitors >= 3 && toGouffre == zonePts.size() * 3,
			"gouffre v1.0.107 : mort dans le canyon (3 km autour de Deathtrap Canyon, route 000074, gouffre) -> cimetiere du gouffre, toutes factions");
		// Ailleurs : même cimetière qu'avant, à Der Hem Shelbem, au hameau de Frhozhol, au
		// cimetière de chaque hameau ou ville voisin et sur la route 000074 au-delà de 9 km.
		std::vector<Vec2> elsewhere;
		for (const char* id : { "ville_der_hem_shelsem", "hamlet_0193" })
			if (const CityDef* c = g1.FindCity(id)) elsewhere.push_back(c->center);
		for (const RespawnDef& x : without)
			if (x.type == "graveyard") elsewhere.push_back(x.position);
		for (const Vec2& p : r74->points)
			if (std::hypot(p.x - gy->position.x, p.z - gy->position.z) > 9000.0) elsewhere.push_back(p);
		bool unchanged = elsewhere.size() > 10;
		for (const Vec2& p : elsewhere)
		{
			const int a = PickGraveyard(all, p, "lumiere"), b = PickGraveyard(without, p, "lumiere");
			unchanged = unchanged && a >= 0 && b >= 0 && all[static_cast<size_t>(a)].position.x == without[static_cast<size_t>(b)].position.x
				&& all[static_cast<size_t>(a)].position.z == without[static_cast<size_t>(b)].position.z;
		}
		Check(unchanged, "gouffre v1.0.107 : reapparition inchangee ailleurs (villes et hameaux voisins, route a plus de 9 km)");
		fs::remove_all(tmp, ec);
	}
}

int main()
{
	TestScatterRoundTrip();
	TestPng();
	TestMaster();
	TestMasterEnfers();
	TestStartCorridors();
	TestGenerationAndInvariants();
	TestReconstruction();
	TestMapFeatures();
	TestWaterShores();
	TestRiverIslandAndSharedPois();
	TestReliefs();
	TestVillesV1073();
	TestIleV1074();
	TestCorrectionsV1076();
	TestReliefIleV1077();
	TestCellulesV1078();
	TestLieuxV1081();
	TestLieuxV1082();
	TestLieuxV1085();
	TestLieuxV1086();
	TestLieuxV1087();
	TestCellulesLieuxV1088();
	TestLieuxV1089();
	TestCellulesLieuxV1091();
	TestCellulesLieuxV1104();
	TestRoutesLoreV1105();
	TestRoutesManquantesV1106();
	TestPlacementManuelV1095();
	TestGouffreV1107();
	TestGenerationEnfers();
	if (g_failures)
	{
		std::fprintf(stderr, "world_gen_tests : %d echec(s)\n", g_failures);
		return 1;
	}
	std::printf("world_gen_tests : tout est vert\n");
	return 0;
}
