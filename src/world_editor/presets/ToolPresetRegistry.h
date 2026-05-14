#pragma once

#include "src/world_editor/presets/ToolPreset.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::editor::world::presets
{
	/// Charge et indexe tous les `tool_presets/*.json` (M100.45, Phase 12).
	/// Singleton accessible depuis tous les `ToolPropertiesPanel`.
	///
	/// Chaque fichier `<toolId>.json` contribue ses presets sous la clé
	/// `toolId` (lue dans le JSON, pas dérivée du nom de fichier). Le
	/// chargement est **tolérant** : un fichier corrompu est loggé et
	/// ignoré, les autres sont chargés normalement.
	class ToolPresetRegistry
	{
	public:
		static ToolPresetRegistry& Instance();

		/// Charge tous les `*.json` de `<contentRoot>/editor/tool_presets/`.
		/// Remplace l'index courant. \return le nombre de fichiers chargés
		/// avec succès.
		size_t LoadFromContentPath(const std::string& contentRoot);

		/// Recharge depuis le dernier `contentRoot` passé à
		/// `LoadFromContentPath` (hot-reload dev, Ctrl+Shift+R).
		size_t Reload();

		/// Presets d'un outil. Vide si l'outil n'a pas de fichier de presets.
		const std::vector<ToolPreset>& GetPresetsForTool(const std::string& toolId) const;

		/// Id du preset par défaut d'un outil (champ `defaultPreset` du
		/// JSON). Vide si non défini.
		std::string GetDefaultPresetId(const std::string& toolId) const;

		/// Recherche un preset précis. nullptr si introuvable.
		const ToolPreset* FindPreset(const std::string& toolId,
			const std::string& presetId) const;

		size_t ToolCount() const { return m_presetsByTool.size(); }

		void ResetForTesting();

	private:
		ToolPresetRegistry() = default;

		std::unordered_map<std::string, std::vector<ToolPreset>> m_presetsByTool;
		std::unordered_map<std::string, std::string>             m_defaultByTool;
		std::string                                              m_lastContentRoot;
	};
}
