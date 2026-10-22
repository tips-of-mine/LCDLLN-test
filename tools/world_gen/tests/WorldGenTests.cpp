// world_gen_tests — tests du générateur de monde « Royaumes » (CPU pur, CI Linux).
//
// 1. scatter.bin : round-trip bit-à-bit + rejets (magic, troncature, index).
// 2. PNG : l'encodeur maison est relu par stb_image (décodeur du client).
// 3. Master réel (game/data/world/royaumes/world_master.json) : chargement OK.
// 4. Génération complète vers DEUX dossiers temporaires : octets identiques
//    (déterminisme), puis invariants du monde produit.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "lib/PngWriter.h"
#include "lib/WorldGen.h"
#include "lib/WorldGenContext.h"

#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/ScatterInstances.h"

#include <zone_builder/JsonDocument.h>

#include <cmath>
#include <cstdio>
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
	}

	void TestGenerationAndInvariants()
	{
		namespace fs = std::filesystem;
		const fs::path tmp = fs::temp_directory_path() / "lcdlln_world_gen_tests";
		std::error_code ec;
		fs::remove_all(tmp, ec);
		tools::world_gen::GenReport r1, r2;
		std::string err;
		tools::world_gen::GenOptions o;
		o.contentRoot = LCDLLN_CONTENT_DIR;
		o.worldId = "royaumes";
		o.outputRoot = (tmp / "a").string();
		const bool ok1 = tools::world_gen::RunWorldGen(o, r1, err);
		if (!ok1) std::fprintf(stderr, "  %s\n", err.c_str());
		Check(ok1, "generation complete (passe 1)");
		o.outputRoot = (tmp / "b").string();
		const bool ok2 = tools::world_gen::RunWorldGen(o, r2, err);
		Check(ok2, "generation complete (passe 2)");
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
		// Décor instancié : volume et chargement.
		{
			engine::world::instances::ScatterFile sf; std::string e;
			const bool ok = engine::world::instances::LoadScatterBin(ReadBytes(out / "instances/zone_royaumes/scatter.bin"), sf, e);
			Check(ok, "scatter.bin genere : relecture OK");
			Check(sf.instances.size() > 50000 && sf.instances.size() < 400000, "scatter.bin : volume plausible (50k..400k)");
		}
		// Bâtiments : toutes les références se résolvent dans la bibliothèque.
		{
			std::vector<engine::world::instances::BuildingPlacement> pls; std::string e;
			Check(engine::world::instances::LoadBuildingsBin(ReadBytes(out / "instances/zone_royaumes/buildings.bin"), pls, e),
				"buildings.bin genere : relecture OK");
			Check(pls.size() > 300, "buildings.bin : plus de 300 batiments");
			const std::string maison = ReadText(out / "buildings/templates/maison.json");
			bool allResolved = true;
			for (const auto& p : pls)
			{
				if (p.templateType == "maison" && maison.find("\"id\": \"" + p.variantId + "\"") == std::string::npos) allResolved = false;
				if (p.templateType != "maison" && p.templateType != "tavern" && p.templateType != "house") allResolved = false;
			}
			Check(allResolved, "buildings.bin : types/variantes connus");
		}
		// Heightmap : dimensions et en-tête.
		{
			const std::vector<uint8_t> hm = ReadBytes(out / "zones/royaumes/terrain_height.r16h");
			Check(hm.size() == 12u + 1025u * 1025u * 2u && hm[0] == 'H' && hm[1] == 'A', "heightmap 1025x1025 HAMP");
		}
		// Points de départ : 5 races, au sec, dans la grille serveur (±5000 m).
		{
			tools::zone_builder::JsonValue root; std::string e;
			const bool ok = tools::zone_builder::ParseJsonDocument(ReadText(out / "world/start_locations.json"), root, e);
			Check(ok, "start_locations.json : JSON valide");
			const auto* races = tools::zone_builder::FindObjectMember(root, "races");
			Check(races && races->objectValue.size() == 5, "start_locations.json : 5 races");
			bool inGrid = races != nullptr;
			if (races)
				for (const auto& kv : races->objectValue)
				{
					const double x = tools::zone_builder::FindObjectMember(kv.second, "x")->numberValue;
					const double z = tools::zone_builder::FindObjectMember(kv.second, "z")->numberValue;
					const double y = tools::zone_builder::FindObjectMember(kv.second, "y")->numberValue;
					if (std::fabs(x) >= 5000.0 || std::fabs(z) >= 5000.0 || y < 41.0) inGrid = false;
				}
			Check(inGrid, "departs : dans la grille serveur et au-dessus de la mer");
		}
		// Spawners : JSON valide, routines bien formées, ids uniques.
		{
			tools::zone_builder::JsonValue root; std::string e;
			const bool ok = tools::zone_builder::ParseJsonDocument(ReadText(out / "zones/royaumes/spawners.json"), root, e);
			Check(ok, "spawners.json : JSON valide");
			const auto* sp = tools::zone_builder::FindObjectMember(root, "spawners");
			std::set<std::string> ids;
			bool routinesOk = sp != nullptr;
			size_t withRoutine = 0;
			if (sp)
				for (const auto& s : sp->arrayValue)
				{
					ids.insert(tools::zone_builder::FindObjectMember(s, "id")->stringValue);
					const auto* rt = tools::zone_builder::FindObjectMember(s, "routine");
					if (!rt) continue;
					++withRoutine;
					const std::string kind = tools::zone_builder::FindObjectMember(*rt, "kind")->stringValue;
					const auto* path = tools::zone_builder::FindObjectMember(*rt, "path");
					if ((kind == "tour" || kind == "patrol") && (!path || path->arrayValue.size() < 2)) routinesOk = false;
				}
			Check(sp && ids.size() == sp->arrayValue.size(), "spawners : ids uniques");
			Check(routinesOk, "spawners : routines tour/patrol avec chemin >= 2 points");
			Check(withRoutine > 200, "spawners : plus de 200 PNJ d'ambiance");
		}
		fs::remove_all(tmp, ec);
	}
}

int main()
{
	TestScatterRoundTrip();
	TestPng();
	TestMaster();
	TestGenerationAndInvariants();
	if (g_failures)
	{
		std::fprintf(stderr, "world_gen_tests : %d echec(s)\n", g_failures);
		return 1;
	}
	std::printf("world_gen_tests : tout est vert\n");
	return 0;
}
