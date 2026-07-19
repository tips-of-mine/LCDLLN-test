/// Roadmap-8 (2026-07-19) — Tests CPU du round-trip Save→Load des documents
/// des outils câblés (SplineDocument, ZoneDocument, HazardDocument), qui
/// comble la dette audit 7.2 (SaveToDisk sans LoadFromDisk = écriture morte).
/// Fichiers temporaires ; pur CPU, tourne sous ctest Linux.

#include "src/world_editor/SplineDocument.h"
#include "src/world_editor/ZoneDocument.h"
#include "src/world_editor/HazardDocument.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	/// Dossier temporaire de travail (créé au premier appel).
	std::string TempDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "lcdlln_gameplay_docs_tests";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir.string();
	}

	void Test_SplineRoundTrip()
	{
		using engine::editor::world::SplineDocument;
		const std::string path = TempDir() + "/splines.bin";
		SplineDocument doc;
		engine::world::spline::Spline s;
		engine::world::spline::SplineNode n1; n1.position = { 10.0f, 1.0f, -5.0f }; n1.widthMeters = 4.5f;
		engine::world::spline::SplineNode n2; n2.position = { 20.0f, 2.0f, -8.0f }; n2.widthMeters = 6.0f;
		s.nodes.push_back(n1);
		s.nodes.push_back(n2);
		doc.Add(s);
		std::string err;
		REQUIRE(doc.SaveToDisk(path, err));

		SplineDocument loaded;
		REQUIRE(loaded.LoadFromDisk(path, err));
		REQUIRE(loaded.All().size() == 1u);
		if (loaded.All().size() == 1u)
		{
			REQUIRE(loaded.All()[0].nodes.size() == 2u);
			REQUIRE(loaded.All()[0].nodes[1].widthMeters == 6.0f);
			REQUIRE(loaded.All()[0].nodes[0].position.x == 10.0f);
		}
	}

	void Test_ZoneRoundTrip()
	{
		using engine::editor::world::ZoneDocument;
		const std::string path = TempDir() + "/zones.bin";
		ZoneDocument doc;
		engine::world::zones::GameplayZone z;
		z.type = engine::world::zones::ZoneType::PvPZone;
		z.name = "arene";
		z.polygon = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 10.0f } };
		doc.Add(z);
		std::string err;
		REQUIRE(doc.SaveToDisk(path, err));

		ZoneDocument loaded;
		REQUIRE(loaded.LoadFromDisk(path, err));
		REQUIRE(loaded.All().size() == 1u);
		if (loaded.All().size() == 1u)
		{
			REQUIRE(loaded.All()[0].type == engine::world::zones::ZoneType::PvPZone);
			REQUIRE(loaded.All()[0].name == "arene");
			REQUIRE(loaded.All()[0].polygon.size() == 3u);
		}
	}

	void Test_HazardRoundTrip()
	{
		using engine::editor::world::HazardDocument;
		const std::string path = TempDir() + "/hazards.bin";
		HazardDocument doc;
		engine::world::hazard::HazardVolume h =
			engine::world::hazard::MakeDefaultHazard(engine::world::hazard::HazardType::Tar);
		h.position = { 42.0f, 3.0f, -7.0f };
		h.cylRadius = 8.0f;
		doc.Add(h);
		std::string err;
		REQUIRE(doc.SaveToDisk(path, err));

		HazardDocument loaded;
		REQUIRE(loaded.LoadFromDisk(path, err));
		REQUIRE(loaded.All().size() == 1u);
		if (loaded.All().size() == 1u)
		{
			REQUIRE(loaded.All()[0].type == engine::world::hazard::HazardType::Tar);
			REQUIRE(loaded.All()[0].position.x == 42.0f);
			REQUIRE(loaded.All()[0].cylRadius == 8.0f);
		}
	}

	/// Fichier absent = succès avec liste vide (carte sans instances).
	void Test_MissingFileIsEmptySuccess()
	{
		std::string err;
		engine::editor::world::SplineDocument sd;
		REQUIRE(sd.LoadFromDisk(TempDir() + "/absent_splines.bin", err));
		REQUIRE(sd.All().empty());
		engine::editor::world::ZoneDocument zd;
		REQUIRE(zd.LoadFromDisk(TempDir() + "/absent_zones.bin", err));
		REQUIRE(zd.All().empty());
		engine::editor::world::HazardDocument hd;
		REQUIRE(hd.LoadFromDisk(TempDir() + "/absent_hazards.bin", err));
		REQUIRE(hd.All().empty());
	}
}

int main()
{
	Test_SplineRoundTrip();
	Test_ZoneRoundTrip();
	Test_HazardRoundTrip();
	Test_MissingFileIsEmptySuccess();

	if (g_failed == 0)
	{
		std::printf("[PASS] GameplayDocsRoundTripTests\n");
		return 0;
	}
	std::printf("[FAIL] GameplayDocsRoundTripTests: %d failure(s)\n", g_failed);
	return 1;
}
