# Réorganisation menus / barre d'actions / palette d'outils — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Réorganiser l'UI de l'éditeur monde sur les conventions UE : registre
d'actions central, menus Fichier/Édition/Vue/Fenêtre/Outils/Aide, barre
d'actions en haut, palette d'outils à gauche, statut enrichi, palette de
commandes Ctrl+P, fenêtre raccourcis. Spec :
`docs/superpowers/specs/2026-07-17-editor-menus-toolbar-reorg-design.md`.

**Architecture :** Un `EditorActionRegistry` (pure data + std::function) vit
dans `WorldEditorShell` ; le shell y enregistre ses actions autonomes
(undo/redo, toggles panneaux, outils), `WorldEditorImGui` y ajoute les actions
session (save/load/export/import/quit). Menus, barre d'actions, palette Ctrl+P
et fenêtre raccourcis consomment le registre. La logique testable (registre,
groupes palette, filtre, récents) est séparée d'ImGui pour tourner sous ctest
Linux.

**Tech stack :** C++20, ImGui docking, CMake, tests maison REQUIRE + main
(pattern `EditorToolbarTests.cpp`).

## Global Constraints

- Commentaires en **français** ; toute fonction ajoutée/modifiée dans
  `src/world_editor/` documentée en `///` (rôle, params non évidents, effets
  de bord, contraintes thread) — règle stricte CLAUDE.md.
- **PascalCase** pour nouveaux fichiers/classes ; membres `m_camelCase`,
  constantes `kPascalCase`.
- **Pas de toolchain locale** : aucune compilation locale possible. Le cycle
  TDD est : écrire test + impl → push → CI (build-windows compile,
  build-linux compile + ctest). Ne jamais prétendre « tests passés » avant CI
  verte.
