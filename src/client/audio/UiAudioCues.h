#pragma once

// SND.3 — Sons d'interface déclenchés par les changements d'état du jeu.
//
// Plutôt que de semer des appels audio dans chaque fenêtre, Engine prend une
// photo compacte de l'état visible à chaque frame (niveau, quêtes, discussion,
// bourse, inventaire, fenêtres ouvertes…) ; la comparaison avec la photo
// précédente donne les sons à jouer : « montée de niveau », « quête acceptée »,
// « pièces », « fenêtre ouverte »…
//
// Module PUR (testé par ui_audio_cues_tests). La première photo valide (entrée
// en jeu) ne produit aucun son : on n'annonce pas l'état initial.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::audio
{
	/// Photo de l'état observé par l'audio d'interface.
	struct UiAudioSnapshot
	{
		bool valid = false;                 ///< false : pas encore en jeu
		uint32_t level = 0;                 ///< niveau du personnage (0 = inconnu)
		/// Quêtes : id → (statut wire, somme des progressions d'étapes).
		/// Statuts : 1 proposée, 2 active, 3 prête à rendre, 4 terminée.
		std::unordered_map<std::string, std::pair<uint8_t, uint32_t>> quests;
		uint64_t chatSerial = 0;            ///< change à chaque nouvelle ligne (horodatage de la dernière)
		uint8_t lastChatChannel = 0;        ///< canal de la dernière ligne (2 = chuchotement)
		bool lastChatFromSelf = false;      ///< la dernière ligne vient du joueur local
		uint32_t gold = 0;
		uint64_t inventoryUnits = 0;        ///< somme des quantités d'objets
		uint64_t equipmentHash = 0;         ///< empreinte de l'équipement porté
		bool lootVisible = false;
		bool partyInvite = false;
		uint32_t openPanels = 0;            ///< 1 bit par fenêtre (hors carte)
		bool mapOpen = false;
		bool placeBanner = false;           ///< bannière « Vous entrez à … » affichée
	};

	/// Identifiants des sons d'interface (catalogue zone_audio.json, bus UI).
	namespace ui_sounds
	{
		inline constexpr std::string_view kClick = "ui_click";
		inline constexpr std::string_view kOpen = "ui_open";
		inline constexpr std::string_view kClose = "ui_close";
		inline constexpr std::string_view kError = "ui_error";
		inline constexpr std::string_view kNotify = "ui_notify";
		inline constexpr std::string_view kChat = "chat_message";
		inline constexpr std::string_view kWhisper = "chat_whisper";
		inline constexpr std::string_view kQuestAccept = "quest_accept";
		inline constexpr std::string_view kQuestObjective = "quest_objective";
		inline constexpr std::string_view kQuestComplete = "quest_complete";
		inline constexpr std::string_view kLevelUp = "level_up";
		inline constexpr std::string_view kCoins = "coins";
		inline constexpr std::string_view kItemPickup = "item_pickup";
		inline constexpr std::string_view kItemEquip = "item_equip";
		inline constexpr std::string_view kLootOpen = "loot_open";
		inline constexpr std::string_view kMapOpen = "map_open";
		inline constexpr std::string_view kMapClose = "map_close";
		inline constexpr std::string_view kPlaceDiscovered = "place_discovered";
	}

	/// Sons à jouer entre deux photos, par ordre d'importance, au plus `maxCues`
	/// (les sons « fanfare » — niveau, quête terminée — passent en premier).
	std::vector<std::string_view> DiffUiAudio(const UiAudioSnapshot& prev, const UiAudioSnapshot& cur,
		size_t maxCues = 3);

	/// Détecte un clic d'interface : un élément devient actif à la frame d'un
	/// appui de souris. \param wasActive / isActive : « un élément est actif »
	/// à la frame précédente / courante ; \param mouseClicked : appui ce frame.
	inline bool IsUiClick(bool wasActive, bool isActive, bool mouseClicked)
	{
		return mouseClicked && isActive && !wasActive;
	}
}
