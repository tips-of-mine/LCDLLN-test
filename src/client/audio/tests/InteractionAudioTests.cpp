// SND.6 — Tests des sons de récolte et d'artisanat (InteractionAudio).

#include "src/client/audio/InteractionAudio.h"

#include <iostream>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	using namespace engine::audio;
	namespace s = interaction_sounds;

	Check(HarvestLoopFor(1) == s::kMining, "minerai = pioche");
	Check(HarvestLoopFor(2) == s::kHerb, "plante = feuillage");
	Check(HarvestLoopFor(3) == s::kWood, "arbre = hache");
	Check(HarvestLoopFor(4) == s::kSkinning, "depouille = couteau");
	Check(HarvestLoopFor(0) == s::kHerb, "inconnu = feuillage");
	Check(CraftLoopFor("blacksmithing") == s::kForge, "forge");
	Check(CraftLoopFor("alchemy") == s::kAlchemy, "alchimie");
	Check(CraftLoopFor("leatherworking") == s::kLeather, "cuir");
	Check(CraftLoopFor("") == s::kCraftGeneric, "metier inconnu");

	InteractionAudioSnapshot idle;
	idle.valid = true;
	Check(PlanInteractionAudio(idle, idle).loop.empty(), "repos : pas de boucle");

	// Récolte d'un arbre : boucle hache, puis fin réussie.
	InteractionAudioSnapshot h = idle;
	h.harvesting = true;
	h.harvestNodeType = 3;
	auto p = PlanInteractionAudio(idle, h);
	Check(p.loop == s::kWood && p.oneShots.empty(), "recolte en cours");
	InteractionAudioSnapshot done = idle;
	done.harvestFill = 1.0f;
	p = PlanInteractionAudio(h, done);
	Check(p.loop.empty() && p.oneShots.size() == 1 && p.oneShots[0] == s::kHarvestComplete, "recolte terminee");
	// Récolte annulée : la boucle s'arrête, aucun son de fin.
	p = PlanInteractionAudio(h, idle);
	Check(p.loop.empty() && p.oneShots.empty(), "recolte annulee");

	// Artisanat : forge, puis objet épique.
	InteractionAudioSnapshot c = idle;
	c.crafting = true;
	c.craftProfession = "blacksmithing";
	p = PlanInteractionAudio(idle, c);
	Check(p.loop == s::kForge, "forge en cours");
	InteractionAudioSnapshot crafted = idle;
	crafted.craftProfession = "blacksmithing";
	crafted.craftFill = 1.0f;
	crafted.lastQuality = 3;
	p = PlanInteractionAudio(c, crafted);
	Check(p.oneShots.size() == 1 && p.oneShots[0] == s::kCraftCompleteRare, "objet epique");
	crafted.lastQuality = 0;
	p = PlanInteractionAudio(c, crafted);
	Check(p.oneShots.size() == 1 && p.oneShots[0] == s::kCraftComplete, "objet normal");

	// La récolte prime si les deux barres sont actives.
	InteractionAudioSnapshot both = h;
	both.crafting = true;
	both.craftProfession = "alchemy";
	Check(PlanInteractionAudio(idle, both).loop == s::kWood, "recolte prioritaire");

	// Entrée en jeu : boucle éventuelle, mais aucun son de fin.
	Check(PlanInteractionAudio(InteractionAudioSnapshot{}, done).oneShots.empty(), "premiere photo : pas de fin");
	Check(PlanInteractionAudio(h, InteractionAudioSnapshot{}).loop.empty(), "photo invalide : silence");

	if (s_failCount == 0)
		std::cout << "interaction_audio_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
