#pragma once

#include <cstdint>
#include <memory>

#include <vector>

#include <vulkan/vulkan_core.h>

#include "src/world_editor/ui/WorldMapEditDocument.h"
// Lot C vague 4 — sous-systèmes câblés dans l'éditeur monde :
//   - validation de zone (ZoneValidator) : registre + rapport trié par sévérité ;
//   - guidance overlay (OverlayGuidanceSystem + WidgetTargetRegistry) : fondation
//     du tutoriel interactif (rendu du voile/surlignage, aucune séquence lancée).
// Inclus en entier (pas en forward-decl) car ces membres sont des objets-valeur
// du `WorldEditorImGui` : leur taille doit être connue à la déclaration.
#include "src/world_editor/validation/ValidationRuleRegistry.h"
#include "src/world_editor/validation/ZoneValidator.h"
#include "src/world_editor/help/OverlayGuidanceSystem.h"
#include "src/world_editor/help/WidgetTargetRegistry.h"
// Lot C vague 4 — sous-système diagnostic (« Pourquoi ça ne marche pas ? ») :
// registre de règles workflow + moteur d'analyse. Inclus en entier (objets-valeur
// membres du `WorldEditorImGui`, taille requise à la déclaration).
#include "src/world_editor/diagnostic/DiagnosticRuleRegistry.h"
#include "src/world_editor/diagnostic/DiagnosticSystem.h"
#include "src/world_editor/diagnostic/IDiagnosticRule.h"
// Lot C vague 4 — assistant « Nouvelle zone » (QuickStartWizard) : machine d'état
// 5 étapes + résolveur de template. Inclus en entier (objets-valeur membres du
// `WorldEditorImGui`, taille requise à la déclaration). Le résolveur transforme
// les choix du wizard en `ZonePreset`, exécuté ensuite par le MÊME chemin que le
// `ZonePresetDialog` (ZonePresetExecutor + CommandStack du Shell).
#include "src/world_editor/wizard/QuickStartWizard.h"
#include "src/world_editor/wizard/WizardTemplateResolver.h"
#include "src/world_editor/zone_presets/ZonePresetExecutor.h" // ExecutionSummary (membre valeur)

namespace engine::core
{
	class Config;
}
namespace engine::platform
{
	class Window;
}
namespace engine::render
{
	class VkDeviceContext;
	class DayNightCycle;
}
namespace engine::render::terrain
{
	struct HeightmapData;
}
namespace engine::editor::world
{
	class WorldEditorShell;
}
namespace engine::editor::world::zone_presets
{
	class ZonePresetDialog;
}

namespace engine::editor
{
	class WorldEditorSession;
	class TexturePreviewCache;  // vignettes splatting (Task 12)

	/// Données pour dessiner grille + aperçu brosse par-dessus la vue 3D (avant \c ImGui::Render).
	struct WorldEditorViewportOverlayDesc
	{
		const float* viewProjColMajor = nullptr;
		/// Position / angles caméra (monde), remplis chaque frame — la grille utilise \c viewProjColMajor, pas ces champs.
		float cameraWorldX = 0.f;
		float cameraWorldY = 0.f;
		float cameraWorldZ = 0.f;
		float cameraYawDeg = 0.f;
		float cameraPitchDeg = 0.f;
		int viewportWidth = 0;
		int viewportHeight = 0;
		bool showGrid = false;
		float gridCellMeters = 8.f;
		float terrainOriginX = 0.f;
		float terrainOriginZ = 0.f;
		float terrainWorldSize = 1024.f;
		float heightScale = 200.f;
		const engine::render::terrain::HeightmapData* heightmap = nullptr;
		bool showBrushPreview = false;
		float brushWorldX = 0.f;
		float brushWorldZ = 0.f;
		float brushRadiusMeters = 10.f;
		/// Marqueurs debug instances (monde m). Nullptr = rien à dessiner.
		const std::vector<WorldMapEditLayoutInstance>* layoutInstancesOverlay = nullptr;
		int selectedLayoutInstanceOverlay = -1;
	};

