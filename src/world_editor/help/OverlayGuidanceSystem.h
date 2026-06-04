#pragma once

// M100.49 — OverlayGuidanceSystem : moteur de SÉQUENCE d'instructions (logique
// PURE, testable headless). Partagé entre tutoriel et (futur) diagnostic.
//
// Le moteur gère l'enchaînement des étapes : démarrage d'une séquence, étape
// courante, avancement (manuel ou sur action validée), abandon, complétion. Le
// rendu visuel (voile, rectangle pulsant, bulle) est une passe UI séparée
// (Windows) qui lit CurrentInstruction() + le WidgetTargetRegistry — non incluse
// dans ce cœur pour le garder pur.

#include <cstddef>
#include <functional>
#include <vector>

#include "src/world_editor/tutorial/Tutorial.h"

namespace engine::editor::world::help
{
	/// Moteur de séquence d'OverlayInstruction. Instanciable (pas un singleton
	/// global) pour rester testable ; le shell en tient une instance.
	class OverlayGuidanceSystem
	{
	public:
		using OnCompleteCallback = std::function<void()>;

		/// Démarre une séquence. Remplace toute séquence active. `onComplete` est
		/// appelé quand la dernière étape est franchie (ou null).
		void StartSequence(std::vector<OverlayInstruction> instructions, OnCompleteCallback onComplete = nullptr);

		/// Avance d'une étape. Si c'était la dernière, termine la séquence et
		/// invoque le callback de complétion. No-op si aucune séquence active.
		void AdvanceStep();

		/// Signale qu'une action utilisateur a eu lieu sur `widgetId`. Si l'étape
		/// courante attend cette action (validatesOnAction == widgetId), avance.
		/// \return true si l'action a fait avancer la séquence.
		bool NotifyAction(const WidgetTargetId& widgetId);

		/// Abandonne la séquence en cours (sans appeler onComplete). No-op si
		/// aucune séquence active.
		void AbortSequence();

		/// True si une séquence est en cours.
		bool IsActiveSequence() const { return m_active; }

		/// Index de l'étape courante (0-based ; 0 si inactive).
		size_t CurrentIndex() const { return m_index; }

		/// Nombre total d'étapes de la séquence active (0 si inactive).
		size_t StepCount() const { return m_instructions.size(); }

		/// Instruction courante (valide seulement si IsActiveSequence()).
		const OverlayInstruction& CurrentInstruction() const { return m_instructions[m_index]; }

	private:
		std::vector<OverlayInstruction> m_instructions;
		size_t                          m_index   = 0;
		bool                            m_active  = false;
		OnCompleteCallback              m_onComplete;
	};
}
