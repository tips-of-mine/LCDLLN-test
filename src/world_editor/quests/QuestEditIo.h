#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world::quests
{
	/// Une étape éditable d'une quête (miroir authoring de `QuestStepDefinition`
	/// côté serveur, mais en texte brut : pas d'enum `QuestStepType` ici, la
	/// validation du token `type` est différée à la Tâche 2 (Validate)).
	struct EditedStep
	{
		std::string type;             ///< ex. "kill", "collect", "talk", "enter".
		std::string target;           ///< identifiant cible (ex. "mob:100", "npc:elder_marn").
		uint32_t requiredCount = 1;   ///< quantité requise pour compléter l'étape.
	};

	/// Un item de récompense éditable (itemId + quantité).
	struct EditedRewardItem
	{
		uint32_t itemId = 0;
		uint32_t quantity = 1;
	};

	/// Mode de re-réalisation d'une quête (EXT-2), miroir éditeur-local de
	/// `QuestRepeatMode` côté shard (`QuestRuntime.h`). Volontairement dupliqué
	/// ici pour éviter un couplage cross-module éditeur→shard : les valeurs
	/// entières DOIVENT rester alignées sur le shard (sérialisation JSON par
	/// chaîne, pas par entier — voir `RepeatModeToString`/`ParseRepeatMode`).
	/// - None : quête à réaliser une seule fois (comportement historique).
	/// - Repeatable : re-disponible immédiatement après complétion.
	/// - Daily : re-disponible au passage d'un jour UTC.
	/// - Weekly : re-disponible au passage d'une semaine UTC (borne lundi).
	/// - Cooldown : re-disponible après `cooldownHours` heures écoulées.
	enum class QuestRepeatMode : uint8_t
	{
		None = 0,
		Repeatable = 1,
		Daily = 2,
		Weekly = 3,
		Cooldown = 4
	};

	/// Une quête telle qu'éditée dans l'outil d'authoring : fusion des données
	/// mécaniques (`quest_definitions.json`) et des textes lisibles
	/// (`quest_texts.<lang>.json`), pour affichage/édition unifiés dans l'éditeur.
	struct EditedQuest
	{
		std::string id;
		std::string giver;
		std::string turnIn;
		std::vector<std::string> prereqs;
		/// Quêtes mutuellement exclusives (EXT-1) : s'engager dans cette quête
		/// rend indisponibles celles listées ici (et réciproquement, l'exclusion
		/// étant symétrique au runtime shard). Champ optionnel — absent = aucune
		/// exclusion. N'introduit AUCUNE contrainte d'acyclicité (contrairement à
		/// `prereqs`) : deux quêtes peuvent s'exclure mutuellement.
		std::vector<std::string> excludes;
		std::vector<EditedStep> steps;
		uint32_t rewardXp = 0;
		uint32_t rewardGold = 0;
		std::vector<EditedRewardItem> rewardItems;

		/// EXT-2 : mode de re-réalisation de la quête. Défaut `None` (absent du
		/// JSON = quête à réaliser une seule fois, rétro-compatible). Sérialisé
		/// via la clé JSON `"repeat"` (chaîne minuscule).
		QuestRepeatMode repeatMode = QuestRepeatMode::None;
		/// EXT-2 : durée du cooldown en heures, pertinente UNIQUEMENT si
		/// `repeatMode == Cooldown` (doit alors être > 0). Ignorée sinon.
		/// Sérialisée via la clé JSON `"cooldownHours"` (entier).
		uint32_t cooldownHours = 0;
		/// EXT-2 : si vrai, la quête se termine automatiquement (statut
		/// `Completed`) dès que toutes ses étapes sont finies, sans retour PNJ.
		/// Défaut false. Sérialisé via la clé JSON `"autoComplete"` (bool).
		bool autoComplete = false;

		// Textes lisibles, fusionnés depuis quest_texts.<lang>.json. Vides si la
		// quête n'a pas (encore) d'entrée de texte pour la langue chargée.
		std::string title;
		std::string description;
		std::vector<std::string> stepLabels; ///< un libellé brut par étape (même ordre que `steps`).
	};

	/// Entrées/sorties JSON de l'éditeur de quêtes (authoring, SP4).
	///
	/// Pur côté lecture pour cette tâche (Load uniquement) : Validate (Tâche 2)
	/// et Save (Tâche 3) viendront s'y ajouter. Ne modifie aucun état global ;
	/// toute l'E/S passe par des lectures disque explicites au moment de l'appel.
	class QuestEditIo
	{
	public:
		/// Charge les quêtes éditables depuis `<contentRoot>/quests/quest_definitions.json`
		/// (données mécaniques, JSON pur) puis fusionne les textes lisibles
		/// trouvés dans `<contentRoot>/quests/quest_texts.fr.json` (titre,
		/// description, libellés d'étape), appariés par `id` de quête.
		///
		/// \param contentRoot chemin de dossier de contenu (filesystem, pas relatif
		///        à une Config) ; ex. "game/data" en jeu, ou un dossier temporaire
		///        en test.
		/// \param out rempli avec une `EditedQuest` par quête trouvée dans
		///        quest_definitions.json (dans l'ordre du fichier). Vidé en cas
		///        d'échec.
		/// \param outError message d'erreur lisible en cas d'échec (fichier
		///        manquant, JSON invalide, champ requis absent). Vide en cas de
		///        succès.
		/// \return false si quest_definitions.json est absent/invalide ou mal
		///         formé ; true sinon (quest_texts.fr.json manquant n'est PAS
		///         une erreur : les champs texte restent vides).
		///
		/// Effet de bord : lecture disque (aucune écriture). Peut être appelée
		/// depuis n'importe quel thread tant que `contentRoot` n'est pas modifié
		/// concurremment sur disque.
		bool Load(const std::string& contentRoot, std::vector<EditedQuest>& out, std::string& outError) const;

		/// Valide l'ensemble de quêtes éditées : unicité des `id`, non-vacuité
		/// de `giver`/`turnIn`, formes des étapes (`type` connu, `target` non
		/// vide, `requiredCount` ≥ 1), existence des `prereqs` référencés dans
		/// l'ensemble, absence de cycle dans le graphe `prereqs`, existence des
		/// `excludes` référencés + interdiction de l'auto-exclusion (`id` dans
		/// son propre `excludes`) SANS contrainte d'acyclicité, formes des
		/// items de récompense (`itemId` > 0, `quantity` ≥ 1) et, pour EXT-2,
		/// cohérence du mode de re-réalisation (`cooldownHours` > 0 requis quand
		/// `repeatMode == Cooldown`).
		///
		/// Pure : aucune E/S, aucun état modifié (ni sur `this`, ni global) ;
		/// utilisable depuis n'importe quel thread sur une copie de \p quests.
		///
		/// \param quests ensemble de quêtes à valider (tel que produit par `Load`
		///        ou construit par l'UI d'édition).
		/// \param outErrors rempli avec un message d'erreur lisible (français)
		///        par violation détectée ; vidé en début d'appel. Peut contenir
		///        plusieurs messages pour une même quête. Vide si tout est valide.
		/// \return `outErrors.empty()` — true si aucune violation n'a été trouvée.
		bool Validate(const std::vector<EditedQuest>& quests, std::vector<std::string>& outErrors) const;

		/// Écrit l'ensemble \p quests sous `<contentRoot>/quests/` en 3 fichiers
		/// JSON purs (sérialisation manuelle, sans passer par `Config`) :
		/// - `quest_definitions.json` : données mécaniques (id/giver/turnIn/
		///   prereqs/steps/rewards), format tableaux (`{ "quests": [...] }`),
		///   compatible avec `QuestEditIo::Load` et avec le parseur JSON pur du
		///   shard (PAS le format `count`-indexé de `Config`, qui casserait
		///   `QuestRuntime`).
		/// - `quest_texts.fr.json` : textes lisibles (`{ "<id>": {title,
		///   description, steps:[...]} }`), appariés par `id`.
		/// - `quest_givers.json` : RÉGÉNÉRÉ intégralement à partir de `giver`/
		///   `turnIn` de chaque quête (ne lit pas l'éventuel fichier existant) ;
		///   groupé par PNJ cible, chaque entrée `{questId, role}` avec
		///   `role=0` pour un donneur (`giver`) et `role=1` pour un rendu
		///   (`turnIn`). Un même PNJ peut apparaître comme donneur ET receveur
		///   (pour la même quête ou des quêtes différentes) : toutes les
		///   entrées sont conservées.
		///
		/// \param contentRoot dossier de contenu (filesystem) sous lequel écrire
		///        `quests/` (créé si absent).
		/// \param quests ensemble de quêtes à écrire, tel quel (aucune validation
		///        implicite : appeler `Validate` avant `Save` si nécessaire).
		/// \param outError message d'erreur lisible en cas d'échec d'écriture
		///        (dossier non créable, fichier non ouvrable). Vide en cas de
		///        succès.
		/// \return false si l'un des 3 fichiers n'a pas pu être écrit ; true sinon.
		///
		/// Effet de bord : écriture disque (3 fichiers sous `<contentRoot>/quests/`,
		/// écrasés s'ils existent déjà). À appeler depuis le thread principal de
		/// l'éditeur (pas de synchronisation interne, comme `Load`/`Validate`).
		bool Save(const std::string& contentRoot, const std::vector<EditedQuest>& quests, std::string& outError) const;
	};
}