	/// ImGui + Vulkan (rendu dynamique) pour \c lcdlln_world_editor.exe uniquement (Windows).
	/// Sur les autres plateformes, les appels sont des no-op.
	class WorldEditorImGui final
	{
	public:
		/// Constructeur out-of-line : la définition vit dans le `.cpp` où
		/// `ZonePresetDialog.h` est inclus en entier. Sans cela, le
		/// unique_ptr<ZonePresetDialog> ne peut pas instancier son cleanup
		/// d'exception (type incomplet vu depuis Engine.cpp).
		WorldEditorImGui();
		WorldEditorImGui(const WorldEditorImGui&) = delete;
		WorldEditorImGui& operator=(const WorldEditorImGui&) = delete;
		WorldEditorImGui(WorldEditorImGui&&) = delete;
		WorldEditorImGui& operator=(WorldEditorImGui&&) = delete;
		~WorldEditorImGui();

		/// Branche le `WorldEditorShell` propriétaire des 4 documents
		/// (terrain / water / mesh inserts / dungeon portals), du
		/// `CommandStack` et des 4 catalogs (caves, overhangs, arches,
		/// dungeons) — requis pour l'entrée menu Fichier > « Appliquer un
		/// preset de zone » (M100.46 incrément 3). Pointeur non possédé.
		/// Si nul, l'entrée menu est désactivée.
		void SetWorldEditorShell(engine::editor::world::WorldEditorShell* shell)
		{
			m_shell = shell;
		}

		/// \param hwndNative \c HWND sous Windows, sinon ignoré.
		/// \param cfg utilisé pour charger les polices TTF de l'UI auth (Windlass / Morpheus) dans
		/// l'atlas ImGui avant la création de la texture de fonts par ImGui_ImplVulkan_Init.
		/// \param isWorldEditorExe \c true pour \c lcdlln_world_editor.exe : la police par defaut
		/// devient Arial (lisible, neutre pour un editeur de carte) au lieu de Windlass (decorative,
		/// reservee a l'UI auth/in-game). Cf. \c editor.font.arial_path / \c editor.font.arial_pixel_height
		/// dans config.json. Aucun effet si \c false (UI auth garde Windlass).
		bool Init(VkInstance instance,
			const engine::render::VkDeviceContext& deviceContext,
			VkFormat swapchainFormat,
			uint32_t swapchainImageCount,
			uint32_t vulkanApiVersion,
			void* hwndNative,
			const engine::core::Config* cfg = nullptr,
			bool isWorldEditorExe = false);

		void Shutdown(VkDevice device);

		void OnSwapchainRecreate(uint32_t swapchainImageCount);

		/// À appeler chaque frame avant \ref RecordToBackbuffer (côté CPU).
		void NewFrame(float deltaSeconds, float displayWidth, float displayHeight);
		void BuildUi(const WorldEditorViewportOverlayDesc* viewportOverlay = nullptr);

		/// Contexte données éditeur (\c lcdlln_world_editor uniquement). Peut être nul.
		void SetEditorContext(WorldEditorSession* session, engine::core::Config* cfg);

		/// Branche le DayNightCycle pour que le panneau "Atmosphere" puisse modifier
		/// la time-of-day, le timeScale et l'ambient en live. Pointeur non possede.
		/// Nul si non branche -> panneau Atmosphere affiche un message d'attente.
		void SetDayNightCycle(engine::render::DayNightCycle* dayNight) { m_dayNight = dayNight; }

		/// Branche le cache de vignettes (possede par Engine). Pointeur non
		/// possede. Si nul, les vignettes sont rendues comme cellules grises
		/// (ImGui::Dummy 48x48) — l'UI reste fonctionnelle, juste sans previews.
		void SetTexturePreviewCache(engine::editor::TexturePreviewCache* cache) { m_texturePreviewCache = cache; }

