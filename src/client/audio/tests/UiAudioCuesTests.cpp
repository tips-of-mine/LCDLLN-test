// SND.3 — Tests des sons d'interface déclenchés par changement d'état (UiAudioCues).

#include "src/client/audio/UiAudioCues.h"

#include <algorithm>
#include <iostream>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	bool Has(const std::vector<std::string_view>& v, std::string_view id)
	{
		return std::find(v.begin(), v.end(), id) != v.end();
	}
}

int main()
{
	using namespace engine::audio;
	namespace s = ui_sounds;
	UiAudioSnapshot a;
	a.valid = true;
	a.level = 3;
	a.gold = 10;
	a.quests["q1"] = { 2, 0 };

	// Première photo valide : aucun son (état initial).
	Check(DiffUiAudio(UiAudioSnapshot{}, a).empty(), "entree en jeu : silence");
	Check(DiffUiAudio(a, a).empty(), "rien ne change : silence");

	UiAudioSnapshot b = a;
	b.level = 4;
	Check(Has(DiffUiAudio(a, b), s::kLevelUp), "montee de niveau");

	b = a;
	b.quests["q2"] = { 2, 0 };
	Check(Has(DiffUiAudio(a, b), s::kQuestAccept), "quete acceptee");
	b = a;
	b.quests["q1"] = { 2, 1 };
	Check(Has(DiffUiAudio(a, b), s::kQuestObjective), "objectif progresse");
	b = a;
	b.quests["q1"] = { 3, 5 };
	Check(Has(DiffUiAudio(a, b), s::kQuestObjective), "objectifs remplis");
	UiAudioSnapshot ready = a;
	ready.quests["q1"] = { 3, 5 };
	b = ready;
	b.quests.erase("q1");
	Check(Has(DiffUiAudio(ready, b), s::kQuestComplete), "quete rendue (disparait du journal)");

	b = a;
	b.chatSerial = 42;
	Check(Has(DiffUiAudio(a, b), s::kChat), "message de discussion");
	b.lastChatChannel = 2;
	Check(Has(DiffUiAudio(a, b), s::kWhisper), "chuchotement");
	b.lastChatFromSelf = true;
	Check(DiffUiAudio(a, b).empty(), "son propre message : silence");

	b = a;
	b.gold = 25;
	b.inventoryUnits = 3;
	auto cues = DiffUiAudio(a, b);
	Check(Has(cues, s::kCoins) && !Has(cues, s::kItemPickup), "achat/vente : pieces seulement");
	b.gold = 10;
	Check(Has(DiffUiAudio(a, b), s::kItemPickup), "objet ramasse");
	b = a;
	b.equipmentHash = 99;
	Check(Has(DiffUiAudio(a, b), s::kItemEquip), "equipement change");

	b = a;
	b.openPanels = 1;
	Check(Has(DiffUiAudio(a, b), s::kOpen), "fenetre ouverte");
	Check(Has(DiffUiAudio(b, a), s::kClose), "fenetre fermee");
	b = a;
	b.mapOpen = true;
	Check(Has(DiffUiAudio(a, b), s::kMapOpen), "carte ouverte");
	Check(Has(DiffUiAudio(b, a), s::kMapClose), "carte fermee");
	b = a;
	b.placeBanner = true;
	Check(Has(DiffUiAudio(a, b), s::kPlaceDiscovered), "nouveau lieu");

	// Priorité + plafond : la montée de niveau passe avant le reste.
	b = a;
	b.level = 4;
	b.gold = 50;
	b.openPanels = 3;
	b.chatSerial = 7;
	b.mapOpen = true;
	cues = DiffUiAudio(a, b, 2);
	Check(cues.size() == 2 && cues[0] == s::kLevelUp, "plafond et priorite");

	Check(IsUiClick(false, true, true), "clic : element active a l'appui");
	Check(!IsUiClick(true, true, true), "element deja actif : pas de second clic");
	Check(!IsUiClick(false, true, false), "activation clavier : pas de clic");

	if (s_failCount == 0)
		std::cout << "[OK] ui_audio_cues_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
