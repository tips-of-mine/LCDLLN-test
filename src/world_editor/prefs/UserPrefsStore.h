#pragma once

#include "src/world_editor/modes/EditorMode.h"
#include "src/world_editor/prefs/UserPrefs.h"

#include <filesystem>
#include <string>

namespace engine::editor::world::prefs
{
	/// Charge / persiste `UserPrefs` depuis `<contentRoot>/editor/user_prefs.json`
	/// (M100.45, Phase 12). Singleton accessible depuis tous les panels.
	///
	/// Sauvegarde **atomique** : écriture dans un fichier `.tmp` puis
	/// `rename` — un crash pendant `SaveToDisk` laisse soit l'ancien
	/// fichier intact, soit le nouveau complet, jamais un JSON tronqué.
	///
	/// Premier lancement : si `user_prefs.json` est absent, `Init` crée le
	/// fichier avec les défauts (`editorMode = Simple`).
	class UserPrefsStore
	{
	public:
		static UserPrefsStore& Instance();

		/// Pointe le store sur `<contentRoot>/editor/user_prefs.json` et
		/// charge le fichier (ou crée les défauts s'il est absent). À
		/// appeler une fois au boot de `WorldEditorShell`.
		/// \return true si un fichier existant a été chargé, false si les
		///         défauts ont été créés (premier lancement).
		bool Init(const std::string& contentRoot);

		const UserPrefs& Get() const { return m_prefs; }

		/// Remplace l'intégralité des prefs et persiste immédiatement.
		void Set(const UserPrefs& prefs);

		// --- Helpers ciblés (persistent immédiatement) ---
		modes::EditorMode GetEditorMode() const { return m_prefs.editorMode; }
		void              SetEditorMode(modes::EditorMode mode);

		std::string GetLastPresetForTool(const std::string& toolId) const;
		void        SetLastPresetForTool(const std::string& toolId, const std::string& presetId);

		bool GetTutorialFlag(const std::string& flagId) const;
		void SetTutorialFlag(const std::string& flagId, bool value);

		bool IsInitialized() const { return m_initialized; }

		/// Réinitialise l'état (défauts, non initialisé). Réservé aux tests.
		void ResetForTesting();

	private:
		UserPrefsStore() = default;

		void LoadFromDisk();
		bool SaveToDisk() const;  ///< atomique : .tmp + rename

		UserPrefs             m_prefs;
		std::filesystem::path m_path;
		bool                  m_initialized = false;
	};
}
