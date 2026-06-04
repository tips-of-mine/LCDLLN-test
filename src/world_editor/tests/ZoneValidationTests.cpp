// M100.48 — Tests du Zone Validation Service : registre, règles MVP (heightmap,
// splat, mesh inserts), tri/compteurs du rapport, options, et validation
// incrémentale. Headless, lié à engine_core.

#include "src/world_editor/validation/ValidationRuleRegistry.h"
#include "src/world_editor/validation/ZoneValidator.h"

#include "src/client/world/terrain/SplatMap.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace engine::editor::world::validation;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	engine::world::terrain::SplatMap MakeSplat(uint32_t res)
	{
		engine::world::terrain::SplatMap s;
		s.resolution = res;
		s.layerCount = 8u;
		s.weights.assign(static_cast<size_t>(res) * res * 8u, 0u);
		// Toutes cellules valides par défaut : layer 0 = 255.
		for (uint32_t i = 0; i < res * res; ++i)
			s.weights[static_cast<size_t>(i) * 8u + 0u] = 255u;
		return s;
	}

	bool HasRule(const std::vector<ValidationIssue>& issues, const std::string& ruleId)
	{
		for (const auto& i : issues) if (i.ruleId == ruleId) return true;
		return false;
	}

	void Test_Registry_RegistersMvpRules()
	{
		ValidationRuleRegistry reg;
		RegisterMvpValidationRules(reg);
		REQUIRE(reg.Count() == 6);
		REQUIRE(reg.GetRulesByCategory("heightmap").size() == 2);
		REQUIRE(reg.GetRulesByCategory("splat").size() == 2);
		REQUIRE(reg.GetRulesByCategory("mesh_inserts").size() == 2);
	}

	void Test_Heightmap_HolesDetected()
	{
		engine::world::terrain::TerrainChunk chunk = engine::world::terrain::TerrainChunk::MakeFlat(10.0f);
		chunk.heights[5] = std::numeric_limits<float>::quiet_NaN();
		chunk.heights[6] = std::numeric_limits<float>::infinity();

		ValidationContext ctx;
		ctx.terrainChunks.push_back({ &chunk, {0,0,0} });

		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);
		auto rep = v.Validate(ctx);
		REQUIRE(HasRule(rep.issues, "heightmap.holes"));
		REQUIRE(rep.errorCount >= 2); // 2 cellules corrompues.
		REQUIRE(rep.HasBlockingErrors());
	}

	void Test_Heightmap_ExtremeSlope()
	{
		// Chunk plat puis une cellule très haute → pente quasi verticale.
		engine::world::terrain::TerrainChunk chunk = engine::world::terrain::TerrainChunk::MakeFlat(0.0f);
		chunk.heights[1] = 1000.0f; // voisin de la cellule 0 (Δh=1000 m sur 1 m).

		ValidationContext ctx;
		ctx.terrainChunks.push_back({ &chunk, {0,0,0} });
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);
		auto rep = v.Validate(ctx);
		REQUIRE(HasRule(rep.issues, "heightmap.extreme_slope"));
		REQUIRE(rep.warningCount >= 1);
	}

	void Test_Splat_SumInvalidAndEmpty()
	{
		auto splat = MakeSplat(2u); // 4 cellules valides.
		// Cellule 0 : somme invalide (100).
		splat.weights[0] = 100u;
		// Cellule 3 : vide (somme 0).
		splat.weights[3u * 8u + 0u] = 0u;

		ValidationContext ctx;
		ctx.splat = &splat;
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);
		auto rep = v.Validate(ctx);
		REQUIRE(HasRule(rep.issues, "splat.sum_invalid"));
		REQUIRE(HasRule(rep.issues, "splat.empty_cell"));
	}

	void Test_MeshInserts_GltfMissingAndDuplicateGuid()
	{
		std::vector<engine::editor::world::volumes::MeshInsertInstance> inserts(3);
		inserts[0].guid = 1; inserts[0].gltfRelativePath = "meshes/caves/c1.gltf"; inserts[0].insertCategory = "cave";
		inserts[1].guid = 2; inserts[1].gltfRelativePath = "";                      inserts[1].insertCategory = "overhang"; // missing
		inserts[2].guid = 1; inserts[2].gltfRelativePath = "meshes/arches/a1.gltf"; inserts[2].insertCategory = "arch";    // dup guid 1

		ValidationContext ctx;
		ctx.meshInserts = &inserts;
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);
		auto rep = v.Validate(ctx);
		REQUIRE(HasRule(rep.issues, "mesh_inserts.gltf_missing"));
		REQUIRE(HasRule(rep.issues, "mesh_inserts.duplicate_guid"));
	}

	void Test_Report_SortedBySeverityDesc()
	{
		// Mix : un warning (slope) + une error (hole). Error doit venir en 1er.
		engine::world::terrain::TerrainChunk chunk = engine::world::terrain::TerrainChunk::MakeFlat(0.0f);
		chunk.heights[1] = 1000.0f;                                   // warning slope
		chunk.heights[10] = std::numeric_limits<float>::quiet_NaN();  // error hole

		ValidationContext ctx;
		ctx.terrainChunks.push_back({ &chunk, {0,0,0} });
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);
		auto rep = v.Validate(ctx);
		REQUIRE(!rep.issues.empty());
		// Le premier problème doit être une Error.
		REQUIRE(rep.issues.front().severity == Severity::Error);
		// Les sévérités sont non-croissantes.
		for (size_t i = 1; i < rep.issues.size(); ++i)
			REQUIRE(static_cast<uint8_t>(rep.issues[i - 1].severity) >= static_cast<uint8_t>(rep.issues[i].severity));
	}

	void Test_Options_OnlyCategoryAndExcluded()
	{
		engine::world::terrain::TerrainChunk chunk = engine::world::terrain::TerrainChunk::MakeFlat(0.0f);
		chunk.heights[10] = std::numeric_limits<float>::quiet_NaN();
		auto splat = MakeSplat(2u); splat.weights[0] = 100u;

		ValidationContext ctx;
		ctx.terrainChunks.push_back({ &chunk, {0,0,0} });
		ctx.splat = &splat;
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);

		// onlyCategory = heightmap → pas de problème splat.
		ZoneValidator::Options optCat; optCat.onlyCategory = true; optCat.category = "heightmap";
		auto repCat = v.Validate(ctx, optCat);
		REQUIRE(HasRule(repCat.issues, "heightmap.holes"));
		REQUIRE(!HasRule(repCat.issues, "splat.sum_invalid"));

		// excludedRules : désactive heightmap.holes.
		ZoneValidator::Options optEx; optEx.excludedRules.insert("heightmap.holes");
		auto repEx = v.Validate(ctx, optEx);
		REQUIRE(!HasRule(repEx.issues, "heightmap.holes"));
		REQUIRE(HasRule(repEx.issues, "splat.sum_invalid"));
	}

	void Test_Incremental_RerunsOnlyChangedTags()
	{
		engine::world::terrain::TerrainChunk chunk = engine::world::terrain::TerrainChunk::MakeFlat(0.0f);
		chunk.heights[10] = std::numeric_limits<float>::quiet_NaN(); // error heightmap
		auto splat = MakeSplat(2u); splat.weights[0] = 100u;          // error splat

		ValidationContext ctx;
		ctx.terrainChunks.push_back({ &chunk, {0,0,0} });
		ctx.splat = &splat;
		ValidationRuleRegistry reg; RegisterMvpValidationRules(reg);
		ZoneValidator v(reg);

		auto full = v.Validate(ctx);
		REQUIRE(HasRule(full.issues, "heightmap.holes"));
		REQUIRE(HasRule(full.issues, "splat.sum_invalid"));

		// Le splat est corrigé ; seul le tag "splat" a changé.
		for (uint32_t i = 0; i < 4u; ++i) { splat.weights[static_cast<size_t>(i) * 8u + 0u] = 255u; }
		auto inc = v.ValidateIncremental(ctx, { "splat" }, full);
		// Les règles splat re-runées → plus de problème splat.
		REQUIRE(!HasRule(inc.issues, "splat.sum_invalid"));
		// Les règles heightmap NON re-runées → problème conservé du rapport précédent.
		REQUIRE(HasRule(inc.issues, "heightmap.holes"));
	}
}

int main()
{
	Test_Registry_RegistersMvpRules();
	Test_Heightmap_HolesDetected();
	Test_Heightmap_ExtremeSlope();
	Test_Splat_SumInvalidAndEmpty();
	Test_MeshInserts_GltfMissingAndDuplicateGuid();
	Test_Report_SortedBySeverityDesc();
	Test_Options_OnlyCategoryAndExcluded();
	Test_Incremental_RerunsOnlyChangedTags();

	if (g_failed == 0)
		std::printf("[zone_validation_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[zone_validation_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
