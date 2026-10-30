// SND.3 — Sons d'interface par comparaison d'état (cf. UiAudioCues.h).

#include "src/client/audio/UiAudioCues.h"

#include <algorithm>

namespace engine::audio
{
	namespace
	{
		constexpr uint8_t kActive = 2;
		constexpr uint8_t kReady = 3;
		constexpr uint8_t kCompleted = 4;
		constexpr uint8_t kWhisperChannel = 2;

		/// Importance d'un son (plus petit = plus important).
		int Priority(std::string_view id)
		{
			using namespace ui_sounds;
			if (id == kLevelUp) return 0;
			if (id == kQuestComplete) return 1;
			if (id == kPlaceDiscovered) return 2;
			if (id == kQuestAccept) return 3;
			if (id == kQuestObjective) return 4;
			if (id == kWhisper) return 5;
			if (id == kNotify) return 6;
			if (id == kLootOpen) return 7;
			if (id == kItemEquip) return 8;
			if (id == kCoins) return 9;
			if (id == kItemPickup) return 10;
			if (id == kMapOpen || id == kMapClose) return 11;
			if (id == kChat) return 12;
			return 13; // ouverture / fermeture de fenêtre
		}
	}

	std::vector<std::string_view> DiffUiAudio(const UiAudioSnapshot& prev, const UiAudioSnapshot& cur, size_t maxCues)
	{
		using namespace ui_sounds;
		std::vector<std::string_view> out;
		if (!cur.valid || !prev.valid)
			return out; // entrée en jeu / sortie : aucun son pour l'état initial

		auto add = [&](std::string_view id) {
			if (std::find(out.begin(), out.end(), id) == out.end())
				out.push_back(id);
		};

		if (prev.level > 0 && cur.level > prev.level)
			add(kLevelUp);

		for (const auto& [id, st] : cur.quests)
		{
			const auto it = prev.quests.find(id);
			const uint8_t was = it != prev.quests.end() ? it->second.first : 0;
			const uint32_t wasProgress = it != prev.quests.end() ? it->second.second : 0;
			if (st.first >= kActive && was < kActive)
				add(kQuestAccept);
			else if (st.first == kCompleted && was != kCompleted)
				add(kQuestComplete);
			else if (st.first == kReady && was == kActive)
				add(kQuestObjective); // tous les objectifs remplis
			else if (st.first == kActive && st.second > wasProgress)
				add(kQuestObjective);
		}
		// Quête prête à rendre qui disparaît du journal : rendue au PNJ.
		for (const auto& [id, st] : prev.quests)
			if (st.first == kReady && cur.quests.find(id) == cur.quests.end())
				add(kQuestComplete);

		if (cur.chatSerial != prev.chatSerial && cur.chatSerial != 0 && !cur.lastChatFromSelf)
			add(cur.lastChatChannel == kWhisperChannel ? kWhisper : kChat);

		const bool goldChanged = cur.gold != prev.gold;
		if (goldChanged)
			add(kCoins);
		if (cur.inventoryUnits > prev.inventoryUnits && !goldChanged)
			add(kItemPickup);
		if (cur.equipmentHash != prev.equipmentHash)
			add(kItemEquip);
		if (cur.lootVisible && !prev.lootVisible)
			add(kLootOpen);
		if (cur.partyInvite && !prev.partyInvite)
			add(kNotify);
		if (cur.placeBanner && !prev.placeBanner)
			add(kPlaceDiscovered);

		if (cur.mapOpen != prev.mapOpen)
			add(cur.mapOpen ? kMapOpen : kMapClose);
		const uint32_t opened = cur.openPanels & ~prev.openPanels;
		const uint32_t closed = prev.openPanels & ~cur.openPanels;
		if (opened != 0)
			add(kOpen);
		else if (closed != 0)
			add(kClose);

		std::stable_sort(out.begin(), out.end(), [](std::string_view a, std::string_view b) { return Priority(a) < Priority(b); });
		if (out.size() > maxCues)
			out.resize(maxCues);
		return out;
	}
}
