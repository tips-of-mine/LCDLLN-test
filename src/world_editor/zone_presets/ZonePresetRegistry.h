#pragma once

#include "src/world_editor/zone_presets/ZonePreset.h"

#include <string>
#include <vector>

namespace engine::editor::world::zone_presets
{
	/// Charge et indexe tous les `zone_presets/*.json` (M100.46). Singleton
	/// accessible depuis le dialog « Nouvelle zone depuis preset ».
	///
	/// Chargement **tolérant** : un fichier JSON malformé ou un preset qui
	/// échoue à `Validate()` est loggé (warning) et ignoré ; les autres
	/// sont chargés normalement. L'éditeur démarre toujours, même catalogue
	/// partiel.
	class ZonePresetRegistry
	{
	public:
		static ZonePresetRegistry& Instance();

		/// Charge tous les `*.json` de `<contentRoot>/editor/zone_presets/`.
		/// Remplace l'index courant. \return le nombre de presets valides
		/// chargés.
		size_t LoadFromContentPath(const std::string& contentRoot);

		/// Recharge depuis le dernier `contentRoot` (hot-reload dev).
		size_t Reload();

		const std::vector<ZonePreset>& Presets() const { return m_presets; }
		size_t Size() const { return m_presets.size(); }

		/// Recherche par id. nullptr si introuvable.
		const ZonePreset* FindById(const std::string& id) const;

		void ResetForTesting();

	private:
		ZonePresetRegistry() = default;

		std::vector<ZonePreset> m_presets;
		std::string             m_lastContentRoot;
	};
}