		/// Win32 : branche \c ImGui_ImplWin32_WndProcHandler avant le traitement LCDLLN.
		void AttachPlatformWindow(void* hwndNative, engine::platform::Window& window);
		void DetachPlatformWindow(engine::platform::Window& window);

		[[nodiscard]] bool IsReady() const { return m_ready; }
		[[nodiscard]] bool WantsCaptureMouse() const;
		[[nodiscard]] bool WantsCaptureKeyboard() const;

		/// Image swapchain en \c TRANSFER_DST_OPTIMAL → présentation. Dessine ImGui par-dessus la scène copiée.
		bool RecordToBackbuffer(VkCommandBuffer cmd,
			VkImage backbufferImage,
			VkImageView backbufferView,
			VkExtent2D extent,
			const engine::render::VkDeviceContext& deviceContext);

	private:
		bool m_ready = false;
		void* m_hwnd = nullptr;
		WorldEditorSession* m_session = nullptr;
		engine::core::Config* m_cfg = nullptr;
		engine::render::DayNightCycle* m_dayNight = nullptr;
		engine::editor::TexturePreviewCache* m_texturePreviewCache = nullptr;
		engine::editor::world::WorldEditorShell* m_shell = nullptr;
		/// Dialog modal Zone Presets (M100.46 incrément 3). PIMPL via
		/// unique_ptr pour éviter d'exposer le header dans l'API publique
		/// du WorldEditorImGui (cycle Shell ↔ dialog).
		std::unique_ptr<engine::editor::world::zone_presets::ZonePresetDialog> m_zonePresetDialog;
		bool m_showTextureLibrary = false;  // pilote par le menu Affichage (Task 14)
		/// Flag de visibilité du panneau « Atmosphere » (cycle jour/nuit
		/// + couleurs ciel + ambient). Par défaut visible. Pilote par
		/// l'entrée menu `Vue > Atmosphere`. Sans ce toggle, le panneau
		/// ne peut pas être ré-ouvert une fois fermé via la croix du
		/// dock — d'où la régression signalée utilisateur après les fix
		/// dual-menu (#622) qui ont supprimé l'ancienne barre M100.1
		/// listant tous les panels.
		bool m_showAtmospherePanel = true;
		/// Fenêtre d'aide caméra (anciennement « Scene » inline, renommée
		/// en « Camera (aide) » dans #629). Masquée par défaut parce que :
		///   - elle a `NoMouseInputs` (pour laisser passer les clics vers
		///     le viewport 3D) — quand dockée dans un node avec d'autres
		///     tabs, ces flags se propagent au tab bar et empêchent le
		///     basculement entre onglets (bug confirmé utilisateur).
		///   - le `ScenePanel` du Shell (M100.34) prend en charge le
		///     viewport principal, l'aide texte n'est plus essentielle.
		/// Toggle via `Vue > Aide camera`. Quand affichée, elle flotte
		/// (pas de DockBuilderDockWindow associé).
		bool m_showCameraHelp = false;
		/// Flag traçant si une tentative de pose de la disposition par défaut (DockBuilder) a déjà
		/// été faite. Reset à false au démarrage et lors d'un « Réinitialiser la disposition »,
		/// repassé à true après la pose.
		bool m_defaultLayoutAttempted = false;
		/// Dimensions du dockspace au dernier frame BuildUi. Sert a detecter un
		/// resize de fenetre pour forcer DockBuilderSetNodeSize sur le node racine
		/// (sinon les panneaux dockes restent ancres a l'ancienne taille apres
		/// un drag de bord de fenetre, donnant une UI vide ou hors viewport).
		float m_lastDockSpaceWidth  = 0.0f;
		float m_lastDockSpaceHeight = 0.0f;

