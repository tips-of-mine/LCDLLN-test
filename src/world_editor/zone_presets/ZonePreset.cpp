#include "src/world_editor/zone_presets/ZonePreset.h"

#include <array>
#include <string_view>
#include <unordered_set>

namespace engine::editor::world::zone_presets
{
	const std::string& LocalizedString::Get(const std::string& lang) const
	{
		if (lang == "en" && !en.empty()) return en;
		if (lang == "fr" && !fr.empty()) return fr;
		if (!fr.empty()) return fr;
		return en;
	}

	bool IsKnownOperationType(const std::string& type)
	{
		// Les 14 types d'opérations du spec M100.46 §A.3 (outils M100.35-43).
		static const std::unordered_set<std::string_view> kKnown = {
			"mountain_macro", "valley_macro", "sculpt_brush", "splat_paint",
			"lake_polygon", "river_manual", "coastline", "river_network",
			"hydraulic_erosion", "thermal_wind_erosion",
			"place_cave", "place_overhang", "place_arch", "place_dungeon",
		};
		return kKnown.find(std::string_view(type)) != kKnown.end();
	}

	bool IsKnownAffectedByTag(const std::string& tag)
	{
		return tag == "relief" || tag == "water_density" || tag == "dryness";
	}

	bool ZonePreset::Validate(std::string& errorOut) const
	{
		if (id.empty())
		{
			errorOut = "ZonePreset: id vide";
			return false;
		}
		if (version <= 0)
		{
			errorOut = "ZonePreset[" + id + "]: version invalide";
			return false;
		}
		if (operations.empty())
		{
			errorOut = "ZonePreset[" + id + "]: aucune opération";
			return false;
		}
		for (size_t i = 0; i < operations.size(); ++i)
		{
			const ZonePresetOperation& op = operations[i];
			if (!IsKnownOperationType(op.type))
			{
				errorOut = "ZonePreset[" + id + "]: opération " + std::to_string(i)
					+ " type inconnu '" + op.type + "'";
				return false;
			}
			for (const std::string& tag : op.affectedBy)
			{
				if (!IsKnownAffectedByTag(tag))
				{
					errorOut = "ZonePreset[" + id + "]: opération " + std::to_string(i)
						+ " affectedBy inconnu '" + tag + "'";
					return false;
				}
			}
		}
		// La décoration est réservée Phase 13 : un preset MVP ne doit pas
		// en contenir (le format l'autorise, mais le catalogue livré non).
		if (decorationEntryCount != 0u)
		{
			errorOut = "ZonePreset[" + id + "]: section decoration non vide ("
				+ std::to_string(decorationEntryCount)
				+ " entrées) — réservée Phase 13";
			return false;
		}
		return true;
	}
}
