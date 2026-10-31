// SND.6 — Sons de récolte et d'artisanat (cf. InteractionAudio.h).

#include "src/client/audio/InteractionAudio.h"

namespace engine::audio
{
	std::string_view HarvestLoopFor(uint32_t nodeType)
	{
		namespace s = interaction_sounds;
		switch (nodeType)
		{
		case 1: return s::kMining;
		case 3: return s::kWood;
		case 4: return s::kSkinning;
		default: return s::kHerb;
		}
	}

	std::string_view CraftLoopFor(std::string_view profession)
	{
		namespace s = interaction_sounds;
		if (profession == "blacksmithing" || profession == "mining" || profession == "engineering")
			return s::kForge;
		if (profession == "alchemy" || profession == "cooking")
			return s::kAlchemy;
		if (profession == "leatherworking" || profession == "tailoring" || profession == "skinning")
			return s::kLeather;
		return s::kCraftGeneric;
	}

	InteractionAudioPlan PlanInteractionAudio(const InteractionAudioSnapshot& prev, const InteractionAudioSnapshot& cur)
	{
		namespace s = interaction_sounds;
		InteractionAudioPlan plan;
		if (!cur.valid)
			return plan;
		if (cur.harvesting)
			plan.loop = HarvestLoopFor(cur.harvestNodeType);
		else if (cur.crafting)
			plan.loop = CraftLoopFor(cur.craftProfession);

		// Les fins ne sonnent que sur une vraie transition (pas à l'entrée en jeu).
		if (!prev.valid)
			return plan;
		if (prev.harvesting && !cur.harvesting && cur.harvestFill >= 0.99f)
			plan.oneShots.push_back(s::kHarvestComplete);
		if (prev.crafting && !cur.crafting && cur.craftFill >= 0.99f)
			plan.oneShots.push_back(cur.lastQuality >= 2 ? s::kCraftCompleteRare : s::kCraftComplete);
		return plan;
	}
}
