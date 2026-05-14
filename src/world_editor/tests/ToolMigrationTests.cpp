/// Tests unitaires CPU pour M100.45 Phase B — migration des outils vers
/// Simple/Advanced + presets (Phase 12 « Accessibilité éditeur »).
///
/// Couvre la partie *pure* de la migration : l'application d'un preset
/// aux paramètres internes d'un outil. Le rendu Simple/Advanced lui-même
/// (ImGui) n'est pas testable hors environnement Windows — il est validé
/// par le build CI Windows.
///
/// Incrément 1 : hydraulic_erosion (premier outil migré).

#include "src/world_editor/presets/ToolPreset.h"
#include "src/world_editor/presets/ToolPresetApply.h"
#include "src/world_editor/presets/ToolPresetIo.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionParams.h"

#include <cstdio>
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

	namespace presets = engine::editor::world::presets;
	using engine::editor::world::erosion::HydraulicSimulationParams;
	using engine::editor::world::erosion::ThermalWindErosionParams;
	using engine::editor::world::erosion::ErosionSubMode;

	presets::ToolPreset MakePreset(std::initializer_list<std::pair<std::string, double>> kv)
	{
		presets::ToolPreset p;
		p.id = "test";
		p.displayName = "Test";
		for (const auto& e : kv) p.parameters[e.first] = e.second;
		return p;
	}

	/// Un preset complet écrase tous les champs reconnus.
	void Test_HydraulicPreset_FullApply()
	{
		HydraulicSimulationParams p; // démarre sur les défauts
		const auto preset = MakePreset({
			{ "numDroplets", 500000.0 },
			{ "maxLifetimeSteps", 48.0 },
			{ "sedimentCapacity", 8.0 },
			{ "erosionRate", 0.55 },
			{ "depositionRate", 0.25 },
			{ "evaporationRate", 0.015 },
			{ "gravity", 6.0 },
			{ "inertia", 0.02 },
			{ "minSlopeForErosion", 0.005 },
			{ "maxDeltaPerCellMeters", 4.0 },
		});
		presets::ApplyHydraulicErosionPreset(p, preset);
		REQUIRE(p.numDroplets == 500000u);
		REQUIRE(p.maxLifetimeSteps == 48u);
		REQUIRE(p.sedimentCapacity == 8.0f);
		REQUIRE(p.erosionRate == 0.55f);
		REQUIRE(p.depositionRate == 0.25f);
		REQUIRE(p.evaporationRate == 0.015f);
		REQUIRE(p.gravity == 6.0f);
		REQUIRE(p.inertia == 0.02f);
		REQUIRE(p.minSlopeForErosion == 0.005f);
		REQUIRE(p.maxDeltaPerCellMeters == 4.0f);
	}

	/// Un preset partiel ne touche que les clés qu'il définit ; le reste
	/// garde la valeur courante du struct (tolérance).
	void Test_HydraulicPreset_PartialApplyLeavesOthers()
	{
		HydraulicSimulationParams p;
		p.numDroplets = 12345u;
		p.gravity = 9.9f;
		const auto preset = MakePreset({ { "erosionRate", 0.42 } });
		presets::ApplyHydraulicErosionPreset(p, preset);
		REQUIRE(p.erosionRate == 0.42f);   // appliqué
		REQUIRE(p.numDroplets == 12345u);  // inchangé
		REQUIRE(p.gravity == 9.9f);        // inchangé
	}

	/// Un preset vide est un no-op total.
	void Test_HydraulicPreset_EmptyIsNoOp()
	{
		HydraulicSimulationParams ref;
		HydraulicSimulationParams p;
		presets::ApplyHydraulicErosionPreset(p, presets::ToolPreset{});
		REQUIRE(p.numDroplets == ref.numDroplets);
		REQUIRE(p.erosionRate == ref.erosionRate);
		REQUIRE(p.gravity == ref.gravity);
	}

	/// Une valeur négative pour numDroplets (uint) est clampée à 0, pas
	/// wrap-around.
	void Test_HydraulicPreset_NegativeCountClampedToZero()
	{
		HydraulicSimulationParams p;
		const auto preset = MakePreset({ { "numDroplets", -5.0 } });
		presets::ApplyHydraulicErosionPreset(p, preset);
		REQUIRE(p.numDroplets == 0u);
	}

	/// Bout-à-bout : le JSON canonique du repo se parse et s'applique.
	/// Vérifie que les clés du fichier livré matchent bien le struct.
	void Test_HydraulicPreset_RealJsonRoundTrip()
	{
		const std::string json = R"({
			"toolId": "hydraulic_erosion",
			"defaultPreset": "realistic",
			"presets": [
				{
					"id": "intense",
					"displayName": "Intense",
					"parameters": {
						"numDroplets": 500000, "erosionRate": 0.55,
						"depositionRate": 0.25, "sedimentCapacity": 8.0,
						"evaporationRate": 0.015, "gravity": 6.0,
						"inertia": 0.02, "maxLifetimeSteps": 48,
						"minSlopeForErosion": 0.005, "maxDeltaPerCellMeters": 4.0
					}
				}
			]
		})";
		presets::ToolPresetFile file;
		std::string err;
		REQUIRE(presets::ParseToolPresetJson(json, file, err));
		REQUIRE(file.presets.size() == 1u);

		HydraulicSimulationParams p;
		presets::ApplyHydraulicErosionPreset(p, file.presets[0]);
		REQUIRE(p.numDroplets == 500000u);
		REQUIRE(p.maxLifetimeSteps == 48u);
		REQUIRE(p.gravity == 6.0f);
	}

	// --- thermal_wind_erosion : struct imbriqué, clés pointées ----------

	/// Un preset thermal/wind applique subMode + les sous-blocs imbriqués.
	void Test_ThermalWindPreset_NestedApply()
	{
		ThermalWindErosionParams p;
		const auto preset = MakePreset({
			{ "subMode", 2.0 },
			{ "thermal.talusAngleDeg", 34.0 },
			{ "thermal.forcePerPass", 0.35 },
			{ "thermal.numPasses", 40.0 },
			{ "thermal.preserveSteepSlopes", 1.0 },
			{ "wind.windAngleDeg", 45.0 },
			{ "wind.windStrength", 0.5 },
			{ "wind.numParticles", 60000.0 },
		});
		presets::ApplyThermalWindErosionPreset(p, preset);
		REQUIRE(p.subMode == ErosionSubMode::Both);
		REQUIRE(p.thermal.talusAngleDeg == 34.0f);
		REQUIRE(p.thermal.forcePerPass == 0.35f);
		REQUIRE(p.thermal.numPasses == 40u);
		REQUIRE(p.thermal.preserveSteepSlopes == true);
		REQUIRE(p.wind.windAngleDeg == 45.0f);
		REQUIRE(p.wind.windStrength == 0.5f);
		REQUIRE(p.wind.numParticles == 60000u);
	}

	/// subMode hors borne est clampé sur l'enum (pas d'UB).
	void Test_ThermalWindPreset_SubModeClamped()
	{
		ThermalWindErosionParams p;
		presets::ApplyThermalWindErosionPreset(p, MakePreset({ { "subMode", 99.0 } }));
		REQUIRE(p.subMode == ErosionSubMode::Both);          // clamp haut → 2
		presets::ApplyThermalWindErosionPreset(p, MakePreset({ { "subMode", -3.0 } }));
		REQUIRE(p.subMode == ErosionSubMode::Thermal);       // clamp bas → 0
	}

	/// Preset partiel : seul le bloc thermal touché, le bloc wind intact.
	void Test_ThermalWindPreset_PartialLeavesWindIntact()
	{
		ThermalWindErosionParams p;
		p.wind.windStrength = 1.75f;
		p.wind.numParticles = 9999u;
		presets::ApplyThermalWindErosionPreset(p,
			MakePreset({ { "thermal.forcePerPass", 0.9 } }));
		REQUIRE(p.thermal.forcePerPass == 0.9f);   // appliqué
		REQUIRE(p.wind.windStrength == 1.75f);     // intact
		REQUIRE(p.wind.numParticles == 9999u);     // intact
	}

	/// Le JSON canonique du repo se parse et s'applique sans perte.
	void Test_ThermalWindPreset_RealJsonRoundTrip()
	{
		const std::string json = R"({
			"toolId": "thermal_wind_erosion",
			"defaultPreset": "realistic",
			"presets": [
				{
					"id": "sand_and_talus",
					"displayName": "Sable & talus",
					"parameters": {
						"subMode": 1,
						"thermal.talusAngleDeg": 30.0, "thermal.forcePerPass": 0.5,
						"thermal.numPasses": 60, "thermal.preserveSteepSlopes": 0,
						"wind.windAngleDeg": 90.0, "wind.windStrength": 1.2,
						"wind.numParticles": 180000, "wind.maxLifetimeSteps": 64,
						"wind.sandCapacityFactor": 0.85,
						"wind.exposureRadiusMeters": 45.0,
						"wind.maxDeltaPerCellMeters": 2.0
					}
				}
			]
		})";
		presets::ToolPresetFile file;
		std::string err;
		REQUIRE(presets::ParseToolPresetJson(json, file, err));
		REQUIRE(file.presets.size() == 1u);

		ThermalWindErosionParams p;
		presets::ApplyThermalWindErosionPreset(p, file.presets[0]);
		REQUIRE(p.subMode == ErosionSubMode::Wind);
		REQUIRE(p.thermal.numPasses == 60u);
		REQUIRE(p.thermal.preserveSteepSlopes == false);
		REQUIRE(p.wind.numParticles == 180000u);
		REQUIRE(p.wind.windStrength == 1.2f);
	}
}

int main()
{
	Test_HydraulicPreset_FullApply();
	Test_HydraulicPreset_PartialApplyLeavesOthers();
	Test_HydraulicPreset_EmptyIsNoOp();
	Test_HydraulicPreset_NegativeCountClampedToZero();
	Test_HydraulicPreset_RealJsonRoundTrip();
	Test_ThermalWindPreset_NestedApply();
	Test_ThermalWindPreset_SubModeClamped();
	Test_ThermalWindPreset_PartialLeavesWindIntact();
	Test_ThermalWindPreset_RealJsonRoundTrip();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ToolMigrationTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ToolMigrationTests] all tests passed\n");
	return 0;
}
