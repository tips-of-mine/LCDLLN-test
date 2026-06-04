#pragma once

// M100.49 — Structures de données d'un tutoriel interactif. Pures (aucune
// dépendance ImGui/rendu) : le moteur OverlayGuidanceSystem les consomme et le
// rendu visuel (surlignage, bulle) est une passe UI séparée (Windows).

#include <string>
#include <vector>

namespace engine::editor::world::help
{
	/// Identifiant stable d'un widget cible de l'UI éditeur.
	/// Convention : <panel>.<sous-élément>.<id> (ex. "menubar.file.new_from_preset").
	using WidgetTargetId = std::string;

	/// Une instruction overlay : quoi surligner + texte + condition de validation.
	struct OverlayInstruction
	{
		WidgetTargetId targetWidget;       ///< Widget à surligner (vide = aucun).
		std::string    titleFr;
		std::string    bodyFr;
		std::string    iconHint;           ///< "👆", "✓", "⚠"…
		bool           requiresAction = false; ///< Si true, on attend l'action utilisateur.
		WidgetTargetId validatesOnAction;  ///< Action attendue (ex. "zone_preset_dialog.opened").
	};
}

namespace engine::editor::world::tutorial
{
	/// Une étape de tutoriel (= une instruction overlay + un id stable).
	struct TutorialStep
	{
		std::string                                  id;
		engine::editor::world::help::OverlayInstruction instruction;
	};

	/// Modal d'accueil / de fin.
	struct TutorialModal
	{
		std::string              titleFr;
		std::string              bodyFr;
		std::vector<std::string> bulletsFr;
	};

	/// Un tutoriel complet (data-driven : code ou JSON).
	struct Tutorial
	{
		std::string               id;
		uint32_t                  version = 1u;
		std::string               displayName;
		uint32_t                  estimatedMinutes = 5u;
		TutorialModal             introModal;
		std::vector<TutorialStep> steps;
		TutorialModal             outroModal;
	};

	/// Construit le tutoriel MVP "first_launch" (8 étapes) en code. Le fichier
	/// `game/data/editor/tutorials/first_launch.json` documente le même contenu ;
	/// le chargement JSON via TutorialIo est différé (2e passe).
	Tutorial BuildFirstLaunchTutorial();
}
