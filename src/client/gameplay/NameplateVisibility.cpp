// FIX.10 — Règles d'affichage des plaques de nom (cf. NameplateVisibility.h).

#include "src/client/gameplay/NameplateVisibility.h"

namespace engine::client
{
	bool ShouldShowNameplate(NameplateCategory category, bool isSelected, float distanceM,
		const NameplateVisibilityParams& params)
	{
		const bool inVision = distanceM <= params.visionDistanceM;
		switch (category)
		{
		case NameplateCategory::Player:
		case NameplateCategory::Other:
			// Hors périmètre du retour joueur : comportement historique conservé.
			return true;
		case NameplateCategory::AmbientNpc:
			// Le nom d'un PNJ reste utile pour s'orienter, mais seulement à portée de vue.
			return inVision;
		case NameplateCategory::Animal:
			// Chasseur : il « sent » les animaux proches, sélectionnés ou non.
			return inVision && (isSelected || params.animalSense);
		case NameplateCategory::HostileMob:
			return inVision && isSelected;
		}
		return true;
	}

	bool ShouldAutoDeselect(float distanceM, const NameplateVisibilityParams& params)
	{
		return distanceM > params.visionDistanceM;
	}

	bool ShouldShowLevel(float distanceM, const NameplateVisibilityParams& params)
	{
		return distanceM <= params.levelRevealDistanceM;
	}

	bool ClassHasAnimalSense(std::string_view classId, std::string_view classListCsv)
	{
		if (classId.empty())
			return false;
		size_t start = 0;
		while (start <= classListCsv.size())
		{
			size_t end = classListCsv.find(',', start);
			if (end == std::string_view::npos)
				end = classListCsv.size();
			std::string_view token = classListCsv.substr(start, end - start);
			while (!token.empty() && token.front() == ' ')
				token.remove_prefix(1);
			while (!token.empty() && token.back() == ' ')
				token.remove_suffix(1);
			if (!token.empty() && token == classId)
				return true;
			start = end + 1;
		}
		return false;
	}
}
