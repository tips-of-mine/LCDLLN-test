// M100.49 — Définition code-déf’inie du tutoriel "first_launch" (8 étapes) +
// loader par id. Contenu identique à game/data/editor/tutorials/first_launch.json
// (loader JSON différé).

#include "src/world_editor/tutorial/TutorialIo.h"

namespace engine::editor::world::tutorial
{
	namespace
	{
		using engine::editor::world::help::OverlayInstruction;

		TutorialStep Step(const std::string& id, const std::string& title, const std::string& body,
			const std::string& icon, const std::string& target, const std::string& validatesOn)
		{
			TutorialStep s;
			s.id = id;
			s.instruction.targetWidget = target;
			s.instruction.titleFr = title;
			s.instruction.bodyFr = body;
			s.instruction.iconHint = icon;
			s.instruction.requiresAction = !validatesOn.empty() || !target.empty();
			s.instruction.validatesOnAction = validatesOn;
			return s;
		}
	}

	Tutorial BuildFirstLaunchTutorial()
	{
		Tutorial t;
		t.id = "first_launch";
		t.version = 1u;
		t.displayName = "Premier lancement";
		t.estimatedMinutes = 5u;

		t.introModal.titleFr = "Bienvenue dans l'éditeur LCDLLN";
		t.introModal.bodyFr = "Vous êtes ici pour la première fois. Voulez-vous suivre le "
			"tutoriel de 5 minutes qui vous guidera dans la création de votre première zone ?";
		t.introModal.bulletsFr = {
			"Créer une zone à partir d'un template",
			"Ajuster les paramètres de terrain",
			"Valider et exporter votre zone"
		};

		t.steps = {
			Step("step1_open_zone_preset", "Étape 1/8 — Ouvre le dialog",
				"Clique sur Fichier → Nouvelle zone depuis preset.", "👆",
				"menubar.file.new_from_preset", "zone_preset_dialog.opened"),
			Step("step2_select_forest", "Étape 2/8 — Choisis un preset",
				"Choisis la Forêt tempérée. C'est un bon point de départ.", "👆",
				"zone_preset_dialog.thumbnail.temperate_forest", "zone_preset_dialog.selected.temperate_forest"),
			Step("step3_create_zone", "Étape 3/8 — Crée la zone",
				"Laisse les sliders par défaut et clique Créer la zone.", "👆",
				"zone_preset_dialog.button.create", "zone.created"),
			Step("step4_select_cave_tool", "Étape 4/8 — Outil Cave",
				"Bravo ! Ajoutons un mesh insert : clique sur Cave dans la barre d'outils.", "👆",
				"toolbar.button.cave", "tool.cave.active"),
			Step("step5_select_cave_asset", "Étape 5/8 — Choisis un asset",
				"Sélectionne cave_small_01 dans le catalogue.", "👆",
				"panel.tool_properties.catalog_item.cave_small_01", "catalog.cave_small_01.selected"),
			Step("step6_place_cave", "Étape 6/8 — Pose la grotte",
				"Clique sur le terrain pour poser la grotte, dans une zone montagneuse.", "👆",
				"viewport", "mesh_insert.cave.placed"),
			Step("step7_validate", "Étape 7/8 — Valide ta zone",
				"Clique sur le bouton 🛡 Valider dans la barre d'outils.", "🛡",
				"toolbar.button.validate", "panel.validation.opened"),
			Step("step8_export_info", "Étape 8/8 — Exporter",
				"Si tu as 0 erreur (ou seulement des warnings), exporte via Fichier → Exporter. "
				"Le tutoriel s'arrête ici, tu sais l'essentiel !", "✓",
				"menubar.file.export", ""),
		};

		t.outroModal.titleFr = "Félicitations !";
		t.outroModal.bodyFr = "Tu sais maintenant l'essentiel. F1 ouvre la doc complète, et "
			"« Pourquoi ça ne marche pas ? » (menu Aide) t'aide à diagnostiquer les problèmes.";
		return t;
	}

	std::optional<Tutorial> LoadTutorialById(const std::string& id)
	{
		if (id == "first_launch")
			return BuildFirstLaunchTutorial();
		return std::nullopt;
	}
}
