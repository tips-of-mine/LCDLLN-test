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
#include "src/world_editor/splat/SplatPaintTool.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"
#include "src/world_editor/terrain/TerrainBrush.h"
#include "src/world_editor/terrain/TerrainStampTool.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionParams.h"
#include "src/world_editor/water/WatershedSimulationParams.h"

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
	using engine::editor::world::TerrainBrushParams;
	using engine::editor::world::TerrainBrushMode;
	using engine::editor::world::SplatPaintParams;
	using engine::editor::world::WatershedSimulationParams;
	using engine::editor::world::MacroPolylineParams;
	using engine::editor::world::PolylineVertex;
	using engine::editor::world::FlankProfile;
	using engine::editor::world::StampParams;
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

	// --- sculpt : struct plat TerrainBrushParams ------------------------

	/// Un preset sculpt applique radius / strength / falloff. Le `mode`
	/// de la brosse n'est jamais touché (choix d'interaction utilisateur).
	void Test_SculptPreset_AppliesAndLeavesModeUntouched()
	{
		TerrainBrushParams p;
		p.mode = TerrainBrushMode::Flatten;
		const auto preset = MakePreset({
			{ "radiusMeters", 24.0 },
			{ "strengthMps", 12.0 },
			{ "falloff", 0.35 },
		});
		presets::ApplySculptPreset(p, preset);
		REQUIRE(p.radiusMeters == 24.0f);
		REQUIRE(p.strengthMps == 12.0f);
		REQUIRE(p.falloff == 0.35f);
		REQUIRE(p.mode == TerrainBrushMode::Flatten); // intact
	}

	/// noiseOctaves hors borne est clampé à [1, 6].
	void Test_SculptPreset_NoiseOctavesClamped()
	{
		TerrainBrushParams p;
		presets::ApplySculptPreset(p, MakePreset({ { "noiseOctaves", 99.0 } }));
		REQUIRE(p.noiseOctaves == 6u);
		presets::ApplySculptPreset(p, MakePreset({ { "noiseOctaves", 0.0 } }));
		REQUIRE(p.noiseOctaves == 1u);
	}

	/// Preset partiel : seule la clé définie change.
	void Test_SculptPreset_PartialLeavesOthers()
	{
		TerrainBrushParams p;
		p.radiusMeters = 7.0f;
		p.strengthMps = 99.0f;
		presets::ApplySculptPreset(p, MakePreset({ { "radiusMeters", 3.0 } }));
		REQUIRE(p.radiusMeters == 3.0f);   // appliqué
		REQUIRE(p.strengthMps == 99.0f);   // intact
	}

	// --- splat_paint : struct plat SplatPaintParams ---------------------

	/// Un preset splat applique radius / strength / falloff. activeLayer
	/// et autoRules ne sont pas touchés (choix d'interaction).
	void Test_SplatPreset_AppliesAndLeavesInteractionState()
	{
		SplatPaintParams p;
		p.activeLayer = 5u;
		p.autoRules = true;
		const auto preset = MakePreset({
			{ "radiusMeters", 14.0 },
			{ "strength", 0.4 },
			{ "falloff", 0.85 },
		});
		presets::ApplySplatPaintPreset(p, preset);
		REQUIRE(p.radiusMeters == 14.0f);
		REQUIRE(p.strength == 0.4f);
		REQUIRE(p.falloff == 0.85f);
		REQUIRE(p.activeLayer == 5u);   // intact
		REQUIRE(p.autoRules == true);   // intact
	}

	/// Preset partiel : seule la clé définie change.
	void Test_SplatPreset_PartialLeavesOthers()
	{
		SplatPaintParams p;
		p.radiusMeters = 30.0f;
		p.falloff = 0.1f;
		presets::ApplySplatPaintPreset(p, MakePreset({ { "strength", 0.95 } }));
		REQUIRE(p.strength == 0.95f);     // appliqué
		REQUIRE(p.radiusMeters == 30.0f); // intact
		REQUIRE(p.falloff == 0.1f);       // intact
	}

	// --- river_network : struct WatershedSimulationParams ---------------

	/// Un preset river_network applique les seuils + carving. Le vecteur
	/// `springs` (sources posées par l'utilisateur) n'est jamais touché.
	void Test_RiverNetworkPreset_AppliesAndLeavesSprings()
	{
		WatershedSimulationParams p;
		p.springs.push_back({});  // une source déjà posée
		const auto preset = MakePreset({
			{ "minFlowThresholdCells", 40.0 },
			{ "simplificationToleranceMeters", 3.0 },
			{ "carveDepthMeters", 4.0 },
			{ "carveWidthMeters", 16.0 },
		});
		presets::ApplyRiverNetworkPreset(p, preset);
		REQUIRE(p.minFlowThresholdCells == 40u);
		REQUIRE(p.simplificationToleranceMeters == 3.0f);
		REQUIRE(p.carveDepthMeters == 4.0f);
		REQUIRE(p.carveWidthMeters == 16.0f);
		REQUIRE(p.springs.size() == 1u);   // intact
	}

	/// minFlowThresholdCells négatif clampé à 0 (pas de wrap uint).
	void Test_RiverNetworkPreset_NegativeThresholdClamped()
	{
		WatershedSimulationParams p;
		presets::ApplyRiverNetworkPreset(p,
			MakePreset({ { "minFlowThresholdCells", -10.0 } }));
		REQUIRE(p.minFlowThresholdCells == 0u);
	}

	/// Preset partiel : seule la clé définie change.
	void Test_RiverNetworkPreset_PartialLeavesOthers()
	{
		WatershedSimulationParams p;
		p.carveWidthMeters = 99.0f;
		presets::ApplyRiverNetworkPreset(p,
			MakePreset({ { "minFlowThresholdCells", 800.0 } }));
		REQUIRE(p.minFlowThresholdCells == 800u);  // appliqué
		REQUIRE(p.carveWidthMeters == 99.0f);      // intact
	}

	// --- macro polyline (mountain/valley) : globaux + par-vertex --------

	/// Un preset macro applique les globaux + les valeurs par-vertex à
	/// TOUS les sommets posés. Les positions des sommets restent intactes.
	void Test_MacroPreset_AppliesGlobalsAndAllVertices()
	{
		MacroPolylineParams p;
		PolylineVertex a; a.worldX = 10.0f; a.worldZ = 20.0f;
		PolylineVertex b; b.worldX = 90.0f; b.worldZ = 80.0f;
		p.vertices.push_back(a);
		p.vertices.push_back(b);

		const auto preset = MakePreset({
			{ "profile", 2.0 },
			{ "noiseFrequency", 0.006 },
			{ "widthMeters", 1400.0 },
			{ "heightMeters", 1600.0 },
			{ "noiseAmplitude", 90.0 },
			{ "asymmetry", 0.45 },
		});
		presets::ApplyMacroPolylinePreset(p, preset);

		REQUIRE(p.profile == FlankProfile::Exp);
		REQUIRE(p.noiseFrequency == 0.006f);
		for (const auto& v : p.vertices)
		{
			REQUIRE(v.widthMeters == 1400.0f);
			REQUIRE(v.heightMeters == 1600.0f);
			REQUIRE(v.noiseAmplitude == 90.0f);
			REQUIRE(v.asymmetry == 0.45f);
		}
		// positions intactes
		REQUIRE(p.vertices[0].worldX == 10.0f);
		REQUIRE(p.vertices[1].worldX == 90.0f);
	}

	/// Polyline vide : seuls les globaux changent, pas de crash.
	void Test_MacroPreset_EmptyPolylineOnlyGlobals()
	{
		MacroPolylineParams p;
		presets::ApplyMacroPolylinePreset(p,
			MakePreset({ { "noiseFrequency", 0.02 }, { "widthMeters", 500.0 } }));
		REQUIRE(p.noiseFrequency == 0.02f);
		REQUIRE(p.vertices.empty());
	}

	/// Preset partiel : une clé par-vertex absente laisse ce champ intact
	/// sur tous les sommets.
	void Test_MacroPreset_PartialLeavesVertexFields()
	{
		MacroPolylineParams p;
		PolylineVertex v; v.widthMeters = 333.0f; v.heightMeters = 777.0f;
		p.vertices.push_back(v);
		presets::ApplyMacroPolylinePreset(p, MakePreset({ { "heightMeters", 1000.0 } }));
		REQUIRE(p.vertices[0].heightMeters == 1000.0f); // appliqué
		REQUIRE(p.vertices[0].widthMeters == 333.0f);   // intact
	}

	// --- stamp : struct StampParams -------------------------------------

	/// Un preset stamp applique footprint/strength/rotation. mode et
	/// procedural (état d'interaction) ne sont pas touchés.
	void Test_StampPreset_AppliesAndLeavesInteractionState()
	{
		StampParams p;
		const bool procBefore = p.useProcedural;
		presets::ApplyStampPreset(p, MakePreset({
			{ "footprintMeters", 320.0 },
			{ "strengthMeters", 110.0 },
		}));
		REQUIRE(p.footprintMeters == 320.0f);
		REQUIRE(p.strengthMeters == 110.0f);
		REQUIRE(p.useProcedural == procBefore); // intact
	}

	/// Preset partiel stamp : seule la clé définie change.
	void Test_StampPreset_PartialLeavesOthers()
	{
		StampParams p;
		p.strengthMeters = 42.0f;
		presets::ApplyStampPreset(p, MakePreset({ { "footprintMeters", 55.0 } }));
		REQUIRE(p.footprintMeters == 55.0f);  // appliqué
		REQUIRE(p.strengthMeters == 42.0f);   // intact
	}

	// --- river_manual : deux scalaires (width, depth) -------------------

	/// Un preset river_manual applique les deux scalaires width/depth.
	void Test_RiverManualPreset_Applies()
	{
		float width = 6.0f, depth = 1.5f;
		presets::ApplyRiverManualPreset(width, depth,
			MakePreset({ { "width", 20.0 }, { "depth", 4.0 } }));
		REQUIRE(width == 20.0f);
		REQUIRE(depth == 4.0f);
	}

	/// Preset partiel river_manual : seule la clé définie change.
	void Test_RiverManualPreset_PartialLeavesOther()
	{
		float width = 6.0f, depth = 1.5f;
		presets::ApplyRiverManualPreset(width, depth,
			MakePreset({ { "width", 1.5 } }));
		REQUIRE(width == 1.5f);   // appliqué
		REQUIRE(depth == 1.5f);   // intact
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
	Test_SculptPreset_AppliesAndLeavesModeUntouched();
	Test_SculptPreset_NoiseOctavesClamped();
	Test_SculptPreset_PartialLeavesOthers();
	Test_SplatPreset_AppliesAndLeavesInteractionState();
	Test_SplatPreset_PartialLeavesOthers();
	Test_RiverNetworkPreset_AppliesAndLeavesSprings();
	Test_RiverNetworkPreset_NegativeThresholdClamped();
	Test_RiverNetworkPreset_PartialLeavesOthers();
	Test_MacroPreset_AppliesGlobalsAndAllVertices();
	Test_MacroPreset_EmptyPolylineOnlyGlobals();
	Test_MacroPreset_PartialLeavesVertexFields();
	Test_StampPreset_AppliesAndLeavesInteractionState();
	Test_StampPreset_PartialLeavesOthers();
	Test_RiverManualPreset_Applies();
	Test_RiverManualPreset_PartialLeavesOther();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ToolMigrationTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ToolMigrationTests] all tests passed\n");
	return 0;
}
