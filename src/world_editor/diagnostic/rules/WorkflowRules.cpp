// M100.49 partie 2 — Implémentation des 10 règles de diagnostic workflow.
// Les libellés « one-click » sont PROPOSÉS (texte) ; l'action n'est pas exécutée
// ici (câblage UI + confirmation = passe séparée).

#include "src/world_editor/diagnostic/rules/WorkflowRules.h"

namespace engine::editor::world::diagnostic
{
	namespace
	{
		DiagnosticSuggestion Make(const char* id, SuggestionImportance imp,
			const std::string& title, const std::string& explanation,
			const std::string& actionLabel = "", const std::string& confirm = "")
		{
			DiagnosticSuggestion s;
			s.ruleId = id;
			s.importance = imp;
			s.titleFr = title;
			s.explanationFr = explanation;
			s.oneClickActionLabelFr = actionLabel;
			s.confirmationMessageFr = confirm;
			return s;
		}
	}

	void EmptyZoneActiveToolRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.hasActiveTool && ctx.chunkCount == 0)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Strong,
				"Outil actif mais aucune zone",
				"Tu as un outil sélectionné mais 0 chunk chargé. Crée d'abord une zone "
				"(Fichier → Nouvelle zone depuis preset).",
				"Ouvrir le dialog Zone Preset"));
		}
	}

	void NoActiveToolRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (!ctx.hasActiveTool)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Aucun outil sélectionné",
				"Aucun outil n'est actif. Clique sur un outil dans la barre d'outils pour commencer.",
				"Mettre la toolbar en évidence"));
		}
	}

	void ExportAttemptedWithErrorsRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.hasUserAttemptedExport && ctx.validationErrorCount > 0)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Critical,
				"Export bloqué par des erreurs",
				"Tu as essayé d'exporter mais ta zone a " + std::to_string(ctx.validationErrorCount) +
				" erreur(s). L'export est bloqué tant qu'elles ne sont pas corrigées.",
				"Ouvrir le panel Validation"));
		}
	}

	void UnsavedChangesLongTimeRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.commandsSinceLastSave > 30u)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Modifications non sauvegardées",
				"Tu as fait " + std::to_string(ctx.commandsSinceLastSave) +
				" modifications sans sauvegarder.",
				"Sauvegarder maintenant"));
		}
	}

	void RiversAfterErosionRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.erosionAppliedAfterRivers)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Strong,
				"Tes rivières flottent au-dessus du terrain",
				"Tu as appliqué une érosion hydraulique APRÈS avoir généré ton réseau de "
				"rivières. Les rivières suivent l'ancienne topographie. Re-lance le watershed.",
				"Re-lancer le watershed",
				"Re-lancer le watershed ? Cela va re-générer le réseau hydrographique, "
				"remplacer les rivières actuelles, et ajouter une commande annulable."));
		}
	}

	void ToolSelectedButNoActionRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.hasActiveTool && ctx.secondsSinceToolSelected > 120.0 && ctx.commandsSinceToolSelected == 0u)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Outil sélectionné mais inutilisé",
				"L'outil « " + ctx.activeToolId + " » est actif depuis plus de 2 minutes sans "
				"aucune action. Consulte la doc de l'outil (F1) si tu es bloqué."));
		}
	}

	void PresetJustAppliedNoSaveRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.presetJustAppliedNotSaved)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Preset appliqué non sauvegardé",
				"Tu viens d'appliquer un zone preset mais tu ne l'as pas encore sauvegardé.",
				"Sauvegarder"));
		}
	}

	void CavePlacedNoCamouflageRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.cavePlacedWithoutCamouflage)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Grotte sans camouflage de jointure",
				"Une grotte a été posée sans patch splat de camouflage : la jointure mesh ↔ "
				"terrain risque d'être visible.",
				"Ré-éditer pour ajouter le camouflage"));
		}
	}

	void SimpleModeAdvancedAttemptedRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.simpleModeActive && ctx.attemptedAdvancedFeature)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Fonctionnalité avancée en mode Simple",
				"Tu essaies d'accéder à un paramètre avancé alors que le mode Simple est actif.",
				"Basculer en mode Advanced"));
		}
	}

	void NoSeaLevelSetRule::Run(const DiagnosticContext& ctx, std::vector<DiagnosticSuggestion>& out) const
	{
		if (ctx.coastlineToolActive && !ctx.seaLevelSet)
		{
			out.push_back(Make(GetRuleId(), SuggestionImportance::Tip,
				"Aucun niveau de mer défini",
				"L'outil Coastline est actif mais aucun sea level n'est défini. La côte n'aura "
				"pas de référence d'altitude cohérente."));
		}
	}
}
