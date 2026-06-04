#pragma once

// M100.49 partie 2 — Système de diagnostic « Pourquoi ça ne marche pas ? ».
// Contrairement à la validation (M100.48, cohérence des DONNÉES), le diagnostic
// analyse la cohérence d'USAGE / workflow et propose des suggestions
// actionnables. Architecture jumelle de IValidationRule.
//
// Le diagnostic PROPOSE, ne décide pas : les suggestions portent un libellé
// d'action « one-click » et un message de confirmation, mais n'EXÉCUTENT
// jamais rien ici (le câblage de l'action effective + la modale de confirmation
// sont une passe UI séparée). Le cœur (règles + système) est pur et testable.

#include <cstdint>
#include <string>
#include <vector>

#include "src/world_editor/validation/ValidationTypes.h"

namespace engine::editor::world::diagnostic
{
	/// Importance d'une suggestion de diagnostic.
	enum class SuggestionImportance : uint8_t
	{
		Tip      = 0, ///< Astuce / best-practice.
		Strong   = 1, ///< Forte suspicion que c'est la cause.
		Critical = 2, ///< Bloquant ou cause certaine.
	};

	/// Une suggestion émise par une règle de diagnostic.
	struct DiagnosticSuggestion
	{
		std::string          ruleId;
		std::string          titleFr;
		std::string          explanationFr;
		SuggestionImportance importance = SuggestionImportance::Tip;
		std::string          docSectionId;          ///< Lien doc M100.47 (optionnel).
		/// Action « one-click » PROPOSÉE (libellé + confirmation). Non exécutée
		/// ici : l'UI branche l'action réelle derrière une modale de confirmation.
		std::string          oneClickActionLabelFr; ///< Vide = pas d'action proposée.
		std::string          confirmationMessageFr;
	};

	/// Contexte d'analyse : état d'usage courant de l'éditeur (champs simples,
	/// injectés par le shell) + accès optionnel aux données validées de M100.48.
	struct DiagnosticContext
	{
		// --- État outil / dialogues ---
		bool        hasActiveTool         = false;
		std::string activeToolId;                 ///< Ex. "cave", "coastline", "hydraulic".
		bool        hasOpenedDialog       = false;
		double      secondsSinceToolSelected = 0.0;
		uint32_t    commandsSinceToolSelected = 0;

		// --- État zone / chunks ---
		uint32_t    chunkCount            = 0;
		bool        presetJustAppliedNotSaved = false;

		// --- Sauvegarde ---
		uint32_t    commandsSinceLastSave = 0;

		// --- Export / validation (peuplé depuis un ZoneValidator::Report) ---
		bool        hasUserAttemptedExport = false;
		uint32_t    validationErrorCount   = 0;

		// --- Pièges d'usage spécifiques ---
		bool        erosionAppliedAfterRivers = false; ///< Hydraulic après RiverNetwork.
		bool        cavePlacedWithoutCamouflage = false;
		bool        coastlineToolActive   = false;
		bool        seaLevelSet           = false;
		bool        simpleModeActive      = false;
		bool        attemptedAdvancedFeature = false;

		// --- Historique récent (10 dernières commandes) ---
		std::vector<std::string> recentCommandHistory;

		/// Accès optionnel aux données de zone validées (M100.48). Peut être null ;
		/// les règles MVP s'appuient sur les champs simples ci-dessus.
		const engine::editor::world::validation::ValidationContext* validation = nullptr;
	};

	/// Règle de diagnostic : analyse le contexte et ajoute des suggestions.
	class IDiagnosticRule
	{
	public:
		virtual ~IDiagnosticRule() = default;
		virtual const char* GetRuleId() const = 0;
		virtual void Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const = 0;
	};
}
