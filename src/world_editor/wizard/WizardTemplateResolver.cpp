// M100.50 — Implémentation WizardTemplateResolver (template code-défini).

#include "src/world_editor/wizard/WizardTemplateResolver.h"

#include "src/world_editor/wizard/AutoGenerators.h"
#include "src/world_editor/wizard/ConditionEvaluator.h"

#include <string>

namespace engine::editor::world::wizard
{
	using engine::editor::world::zone_presets::ZonePreset;
	using engine::editor::world::zone_presets::ZonePresetOperation;

	namespace
	{
		/// Une entrée du template : type d'opération + condition "if" + affectedBy
		/// + un fragment rawJson (avec placeholders {{var}}).
		struct TemplateOp
		{
			std::string type;
			std::string condition;   ///< vide = toujours inclus.
			std::vector<std::string> affectedBy;
			std::string rawJsonTemplate;
		};

		/// Type d'opération « point d'intérêt » selon le choix POI.
		std::string PoiOpType(const std::string& poi)
		{
			if (poi == "cave")    return "place_cave";
			if (poi == "ruin")    return "place_arch";   // ruine ≈ structure (MVP).
			if (poi == "dungeon") return "place_dungeon";
			return ""; // "none"
		}

		/// Template code-défini (placeholders {{var}} + conditions "if").
		std::vector<TemplateOp> BuildTemplate(const WizardChoices& c)
		{
			std::vector<TemplateOp> ops;

			// 1. Macro relief (toujours) : valley pour plains, mountain sinon.
			if (c.relief == "plains")
				ops.push_back({ "valley_macro", "", { "relief" }, "{ \"relief\": \"{{relief}}\" }" });
			else
				ops.push_back({ "mountain_macro", "", { "relief" }, "{ \"relief\": \"{{relief}}\", \"seed\": {{seed}} }" });

			// 2. Splat selon climat (toujours).
			ops.push_back({ "splat_paint", "", { "dryness" }, "{ \"climate\": \"{{climate}}\" }" });

			// 3. Érosion hydraulique (toujours) — eau selon climat.
			ops.push_back({ "hydraulic_erosion", "", { "water_density" }, "{ \"climate\": \"{{climate}}\" }" });

			// 4. Côte (si pas intérieur).
			ops.push_back({ "coastline", "coast != 'interior'", {}, "{ \"coast\": \"{{coast}}\" }" });

			// 5. Point d'intérêt (si pas none).
			const std::string poiType = PoiOpType(c.poi);
			if (!poiType.empty())
				ops.push_back({ poiType, "poi != 'none'", {}, "{ \"poi\": \"{{poi}}\" }" });

			return ops;
		}
	}

	ZonePreset WizardTemplateResolver::Resolve(const WizardChoices& choices) const
	{
		ZonePreset preset;
		preset.version = 1;
		preset.id = "quickstart_" + choices.climate + "_" + choices.relief + "_" +
			choices.coast + "_" + choices.poi;
		preset.displayName.fr = "Assistant : " + choices.climate + " / " + choices.relief;
		preset.displayName.en = "Wizard: " + choices.climate + " / " + choices.relief;
		preset.tags = { "quickstart", choices.climate, choices.relief };
		preset.decorationEntryCount = 0u;

		for (const TemplateOp& t : BuildTemplate(choices))
		{
			// Condition "if" → skip si fausse.
			if (!EvaluateCondition(t.condition, choices))
				continue;

			ZonePresetOperation op;
			op.type = t.type;
			op.affectedBy = t.affectedBy;
			op.rawJson = SubstituteVariables(t.rawJsonTemplate, choices);
			preset.operations.push_back(std::move(op));
		}

		// Auto-gen : la polyline du macro relief (déterministe) est calculée ici
		// pour que l'exécuteur (ou les tests) puisse la consommer. On l'encode
		// dans le rawJson de la 1re opération (macro) pour rester self-contained.
		if (!preset.operations.empty())
		{
			const auto polyline = GenerateMountainPolyline(choices.relief, choices.seed);
			std::string pts = "[";
			for (size_t i = 0; i < polyline.size(); ++i)
			{
				if (i) pts += ",";
				pts += "[" + std::to_string(polyline[i].x) + "," +
					std::to_string(polyline[i].y) + "," + std::to_string(polyline[i].z) + "]";
			}
			pts += "]";
			preset.operations.front().rawJson += " /*polyline=*/ " + pts;
		}

		return preset;
	}
}