		// ── Lot C vague 4 : validation de zone (ZoneValidator) ─────────────────
		/// Registre des règles MVP (heightmap / splat / mesh inserts). Rempli une
		/// fois par `RegisterMvpValidationRules` au premier `BuildUi` (les règles
		/// sont sans état, l'enregistrement est idempotent côté éditeur grâce au
		/// flag `m_validationRegistered`).
		engine::editor::world::validation::ValidationRuleRegistry m_validationRegistry;
		/// Validateur référençant `m_validationRegistry` (non-owning) ; déclaré
		/// APRÈS le registre pour que sa durée de vie soit englobée (le validateur
		/// garde une référence const sur le registre).
		engine::editor::world::validation::ZoneValidator m_zoneValidator{ m_validationRegistry };
		/// True après le 1er `RegisterMvpValidationRules` — évite de réenregistrer
		/// les règles à chaque frame (le registre prend l'ownership et empilerait
		/// des doublons sinon).
		bool m_validationRegistered = false;
		/// Dernier rapport produit par « Valider la zone ». Conservé entre frames
		/// pour alimenter le panneau Validation et le gating de l'export runtime.
		engine::editor::world::validation::ZoneValidator::Report m_lastValidationReport;
		/// True dès qu'au moins une validation a été lancée (sinon le panneau
		/// affiche « aucune validation lancée » plutôt qu'un rapport vide trompeur).
		bool m_validationHasRun = false;
		/// Flag de visibilité du panneau « Validation » (toggle via menu Vue et
		/// ouvert automatiquement par le bouton « Valider la zone »).
		bool m_showValidationPanel = false;

		// ── Lot C vague 4 : guidance overlay (fondation tutoriel) ──────────────
		/// Moteur de séquence d'instructions (logique pure). Aucune séquence n'est
		/// lancée pour l'instant : `RenderGuidanceOverlay` ne dessine rien tant que
		/// `IsActiveSequence()` est faux. Fondation pour le tutoriel interactif.
		engine::editor::world::help::OverlayGuidanceSystem m_overlay;
		/// Registre id → rectangle écran des widgets-cibles, vidé en début de frame
		/// (`m_widgetTargets.Clear()`) et rerempli depuis les positions ImGui réelles
		/// après le rendu de chaque widget-cible.
		engine::editor::world::help::WidgetTargetRegistry m_widgetTargets;

		// ── Lot C vague 4 : diagnostic « Pourquoi ça ne marche pas ? » ─────────
		/// Registre des règles workflow MVP (10 règles, cf. RegisterMvpDiagnosticRules).
		/// Rempli une seule fois au premier `BuildUi` (les règles sont sans état,
		/// le registre prend l'ownership — réenregistrer empilerait des doublons,
		/// d'où le flag `m_diagnosticRegistered`).
		engine::editor::world::diagnostic::DiagnosticRuleRegistry m_diagnosticRegistry;
		/// Moteur d'analyse référençant `m_diagnosticRegistry` (non-owning) ; déclaré
		/// APRÈS le registre pour que sa durée de vie soit englobée (le système garde
		/// une référence const sur le registre).
		engine::editor::world::diagnostic::DiagnosticSystem m_diagnosticSystem{ m_diagnosticRegistry };
		/// True après le 1er `RegisterMvpDiagnosticRules` — évite de réenregistrer
		/// les règles à chaque frame.
		bool m_diagnosticRegistered = false;
		/// Flag de visibilité du panneau « Diagnostic » (toggle via menu Aide,
		/// ouvert aussi par le bouton « Analyser »).
		bool m_showDiagnosticPanel = false;
		/// Dernier rapport produit par « Analyser ». Conservé entre frames pour
		/// que le panneau affiche les suggestions sans réanalyser à chaque frame
		/// (l'analyse n'a lieu qu'au clic sur « Analyser »).
		engine::editor::world::diagnostic::DiagnosticSystem::Report m_lastDiagnosticReport;
		/// True dès qu'au moins une analyse a été lancée (sinon le panneau affiche
		/// « aucune analyse lancée » plutôt qu'un rapport vide trompeur).
		bool m_diagnosticHasRun = false;

