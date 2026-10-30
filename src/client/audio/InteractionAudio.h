#pragma once

// SND.6 — Sons de récolte et d'artisanat.
//
// Tant qu'une barre de récolte ou d'artisanat avance, une BOUCLE de geste joue
// (pioche, hache, feuillage, couteau ; marteau et enclume, bulles d'alchimie,
// cuir…). À la fin, un son ponctuel : récolte terminée, objet fabriqué (plus
// brillant pour un objet rare ou épique). Une action interrompue coupe juste la
// boucle.
//
// Comme SND.3 / SND.5 : Engine prend une photo de l'état répliqué (UIModel
// harvest / crafting) et ce module décide quoi jouer. Module PUR : testé par
// interaction_audio_tests.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::audio
{
	namespace interaction_sounds
	{
		inline constexpr std::string_view kMining = "harvest_mining";
		inline constexpr std::string_view kWood = "harvest_wood";
		inline constexpr std::string_view kHerb = "harvest_herb";
		inline constexpr std::string_view kSkinning = "harvest_skinning";
		inline constexpr std::string_view kForge = "craft_forge";
		inline constexpr std::string_view kAlchemy = "craft_alchemy";
		inline constexpr std::string_view kLeather = "craft_leather";
		inline constexpr std::string_view kCraftGeneric = "craft_generic";
		inline constexpr std::string_view kHarvestComplete = "harvest_complete";
		inline constexpr std::string_view kCraftComplete = "craft_complete";
		inline constexpr std::string_view kCraftCompleteRare = "craft_complete_rare";
	}

	/// Photo de l'état de récolte / artisanat du joueur local.
	struct InteractionAudioSnapshot
	{
		bool valid = false;
		bool harvesting = false;
		/// Type de nœud (resource_nodes.json : 1 minerai, 2 plante, 3 arbre,
		/// 4 dépouille) ; 0 = inconnu.
		uint32_t harvestNodeType = 0;
		/// Remplissage de la barre de récolte (1 = terminée, 0 = annulée).
		float harvestFill = 0.0f;
		bool crafting = false;
		/// Métier de l'onglet d'artisanat ouvert ("blacksmithing", "alchemy"…).
		std::string craftProfession;
		/// Remplissage de la barre d'artisanat (1 = terminé, 0 = annulé).
		float craftFill = 0.0f;
		/// Qualité du dernier objet fabriqué (0 normal, 1 bon, 2 rare, 3 épique).
		uint8_t lastQuality = 0;
	};

	/// Ce qu'il faut jouer : la boucle voulue maintenant ("" = aucune) et les
	/// sons ponctuels déclenchés par la transition.
	struct InteractionAudioPlan
	{
		std::string_view loop;
		std::vector<std::string_view> oneShots;
	};

	/// Boucle de récolte d'un type de nœud (inconnu → feuillage).
	std::string_view HarvestLoopFor(uint32_t nodeType);
	/// Boucle d'artisanat d'un métier (inconnu → petits coups de bois).
	std::string_view CraftLoopFor(std::string_view profession);

	/// Plan sonore entre deux photos (la récolte prime sur l'artisanat si les
	/// deux sont actifs).
	InteractionAudioPlan PlanInteractionAudio(const InteractionAudioSnapshot& prev, const InteractionAudioSnapshot& cur);
}
