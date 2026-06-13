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

		/// Identifiant catalogue par défaut associé au choix POI. Doit exister
		/// dans le catalogue correspondant (dungeon/cave/arch) côté
		/// OperationDispatcher, sinon l'op `place_*` échoue (`catalogId
		/// introuvable`). MVP : un seul preset de départ par type.
		/// \return chaîne vide pour "none" (aucune op POI émise).
		std::string PoiCatalogId(const std::string& poi)
		{
			if (poi == "cave")    return "cave_small_01";
			if (poi == "ruin")    return "arch_small_01";
			if (poi == "dungeon") return "dungeon_starter_keep";
			return "";
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

			// 2. (Retiré) Le splat_paint n'est pas câblé dans l'OperationDispatcher
			// (skip non-fatal « Unsupported » qui polluait le résumé avec « 1 skipped »).
			// La splat procédurale par défaut s'applique de toute façon ; on ne
			// l'émet donc plus dans le template du wizard.

			// 3. Érosion hydraulique (toujours) — eau selon climat.
			ops.push_back({ "hydraulic_erosion", "", { "water_density" }, "{ \"climate\": \"{{climate}}\" }" });

			// 4. Côte (si pas intérieur).
			ops.push_back({ "coastline", "coast != 'interior'", {}, "{ \"coast\": \"{{coast}}\" }" });

			// 5. Point d'intérêt (si pas none). Le rawJson (catalogId +
			// worldPosition) est construit dans `Resolve` une fois la polyline
			// connue (worldPosition = point médian de la polyline) ; on laisse
			// ici un fragment minimal qui sera ENTIÈREMENT remplacé.
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

		// Auto-gen : la polyline du macro relief (déterministe pour
		// (relief, seed)) est calculée ici puis injectée dans les ops qui en
		// dépendent. `GenerateMountainPolyline` renvoie des `Vec3` en
		// coordonnées MONDE (x/z planaires en mètres, y = hauteur du relief).
		if (!preset.operations.empty())
		{
			const auto polyline = GenerateMountainPolyline(choices.relief, choices.seed);

			// --- 1) Champ "polyline" PLAT [x0,z0,x1,z1,…] (paires x,z, on DROP
			// le y) injecté DANS l'objet JSON de l'op macro (1re op). Le
			// dispatcher (BuildMacroParamsFromJson) exige flat.size() >= 4 et
			// pair. On insère `, "polyline": <flat>` juste avant la dernière `}`.
			std::string flat = "[";
			for (size_t i = 0; i < polyline.size(); ++i)
			{
				if (i) flat += ",";
				flat += std::to_string(polyline[i].x) + "," +
					std::to_string(polyline[i].z);
			}
			flat += "]";

			std::string& macroJson = preset.operations.front().rawJson;
			const size_t closeBrace = macroJson.find_last_of('}');
			if (closeBrace != std::string::npos)
			{
				macroJson.insert(closeBrace, ", \"polyline\": " + flat);
			}

			// --- 2) Op POI (si présente) : reconstruit ENTIÈREMENT son rawJson
			// avec catalogId + position. Le point d'ancrage est le point MÉDIAN
			// de la polyline (même espace de coordonnées que le relief), à y=0
			// (le client recale au sol). `place_cave`/`place_dungeon` lisent
			// `worldPosition` (3 floats plats [x,y,z]) ; `place_arch` (= ruine)
			// exige plutôt `pillarA`/`pillarB` (le dispatcher en dérive le
			// midpoint/yaw/scale) → on émet deux piliers écartés autour du médian.
			if (!polyline.empty())
			{
				const std::string poiType = PoiOpType(choices.poi);
				const std::string catalogId = PoiCatalogId(choices.poi);
				if (!poiType.empty() && !catalogId.empty())
				{
					const engine::math::Vec3 mid = polyline[polyline.size() / 2];
					const std::string mx = std::to_string(mid.x);
					const std::string mz = std::to_string(mid.z);

					for (ZonePresetOperation& op : preset.operations)
					{
						if (op.type != poiType)
							continue;

						if (poiType == "place_arch")
						{
							// Deux piliers espacés de ~20 m sur X autour du médian.
							constexpr float kHalfSpanMeters = 10.0f;
							const std::string ax = std::to_string(mid.x - kHalfSpanMeters);
							const std::string bx = std::to_string(mid.x + kHalfSpanMeters);
							op.rawJson =
								"{ \"poi\": \"" + choices.poi + "\""
								", \"catalogId\": \"" + catalogId + "\""
								", \"pillarA\": [" + ax + ",0," + mz + "]"
								", \"pillarB\": [" + bx + ",0," + mz + "] }";
						}
						else
						{
							op.rawJson =
								"{ \"poi\": \"" + choices.poi + "\""
								", \"catalogId\": \"" + catalogId + "\""
								", \"worldPosition\": [" + mx + ",0," + mz + "] }";
						}
					}
				}
			}
		}

		return preset;
	}
}