		// ── Lot C vague 4 : assistant « Nouvelle zone » (QuickStartWizard) ─────
		/// Machine d'état des 5 étapes guidées (Climat / Relief / Côte / POI /
		/// Aperçu). Logique pure, sans dépendance ImGui : l'UI ci-dessous ne fait
		/// que la piloter (SetChoiceForCurrentStep, Next/Prev, SetSeed).
		engine::editor::world::wizard::QuickStartWizard m_wizard;
		/// Résolveur choix → `ZonePreset`. Sans état : instancié une fois comme
		/// membre valeur, `Resolve` appelé au moment de la génération.
		engine::editor::world::wizard::WizardTemplateResolver m_wizardResolver;
		/// Flag de visibilité de la fenêtre wizard (ouvert par Fichier > « Nouvelle
		/// zone (assistant)... », fermé par le bouton Fermer/Annuler ou la croix).
		bool m_showWizard = false;
		/// True quand la modale de confirmation de génération est demandée : posée
		/// par le bouton « Générer » de l'étape Aperçu, consommée une seule fois par
		/// `RenderWizardWindow` qui pousse l'`ImGui::OpenPopup` correspondant.
		bool m_wizardConfirmRequested = false;
		/// Champ seed édité dans l'UI (étape Aperçu). Recopié dans le wizard via
		/// `SetSeed` à chaque modification — `int` car `ImGui::InputInt` l'exige.
		int m_wizardSeed = 42;
		/// Résumé de la dernière génération via le wizard (commandes poussées /
		/// ignorées / échecs). `totalSteps == 0` → aucune génération encore lancée.
		engine::editor::world::zone_presets::ExecutionSummary m_wizardLastSummary{};
		/// True dès qu'une génération a été lancée depuis le wizard (alimente le
		/// bandeau de résumé en bas de l'étape Aperçu).
		bool m_wizardHasGenerated = false;
		/// Id du preset effectivement résolu+exécuté (affiché dans le résumé).
		std::string m_wizardLastPresetId;

		/// Lot C vague 4 — Construit un `ValidationContext` (vues lecture seule)
		/// depuis les documents du shell branché puis exécute `m_zoneValidator`,
		/// stockant le résultat dans `m_lastValidationReport`. No-op si `m_shell`
		/// ou `m_cfg` est nul (les chunks terrain ont besoin de la config pour le
		/// chargement disque). Effet de bord : peut charger des chunks terrain en
		/// RAM via `TerrainDocument::EnsureLoaded` ; met `m_validationHasRun` à
		/// true et ouvre le panneau Validation.
		/// Contrainte thread : main thread (accès documents + ImGui state ensuite).
		void RunZoneValidation();

		/// Lot C vague 4 — Rend le panneau ImGui « Validation » : compteurs
		/// erreurs / warnings / hints + liste des problèmes triés par sévérité.
		/// Un clic sur un problème logue sa position monde (le recentrage caméra
		/// complet est différé — l'API caméra n'est pas exposée ici). No-op si
		/// `m_showValidationPanel` est faux. Effet de bord : ImGui state, LOG_INFO
		/// au clic. Doit être appelée pendant la frame UI (entre NewFrame et Render).
		void RenderValidationPanel();

		/// Lot C vague 4 — Si une séquence de guidance est active
		/// (`m_overlay.IsActiveSequence()`), dessine via le foreground draw list :
		/// un voile semi-transparent plein écran, un rectangle de surlignage autour
		/// du widget-cible (rect lu dans `m_widgetTargets`), et une bulle
		/// titre/texte. No-op (ne dessine RIEN) quand aucune séquence n'est active.
		/// À appeler en fin de frame UI, juste avant `ImGui::Render`.
		/// Effet de bord : ImGui foreground draw list uniquement.
		void RenderGuidanceOverlay();

