// Tests SP2 (client) — réception de QuestGiverList (opcode 92) dans UIModel.
//
// Round-trip : EncodeQuestGiverList (SP1, src/shared/network/ServerProtocol.h)
// produit les octets envoyés par le shard au Talk d'un PNJ ; ApplyQuestGiverList
// (UIModelBinding) doit décoder ce paquet et peupler model.giverList
// (npcTargetId + entries[{questId, role}]), à l'instar de ApplyQuestDelta.
//
// Style non-strippable (cerr/compteur/return 1), à l'instar de
// QuestTextCatalogTests.cpp : aucune assertion n'est retirée en build NDEBUG.

#include "src/client/ui_common/UIModel.h"
#include "src/shared/network/ServerProtocol.h"

#include <iostream>

using engine::client::UIModelBinding;
using engine::server::QuestGiverEntry;
using engine::server::QuestGiverListMessage;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "[FAIL] " << label << "\n";
			++g_failures;
		}
	}
}

int main()
{
	// Round-trip nominal : Encode -> Apply -> vérifie npcTargetId + entries.
	{
		QuestGiverListMessage source{};
		source.clientId = 7;
		source.npcTargetId = "npc:elder_marn";
		source.entries.push_back(QuestGiverEntry{ "kill_10_boars", 0 });
		source.entries.push_back(QuestGiverEntry{ "kill_10_boars", 1 });

		const std::vector<std::byte> packet = engine::server::EncodeQuestGiverList(source);

		UIModelBinding binding;
		binding.Init();

		Check(binding.ApplyPacket(packet), "ApplyPacket(QuestGiverList) réussit");

		const engine::client::UIModel& model = binding.GetModel();
		Check(model.giverList.npcTargetId == "npc:elder_marn", "giverList.npcTargetId == \"npc:elder_marn\"");
		Check(model.giverList.entries.size() == 2, "giverList.entries.size() == 2");
		if (model.giverList.entries.size() == 2)
		{
			Check(model.giverList.entries[0].questId == "kill_10_boars", "entries[0].questId == \"kill_10_boars\"");
			Check(model.giverList.entries[0].role == 0, "entries[0].role == 0 (offer)");
			Check(model.giverList.entries[1].questId == "kill_10_boars", "entries[1].questId == \"kill_10_boars\"");
			Check(model.giverList.entries[1].role == 1, "entries[1].role == 1 (turnin)");
		}
	}

	// Second paquet : la liste précédente est bien remplacée, pas accumulée.
	{
		QuestGiverListMessage source{};
		source.clientId = 7;
		source.npcTargetId = "npc:other_giver";
		source.entries.push_back(QuestGiverEntry{ "escort_caravan", 0 });

		const std::vector<std::byte> packet = engine::server::EncodeQuestGiverList(source);

		UIModelBinding binding;
		binding.Init();
		binding.ApplyPacket(packet);

		const engine::client::UIModel& model = binding.GetModel();
		Check(model.giverList.npcTargetId == "npc:other_giver", "2e paquet remplace npcTargetId");
		Check(model.giverList.entries.size() == 1, "2e paquet remplace entries (pas d'accumulation)");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