- Nouveaux tests **non gatés WIN32** quand ils n'incluent pas
  `WorldEditorShell.h`/ImGui (sinon ils ne tournent jamais : build-windows
  n'exécute pas ctest). Enregistrer les cibles test dans le
  **CMakeLists.txt racine** (vérifier aussi `src/CMakeLists.txt` — piège
  doublon CMP0002).
- `git add` **ciblé uniquement** (jamais `-A` : le worktree contient des .fbx
  modifiés et `dev/` non suivis qui ne sont pas à nous).
- Ne pas toucher `legacy/`.
- 3 PRs empilées : PR1 = branche courante
  `claude/unreal-engine-editor-comparison-d5280e` → main ; PR2 =
  `claude/editor-ui-reorg-pr2` (base PR1) ; PR3 =
  `claude/editor-ui-reorg-pr3` (base PR2). Indiquer l'ordre de merge.
- Déploiement : ✅ client/éditeur uniquement (à rappeler dans chaque PR).

---

## PR 1 — Registre d'actions + menus + statut

### Task 1 : `EditorAction` + `EditorActionRegistry` + tests

**Files:**
- Create: `src/world_editor/actions/EditorAction.h`
- Create: `src/world_editor/actions/EditorActionRegistry.h`
- Create: `src/world_editor/actions/EditorActionRegistry.cpp`
- Test: `src/world_editor/tests/EditorActionRegistryTests.cpp`
- Modify: `CMakeLists.txt` (bloc tests, après `editor_toolbar_tests`)

**Interfaces (produit) :**

```cpp
// EditorAction.h — AUCUN include ImGui/Shell (testable Linux).
namespace engine::editor::world::actions
{
	/// Catégorie d'une action — pilote le groupement dans les menus, la
	/// palette de commandes et la fenêtre raccourcis.
	enum class ActionCategory : uint8_t
	{ Fichier = 0, Edition, Vue, Fenetre, Outils, Aide };

	/// Déclaration unique d'une action de l'éditeur (menu, barre d'actions,
	/// palette Ctrl+P, fenêtre raccourcis consomment la même entrée).
	struct EditorAction
	{
		std::string id;            ///< stable, kebab-case, ex. "file.save"
		std::string label;         ///< libellé FR affiché
		ActionCategory category = ActionCategory::Fichier;
		std::string section;       ///< en-tête de section optionnel (ex. "Import")
		std::string shortcutText;  ///< texte affiché ("Ctrl+S") ; exécution ailleurs
		std::function<bool()> enabled;  ///< nul => toujours actif
		std::function<bool()> checked;  ///< nul => pas un toggle
		std::function<void()> execute;  ///< nul => item inerte (interdit hors tests)
	};

	class EditorActionRegistry
	{
	public:
		/// Enregistre une action. \return false (et log) si id dupliqué/vide.
		bool Register(EditorAction action);
		const std::vector<EditorAction>& Actions() const;
		/// nullptr si absent.
		const EditorAction* Find(std::string_view id) const;
		bool IsEnabled(const EditorAction& a) const;  // a.enabled ? a.enabled() : true
		size_t Size() const;
		void Clear();  ///< tests uniquement
	};
}
```

- [ ] **Step 1 : test échouant** — `EditorActionRegistryTests.cpp` (pattern
  REQUIRE + main) : Register ok, id dupliqué refusé, id vide refusé, Find,
  IsEnabled avec/sans prédicat, checked toggle, ordre d'insertion préservé.
- [ ] **Step 2 : impl minimale** (`std::vector` + lookup linéaire — <100
  actions, pas de map nécessaire).
- [ ] **Step 3 : CMake** — cible `editor_action_registry_tests` NON gatée
  WIN32 (le module n'inclut ni ImGui ni le shell), lien `engine_core`,
  `add_test`. Vérifier absence de doublon dans `src/CMakeLists.txt`.
- [ ] **Step 4 : commit** `feat(editor): registre d'actions central (PR1 1/6)`.

### Task 2 : `CommandStack::Serial()` + dirty-depuis-save du shell

**Files:**
- Modify: `src/world_editor/core/CommandStack.h/.cpp` — membre
  `uint64_t m_serial = 0;` incrémenté dans `Push` (même en cas de merge),
  `Undo`, `Redo`, `Clear` ; accesseur `uint64_t Serial() const`.
- Modify: `src/world_editor/core/WorldEditorShell.h` —
  `void NoteSaved()` (m_savedSerial = serial courant ; m_dirty = false) ;
  `bool IsDirtySinceSave() const` (serial != m_savedSerial || m_dirty).
- Test: étendre `EditorActionRegistryTests.cpp`… **non** : CommandStack.h est
  inclus sans ImGui → test dans le même binaire
  `editor_action_registry_tests` (section dédiée) pour éviter une cible de
  plus : Push/Undo/Redo/Clear incrémentent, NoteSaved/IsDirtySinceSave via un
  `WorldEditorShell`… **le shell tire tout l'éditeur** → tester
  NoteSaved/IsDirtySinceSave dans `editor_toolbar_tests` (déjà gaté WIN32,
  linke le shell). Serial() testé côté Linux avec une commande stub.
- [ ] Steps : test serial (stub ICommand) → impl → test shell dirty dans
  EditorToolbarTests → commit
  `feat(editor): serial CommandStack + dirty-depuis-save (PR1 2/6)`.

### Task 3 : cartes récentes dans `UserPrefs`

**Files:**
- Modify: `src/world_editor/prefs/UserPrefs.h` —
  `std::vector<std::string> recentMapIds;` (+ doc).
- Modify: `src/world_editor/prefs/UserPrefsStore.h/.cpp` —
  `void PushRecentMap(const std::string& zoneId)` : dédoublonne, insère en
  tête, tronque à `kMaxRecentMaps = 8`, persiste ;
  `const std::vector<std::string>& GetRecentMaps() const`.
  Sérialisation JSON symétrique (champ `recentMapIds`, lecture tolérante).
- Test: `src/world_editor/tests/UserPrefsRecentMapsTests.cpp` (non gaté si
  UserPrefsStore compile hors WIN32 — il n'inclut qu'EditorMode + filesystem ;
  sinon suivre le gating du fichier existant). Cap 8, dédoublonnage remonte
  en tête, ordre, round-trip disque via répertoire temporaire +
  `ResetForTesting`.
- [ ] Steps : test → impl → CMake → commit
  `feat(editor): cartes recentes persistees (PR1 3/6)`.

### Task 4 : réorganisation des menus dans `WorldEditorImGui::BuildUi`

**Files:**
- Modify: `src/world_editor/ui/WorldEditorImGui.h` : membres
  `bool m_showPreferencesWindow = false; bool m_showAboutWindow = false;
  bool m_quitConfirmOpen = false; bool m_actionsRegistered = false;
  std::function<void()> m_onQuitRequested;` + setter
  `void SetQuitCallback(std::function<void()> cb)` + méthodes privées
  `RegisterEditorActions(); RenderMenuBarFr(); RenderPreferencesWindow();
  RenderAboutWindow(); RenderQuitConfirmModal(); MenuItemForAction(const char* id);`
- Modify: `src/world_editor/ui/WorldEditorImGui.cpp` : le bloc
  `BeginMainMenuBar` (~849-1120) est remplacé par `RenderMenuBarFr()`.
- Modify: `src/world_editor/core/WorldEditorShell.h/.cpp` :
  `actions::EditorActionRegistry& MutableActionRegistry()` (membre
  `m_actions`) ; le shell enregistre à `Init` : `edit.undo`, `edit.redo`,
  `edit.history` (SetVisible(true) du panneau History), `window.panel.<name>`
  pour chaque panneau (checked = IsVisible), `window.layout.reset`.
- `WorldEditorImGui::RegisterEditorActions()` (1er BuildUi, garde
  `m_actionsRegistered`) ajoute : `file.new-zone-wizard`, `file.zone-preset`,
  `file.save` (Ctrl+S ; après succès `m_shell->NoteSaved()` +
  `UserPrefsStore::PushRecentMap`), `file.load.<n>` n'est PAS une action (le
  sous-menu Charger reste itératif), `file.import.texture`,
  `file.import.audio`, `zone.validate`, `zone.export` (enabled = gating
  validation existant), `file.quit` (voir modale), `view.grid` (checked),
  `view.camera-help`, `view.atmosphere`, `window.texture-library`,
  `window.validation-panel`, `edit.preferences`, `help.diagnostic`,
  `help.about`, `edit.select.delete` (enabled = sélection non vide ; pousse
  la `DeleteCommand` existante — reprendre le call-site actuel de suppression
  de l'OutlinerPanel/SelectionTool), et les 15 outils
  `tool.<kebab>` (execute = SetActiveTool, checked = GetActiveTool()==t,
  shortcutText = raccourci Ctrl+Shift existant, section = famille).
- Structure des menus (ordre exact, `ImGui::SeparatorText` pour les
  en-têtes) :

```text
Fichier : [NOUVEAU] file.new-zone-wizard, file.zone-preset
          [OUVRIR] Charger une carte ▸ (itératif : ActionLoadMapByZoneId +
                   PushRecentMap + Rafraîchir) ; Cartes récentes ▸
                   (GetRecentMaps ; item grisé si plus dans AvailableMapIds)
          [ENREGISTRER] file.save
          [IMPORT] file.import.texture, file.import.audio
          [EXPORT] zone.validate, zone.export (tooltip blocage conservé)
          ── file.quit
Édition : edit.undo, edit.redo, edit.history ── edit.select.delete
          ── edit.preferences, (PR3: edit.shortcuts)
Vue     : view.grid, view.camera-help, view.atmosphere
Fenêtre : window.panel.* (boucle panneaux, skip "Scene"),
          window.texture-library, window.validation-panel
          ── window.layout.reset (comportement DockBuilderRemoveNode actuel)
Outils  : sous-menus Terrain ▸ / Eau ▸ / Macro ▸ / Structures ▸
          (itère registre catégorie Outils groupé par section)
Aide    : help.diagnostic, (PR3: help.shortcuts), help.about
```

- `MenuItemForAction(id)` : lookup ; `ImGui::MenuItem(label, shortcut,
  checkedVal, enabled)` ; toggle → execute inverse le flag via checked ;
  conserve l'enregistrement `m_widgetTargets` pour `menubar.file.export_runtime`
  et `toolbar.button.validate` (guidance overlay).
- Disparaissent : items top-level Sauvegarder/Charger, statut dans la barre,
  menu Options (fusionné dans Préférences), slider vitesse caméra et astuces
  du menu Vue (déplacés Préférences/tooltips).
- `RenderPreferencesWindow()` : fenêtre « Préférences » (toggle) — layout
  clavier QWERTY/AZERTY (logique actuelle
  `TryPersistMovementLayoutToUserSettings` conservée), slider
  `controls.editor_camera_speed_multiplier` (0.25–5.0 + texte d'aide), mode
  éditeur Simple/Avancé (EditorModeRegistry).
- `RenderQuitConfirmModal()` : `file.quit` → si
  `m_shell && m_shell->IsDirtySinceSave()` ouvre popup modale « Quitter
  l'éditeur » (3 boutons : Sauvegarder et quitter / Quitter sans sauvegarder /
  Annuler) sinon appelle direct `m_onQuitRequested`.
- `RenderAboutWindow()` : version (constante `kWorldEditorVersionLabel`
  = "LCDLLN World Editor — build dev"), chemin spec/manuel.
- Modify: `src/client/app/Engine.cpp` : au câblage worldEditorExe existant
  (`SetWorldEditorShell` / `SetEditorContext`), ajouter
  `m_worldEditorImGui.SetQuitCallback([this]{ OnQuit(); });`
- [ ] Steps : impl (pas de test ImGui possible — la logique testable est déjà
  couverte Tasks 1-3) → relecture manuelle du diff → commit
  `feat(editor): menus reorganises Fichier/Edition/Vue/Fenetre/Outils/Aide (PR1 4/6)`.

### Task 5 : barre de statut enrichie

**Files:**
- Modify: `WorldEditorImGui.cpp` fenêtre `"Statut"` (~1962) : gauche =
  `Status()` ; ` | Carte : <zoneId ou aucune>` ; ` | Outil : <libellé FR>` ;
  droite (SameLine + alignement à `GetContentRegionAvail`) =
  « Tout enregistré » (gris) ou « ● Non sauvegardé » (ambre) selon
  `m_shell->IsDirtySinceSave()`. Libellé outil : réutilise
  `ToolbarIconAtlas::Get(tool).tooltipFr`.
- [ ] Steps : impl → commit `feat(editor): barre de statut enrichie (PR1 5/6)`.

### Task 6 : dégraissage barre anglaise fantôme + PR

**Files:**
- Modify: `src/world_editor/core/WorldEditorShell.cpp` `RenderMenuBar` :
  supprimer menu File (stubs no-op), menu Tools (vide), menu Window (4
  layouts aliasés) ; garder Edit/View/Help minimal comme fallback du mode
  shell-sans-UI-française (le binaire éditeur la supprime déjà via
  `SetMenuBarSuppressed`). Mettre à jour la doc `///`.
- [ ] Step : commit `chore(editor): degraisse la barre M100.1 fantome (PR1 6/6)`.
- [ ] **PR 1** : push branche courante ; `gh pr create` base main. Corps :
  résumé + « **Déploiement** : ✅ client uniquement ». Vérifier CI.

## PR 2 — Barre d'actions + palette d'outils (branche `claude/editor-ui-reorg-pr2`)

### Task 7 : extraction `ActiveTool` + modèle de palette pur + tests

**Files:**
- Create: `src/world_editor/core/ActiveTool.h` (déplacement verbatim de
  l'enum depuis `WorldEditorShell.h`, qui l'inclut désormais — aucun autre
  call-site à toucher).
- Create: `src/world_editor/ui/ToolPaletteModel.h/.cpp` :

```cpp
/// Groupe d'outils affiché par la palette (pure data, testable sans ImGui).
struct ToolPaletteGroup { const char* titleFr; std::vector<ActiveTool> tools; };
/// Les 4 familles (Terrain / Eau / Macro / Structures) couvrant exactement
/// les 15 outils, ordre d'affichage stable.
const std::vector<ToolPaletteGroup>& GetToolPaletteGroups();
/// Libellé FR court d'un outil (source unique pour palette/menu/statut).
const char* ToolLabelFr(ActiveTool t);
```

- Test: `src/world_editor/tests/ToolPaletteModelTests.cpp` NON gaté WIN32 :
  chaque valeur 1..15 de l'enum apparaît exactement une fois, groupes non
  vides, labels non vides et uniques, `ToolLabelFr(None)` défini.
- [ ] Steps : test → impl → CMake → commit.

### Task 8 : `EditorToolbar` reconverti en barre d'actions

**Files:**
- Modify: `src/world_editor/ui/EditorToolbar.h/.cpp` :
  `ToolbarButtonRect.tool` remplacé par `std::string actionId;` ; la liste
  ordonnée devient `{"file.save","edit.undo","edit.redo","zone.validate",
  "zone.export"}` ; séparateur visuel entre groupes (gap double) ;
  `HandleClick` → `registry.Find(id)->execute()` si enabled ; bouton grisé
  sinon ; tooltip = `label (shortcutText)`. Le constructeur prend
  `WorldEditorShell&` (inchangé) et lit `shell.MutableActionRegistry()`.
- Modify: `src/world_editor/ui/ToolbarIconAtlas.h/.cpp` : ajoute
  `static ToolIconStyle GetForAction(std::string_view actionId);` (lettres
  S/Z/Y/V/E, couleurs distinctes, tooltip FR).
- Test: `EditorToolbarTests.cpp` réécrit : invariant géométrique conservé
  (aucun bouton sous la bande), clic exécute l'action (action stub registrée
  dans le test qui flippe un bool), clic sur action disabled = no-op, action
  inconnue = bouton absent (layout l'omet).
- [ ] Steps : test → impl → commit.

### Task 9 : `ToolPalettePanel` + disposition + nettoyage

**Files:**
- Create: `src/world_editor/panels/ToolPalettePanel.h/.cpp` : IPanel, nom
  fenêtre `"Palette d'outils"` ; en tête bouton « Aucun outil » ; par groupe
  `CollapsingHeader(DefaultOpen)` + un bouton par outil (`Selectable` avec
  pastille couleur atlas + `ToolLabelFr`), surligné si actif, tooltip
  raccourci ; clic → `m_shell.SetActiveTool`.
- Modify: `WorldEditorShell.cpp Init` : append du panneau en FIN de
  `m_panels` (les indices F1..F12 restent stables).
- Modify: `WorldEditorImGui.cpp` bloc DockBuilder : `DockBuilderDockWindow
  ("Palette d'outils", idLeft)` ; texte du panneau « Outils » mis à jour
  (« Sélection d'outil : palette à gauche, menu Outils ou Ctrl+Shift+… »).
- [ ] Steps : impl → commit → **PR 2** (base PR1, corps + déploiement ✅,
  ordre de merge 1→2→3).

## PR 3 — Palette de commandes + fenêtre raccourcis (branche `claude/editor-ui-reorg-pr3`)

### Task 10 : filtre pur + tests

**Files:**
- Create: `src/world_editor/ui/CommandPaletteModel.h/.cpp` :

```cpp
/// Entrée candidate (id + libellé + catégorie déjà résolus en texte).
struct PaletteEntry { std::string id, label, categoryFr, shortcutText; bool enabled; };
/// Filtre insensible casse/accents ; préfixe de mot > sous-chaîne ; requête
/// vide => tout dans l'ordre d'origine. Retourne les indices ordonnés.
std::vector<size_t> FilterPaletteEntries(std::string_view query,
	const std::vector<PaletteEntry>& entries);
/// Normalisation : minuscules ASCII + translittération accents FR courants.
std::string NormalizeForSearch(std::string_view text);
```

- Test: `CommandPaletteModelTests.cpp` NON gaté : accents (é→e), casse,
  préfixe classé avant sous-chaîne, requête vide, aucun match, stabilité.
- [ ] Steps : test → impl → CMake → commit.

### Task 11 : fenêtre palette Ctrl+P + fenêtre Raccourcis

**Files:**
- Modify: `WorldEditorImGui.h/.cpp` : `m_showCommandPalette`,
  `m_showShortcutsWindow`, `m_paletteQuery[128]`, `m_paletteSelected` ;
  détection `ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)` dans
  BuildUi (hors capture texte) ; fenêtre centrée : InputText auto-focus,
  liste filtrée (`FilterPaletteEntries` sur le registre), ↑/↓/Entrée/Échap ;
  action disabled grisée non exécutable ; exécution ferme la palette.
- Fenêtre « Raccourcis clavier » : table 2 colonnes groupée par catégorie
  (actions à shortcutText non vide) + table statique documentée (WASD/ZQSD,
  Shift course, molette zoom, Numpad 1/3/7 caméra, F1..F12 panneaux, Suppr,
  Ctrl+P) ; entrées de menu `edit.shortcuts` (Édition) et `help.shortcuts`
  (Aide) enregistrées au registre.
- [ ] Steps : impl → commit → **PR 3** (base PR2) → CI des 3 PRs, rapport
  final avec ordre de merge.

## Self-review (fait à la rédaction)

- Couverture spec : §3 registre=T1 ; §4 menus=T4+T6 (récents=T3, dirty=T2) ;
  §5 barre actions=T8 ; §6 palette=T7+T9 ; §7 statut=T5 ; §8 palette
  Ctrl+P + raccourcis=T10+T11 ; §9 tests=T1/T2/T3/T7/T8/T10 ; §10 PRs=T6/T9/T11.
- Déviation assumée vs spec §4 : la barre anglaise M100.1 n'est pas
  **supprimée** mais **dégraissée** (fallback minimal Edit/View/Help pour le
  mode shell in-game sans UI française) — documentée Task 6 + PR.
- Types cohérents : `EditorActionRegistry` produit en T1, consommé T4/T8/T11
  via `WorldEditorShell::MutableActionRegistry()` (introduit T4).
  `ToolLabelFr` produit T7, consommé T5 ? — non : T5 (PR1) utilise
  `ToolbarIconAtlas::Get(tool).tooltipFr` qui existe déjà ; `ToolLabelFr`
  n'est consommé qu'en PR2+. OK.