		/// Lot C vague 4 — Construit un `DiagnosticContext` (état d'USAGE courant)
		/// depuis l'état runtime accessible via `m_shell` (outil actif, nombre de
		/// chunks, profondeur undo) + le dernier rapport de validation. Lecture
		/// seule : ne modifie aucun document (pas de chargement disque, contrairement
		/// à `RunZoneValidation`). Les champs sans source de suivi fiable (timing,
		/// pièges d'usage spécifiques, mode Simple) gardent leur défaut neutre — voir
		/// commentaires inline (« à instrumenter en 2e passe »). Retourne un contexte
		/// vide (tous champs défaut) si `m_shell` est nul.
		/// Compilée CROSS-PLATFORM (hors garde `_WIN32`, comme `RunZoneValidation`) :
		/// tous les types y sont pleinement qualifiés pour le build Linux.
		/// Contrainte thread : main thread (accès documents du shell).
		engine::editor::world::diagnostic::DiagnosticContext BuildDiagnosticContext() const;

		/// Lot C vague 4 — Rend le panneau ImGui « Diagnostic » (« Pourquoi ça ne
		/// marche pas ? ») : bouton « Analyser » (appelle `m_diagnosticSystem.Analyze`
		/// sur `BuildDiagnosticContext()` et stocke le résultat), puis liste les
		/// suggestions triées par importance (Critical → Tip) avec leur libellé
		/// d'action PROPOSÉE — affichage seul, AUCUNE exécution one-click. No-op si
		/// `m_showDiagnosticPanel` est faux. Effet de bord : ImGui state, met
		/// `m_lastDiagnosticReport`/`m_diagnosticHasRun` à jour au clic « Analyser ».
		/// Doit être appelée pendant la frame UI (entre NewFrame et Render).
		void RenderDiagnosticPanel();

		/// Lot C vague 4 — Rend la fenêtre de l'assistant « Nouvelle zone »
		/// (5 étapes : Climat / Relief / Côte / POI / Aperçu). Cartes de choix
		/// pilotant `m_wizard.SetChoiceForCurrentStep`, navigation Précédent/
		/// Suivant gardée par `CanProceed`, champ seed (`SetSeed`). À l'étape
		/// Aperçu, le bouton « Générer » (actif seulement si `IsReadyToGenerate()`)
		/// demande la modale de confirmation. No-op si `m_showWizard` est faux.
		/// Effet de bord : ImGui state ; peut déclencher `RunWizardGeneration` à la
		/// confirmation. À appeler pendant la frame UI (entre NewFrame et Render).
		/// Contrainte thread : main thread (ImGui + accès documents au moment de
		/// la génération).
		void RenderWizardWindow();

		/// Lot C vague 4 — Résout les choix du wizard en `ZonePreset`
		/// (`m_wizardResolver.Resolve`) puis l'exécute par le MÊME chemin que le
		/// `ZonePresetDialog` : construction du `DispatchContext` depuis les refs
		/// du Shell, `ZonePresetExecutor::Execute` sur le `CommandStack` du Shell.
		/// DESTRUCTIF : l'executor vide d'abord les 4 documents de la zone
		/// (`ResetEditedZoneDocuments`) — irréversible, c'est le P0 connu partagé
		/// avec le dialog (cf. note `///` dans le .cpp). Renseigne
		/// `m_wizardLastSummary`/`m_wizardLastPresetId`/`m_wizardHasGenerated`.
		/// No-op si `m_shell` est nul. Effet de bord : vide+repeuple les documents
		/// terrain/water/mesh/donjon, pousse des commandes sur le CommandStack.
		/// Compilée CROSS-PLATFORM (hors garde `_WIN32`, comme `RunZoneValidation`)
		/// : tous les types y sont pleinement qualifiés pour le build Linux.
		/// Contrainte thread : main thread (accès documents non thread-safe).
		void RunWizardGeneration();

#if defined(_WIN32)
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
#endif
	};

} // namespace engine::editor
