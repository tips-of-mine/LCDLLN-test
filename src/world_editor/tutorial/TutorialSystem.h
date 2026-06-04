#pragma once

// M100.49 — TutorialSystem : pilote un tutoriel via l'OverlayGuidanceSystem.
// Logique pure (état + flags en mémoire) ; testable headless en pilotant
// l'overlay par NotifyAction. La persistance des flags dans UserPrefs (M100.45)
// est différée (2e passe) — le système expose une table de flags en mémoire.

#include <cstddef>
#include <string>
#include <unordered_map>

#include "src/world_editor/help/OverlayGuidanceSystem.h"
#include "src/world_editor/tutorial/Tutorial.h"

namespace engine::editor::world::tutorial
{
	/// État courant du tutoriel.
	enum class TutorialState
	{
		NotStarted,
		Running,
		Paused,    ///< Quitté pour l'instant ; reprenable.
		Skipped,   ///< Passé explicitement ; ne se relance pas auto.
		Completed, ///< Terminé jusqu'au bout.
	};

	/// Clé de flag standard pour le tutoriel de premier lancement.
	inline constexpr const char* kFirstLaunchFlag = "first_launch_tutorial_completed";

	class TutorialSystem
	{
	public:
		/// Charge la définition de tutoriel active (copie).
		void LoadTutorial(Tutorial tutorial) { m_tutorial = std::move(tutorial); }
		const Tutorial& Def() const { return m_tutorial; }

		/// Démarre le tutoriel depuis l'étape 0 : construit les instructions des
		/// étapes et lance la séquence sur `overlay`. État → Running.
		void Start(engine::editor::world::help::OverlayGuidanceSystem& overlay);

		/// Reprend depuis l'étape sauvegardée (SavedProgress). État → Running.
		void Resume(engine::editor::world::help::OverlayGuidanceSystem& overlay);

		/// Quitte pour l'instant : mémorise l'index courant de `overlay`, abandonne
		/// la séquence, état → Paused.
		void PauseFrom(engine::editor::world::help::OverlayGuidanceSystem& overlay);

		/// Passe le tutoriel : état → Skipped, flag = "skipped".
		void Skip();

		TutorialState State() const { return m_state; }
		size_t SavedProgress() const { return m_savedProgress; }
		bool IsCompleted() const { return m_state == TutorialState::Completed; }
		bool IsSkipped() const { return m_state == TutorialState::Skipped; }

		/// Flags de complétion (clé → valeur). Persistance disque différée.
		void SetFlag(const std::string& key, const std::string& value) { m_flags[key] = value; }
		std::string GetFlag(const std::string& key) const
		{
			auto it = m_flags.find(key);
			return it == m_flags.end() ? std::string() : it->second;
		}

	private:
		/// Démarre la séquence overlay à partir de `fromStep` (interne).
		void StartFrom(engine::editor::world::help::OverlayGuidanceSystem& overlay, size_t fromStep);
		/// Callback de complétion de la séquence : état → Completed + flag.
		void OnSequenceComplete();

		Tutorial      m_tutorial;
		TutorialState m_state = TutorialState::NotStarted;
		size_t        m_savedProgress = 0;
		std::unordered_map<std::string, std::string> m_flags;
	};
}
