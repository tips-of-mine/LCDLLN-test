#pragma once

#include "src/shared/core/Config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Textes lisibles d'une quête (titre, description, libellés d'étape) tels
	/// que chargés depuis le contenu data-driven pour une locale donnée.
	struct QuestTextEntry
	{
		std::string title;
		std::string description;
		std::vector<std::string> stepTemplates; ///< un template par étape, peut contenir {current}/{required}
	};

	/// Catalogue client des textes de quête (titre/description/étapes),
	/// résolu par locale depuis `quests/quest_texts.<locale>.json`.
	///
	/// Pur côté lecture : ne modifie aucun état de jeu, seulement de l'affichage.
	/// Toute clé absente retombe sur un texte par défaut plutôt que d'échouer,
	/// pour que l'UI reste lisible même si une quête n'a pas encore été traduite.
	class QuestTextCatalog final
	{
	public:
		/// Charge le fichier de textes pour \p locale (`quests/quest_texts.<locale>.json`,
		/// relatif à `paths.content`). Si le fichier pour \p locale est absent ou
		/// invalide, retente avec la locale de repli `fr`. Retourne false si même
		/// le repli échoue (le catalogue reste vide : tous les lookups utilisent
		/// alors les fallbacks de Title/Description/StepLabel).
		bool Load(const engine::core::Config& cfg, std::string_view locale);

		/// Titre lisible de la quête \p questId. Fallback : \p questId lui-même
		/// si la quête est absente du catalogue.
		std::string Title(std::string_view questId) const;

		/// Description lisible de la quête \p questId. Fallback : chaîne vide.
		std::string Description(std::string_view questId) const;

		/// Libellé lisible de l'étape \p stepIndex de la quête \p questId, avec
		/// substitution de `{current}`/`{required}` dans le template d'étape.
		/// Fallback (quête absente, étape hors bornes, ou template vide) :
		/// `"<current>/<required>"`.
		std::string StepLabel(std::string_view questId, size_t stepIndex, uint32_t current, uint32_t required) const;

	private:
		/// Charge et parse un fichier de textes de quêtes à un chemin de contenu
		/// donné. Remplace \ref m_entries en cas de succès uniquement.
		bool LoadFromContentPath(const engine::core::Config& cfg, const std::string& relativeContentPath);

		std::unordered_map<std::string, QuestTextEntry> m_entries;
	};
}
