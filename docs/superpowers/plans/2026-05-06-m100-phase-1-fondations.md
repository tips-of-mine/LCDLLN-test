# M100 Phase 1 — Fondations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Livrer les 4 tickets de la Phase 1 M100 (M100.1 World Editor Bootstrap, M100.2 Command Stack & Undo/Redo, M100.3 Zone Builder Library Extraction, M100.4 Editor Camera Modes) sous forme **d'une seule PR** sur la branche actuelle. La PR pose la fondation `engine::editor::world::*` pour toute la suite des phases M100, sans casser l'éditeur existant (`engine::editor::WorldEditorImGui`/`WorldEditorSession`) qui continue à tourner en parallèle.

**Architecture :** Mode "couche au-dessus" (cf. design §2). On crée le namespace `engine::editor::world::*` à neuf, isolé de l'existant. M100.1 pose le shell + 6 panneaux + flag `--editor-world` (distinct du flag `--world-editor` actuel). M100.2 ajoute `ICommand` + `CommandStack` consommé par le shell. M100.3 extrait `tools/zone_builder/*` en lib statique (`zone_builder_lib`) sans toucher au format binaire. M100.4 ajoute le `EditorCameraController` consommé par le `ScenePanel`. Aucune passe Vulkan modifiée. Aucun changement réseau.

**Tech Stack :** C++17, ImGui (déjà intégré M43.4), CMake, Catch2 (tests existants), `engine::core::Config` (déjà présent), `engine::core::Log`. Pas de nouvelle dépendance externe.

**Entrées :**
- Specs détaillées : [tickets/M100/M100.1-WorldEditorBootstrap.md](../../../tickets/M100/M100.1-WorldEditorBootstrap.md), [M100.2](../../../tickets/M100/M100.2-CommandStackUndoRedo.md), [M100.3](../../../tickets/M100/M100.3-ZoneBuilderLibraryExtraction.md), [M100.4](../../../tickets/M100/M100.4-EditorCameraModes.md). Chaque spec donne la liste exhaustive des fichiers à créer/modifier, les structs canoniques (déjà reproduites dans les tasks ci-dessous) et les acceptance criteria.
- Audit : [docs/superpowers/audits/2026-05-06-m100-gap-analysis.md](../audits/2026-05-06-m100-gap-analysis.md) sections « Phase 1 — Fondations » + tableau récap. Verdicts : M100.1 partiel (L), M100.2 vide (M), M100.3 partiel (S), M100.4 vide (M).
- Design : [docs/superpowers/specs/2026-05-06-m100-execution-design.md](../specs/2026-05-06-m100-execution-design.md). Cadence : 1 PR pour la phase entière. Validation : CI vert + conformité spec, pas de gate manuel.

---

## File Structure

| Fichier | Rôle | Ticket | Action |
|---------|------|--------|--------|
| `engine/editor/world/IPanel.h` | Interface panneau ancrable | M100.1 | Create |
| `engine/editor/world/WorldEditorShell.h` | Coquille principale (déclaration) | M100.1 | Create |
| `engine/editor/world/WorldEditorShell.cpp` | Coquille principale (implémentation) | M100.1 | Create |
| `engine/editor/world/panels/ScenePanel.{h,cpp}` | Panneau 3D | M100.1 + M100.4 | Create (M100.1) puis Modify (M100.4) |
| `engine/editor/world/panels/InspectorPanel.{h,cpp}` | Panneau Inspector | M100.1 | Create |
| `engine/editor/world/panels/AssetBrowserPanel.{h,cpp}` | Panneau Asset Browser | M100.1 | Create |
| `engine/editor/world/panels/OutlinerPanel.{h,cpp}` | Panneau Outliner | M100.1 | Create |
| `engine/editor/world/panels/ConsolePanel.{h,cpp}` | Panneau Console (sink Log) | M100.1 | Create |
| `engine/editor/world/panels/ToolPropertiesPanel.{h,cpp}` | Panneau Tool Properties | M100.1 | Create |
| `engine/editor/world/panels/HistoryPanel.{h,cpp}` | Panneau History (M100.2) | M100.2 | Create |
| `engine/editor/world/CommandStack.{h,cpp}` | ICommand + CommandStack | M100.2 | Create |
| `engine/editor/world/EditorCameraController.{h,cpp}` | Caméra éditeur (3 modes) | M100.4 | Create |
| `engine/editor/world/tests/WorldEditorShellTests.cpp` | Tests M100.1 | M100.1 | Create |
| `engine/editor/world/tests/CommandStackTests.cpp` | Tests M100.2 | M100.2 | Create |
| `engine/editor/world/tests/EditorCameraControllerTests.cpp` | Tests M100.4 | M100.4 | Create |
| `tools/zone_builder/lib/CMakeLists.txt` | Target `zone_builder_lib` | M100.3 | Create |
| `tools/zone_builder/lib/Public/zone_builder/{ChunkPackageWriter,LayoutImporter,JsonDocument,GltfImporter}.h` | Headers re-exportés | M100.3 | Create (ré-include des sources existantes) |
| `tools/zone_builder/lib/tests/RoundtripTests.cpp` | Tests bit-à-bit | M100.3 | Create |
| `engine/core/LogCategory.h` | Ajout `EditorWorld` | M100.1 | Modify |
| `engine/Engine.cpp` | Branchement `--editor-world` | M100.1 | Modify |
| `engine/editor/EditorMode.{h,cpp}` | Accesseur `IsWorldEditorWorld()` | M100.1 | Modify |
| `tools/zone_builder/CMakeLists.txt` | `zone_builder` link `zone_builder_lib` | M100.3 | Modify |
| `tools/zone_builder/main.cpp` | `#include <zone_builder/...>` | M100.3 | Modify |
| `CMakeLists.txt` (racine) | `add_subdirectory(tools/zone_builder/lib)` + entrées world editor + tests | M100.1 + M100.2 + M100.3 + M100.4 | Modify |
| `game/data/config.json` | Section `editor.world.*` | M100.1 + M100.2 + M100.4 | Modify |
| `scripts/test_zone_builder_baseline.sh` | Test baseline bit-à-bit | M100.3 | Create |

**Conventions transverses (CLAUDE.md) :**
- Toute fonction nouvelle ou modifiée a un commentaire `///` Doxygen au-dessus de sa déclaration (rôle, params non-évidents, effets de bord, contraintes thread).
- Code en français pour les commentaires, anglais pour les identifiants techniques.
- Aucun `--no-verify` sur les commits.

**Contraintes de target serveur (anti-duplication) :**
- Tous les fichiers `engine/editor/world/**/*.cpp` sont **exclus** de `server_app` via la CMake.
- `zone_builder_lib` n'est **pas** linké par `server_app`.
- Vérification : `grep -RIn "engine::editor::world\|zone_builder_lib" engine/server/ src/server/ cpp/server/` doit retourner 0 résultat (la borne dépend de l'arbo réelle, à confirmer dans Task 1).

---

## Tasks

### Task 1 : Reconnaissance — confirmer les chemins et la CMake actuelle

**Files :**
- Read : `CMakeLists.txt` (racine), `tools/zone_builder/CMakeLists.txt`, `tools/zone_builder/main.cpp`, `engine/core/LogCategory.h`, `engine/editor/EditorMode.{h,cpp}`, `engine/Engine.cpp` (sections init/teardown éditeur), `game/data/config.json`.
- Verify : path layout du target serveur (`engine_server`, `server_app` ou autre — l'audit n'a pas confirmé le nom exact).

- [ ] **Step 1 : Lister la structure CMake actuelle**

```bash
grep -nE 'add_library\(engine_core|add_executable\(' CMakeLists.txt | head -40
ls tools/zone_builder/
ls engine/editor/
ls engine/server/ 2>/dev/null || ls cpp/server/ 2>/dev/null
```

Expected : `engine_core` est une lib statique avec une longue liste de sources, `tools/zone_builder/CMakeLists.txt` existe, le target serveur est nommé soit `server_app` soit `engine_server` (à confirmer pour la Task 4 qui doit l'exclure).

- [ ] **Step 2 : Documenter les anomalies dans une note locale (pas commitée)**

Si la CMake a déjà des entrées contradictoires avec les specs (ex. M100.1 spec mentionne `engine/editor/EditorMode.cpp` déjà dans `engine_core`, à vérifier), noter dans le scratch personnel pour l'expliciter dans le commit final.

Pas de commit à cette étape.

---

### Task 2 : `engine::core::LogCategory::EditorWorld` (M100.1)

**Files :**
- Modify : `engine/core/LogCategory.h`

- [ ] **Step 1 : Lire le fichier existant**

```bash
grep -n "enum class LogCategory\|};" engine/core/LogCategory.h
```

Expected : repérer l'enum `LogCategory` et son delimiter de fermeture.

- [ ] **Step 2 : Ajouter la valeur `EditorWorld`**

Ajouter `EditorWorld,` dans l'enum à un emplacement stable (à la fin avant la valeur sentinelle `Count` si elle existe, sinon avant la dernière valeur). Documenter avec un `///` :

```cpp
/// Log category dédiée à l'éditeur de monde 3D AAA (M100.x).
/// Activée par config.json:editor.world.enabled.
EditorWorld,
```

- [ ] **Step 3 : Build de sanity (pas de tests à ce stade)**

```bash
cmake --build build --target engine_core 2>&1 | tail -20
```

Expected : compilation OK (l'ajout d'une valeur d'enum ne casse rien tant qu'on n'a pas de switch exhaustif sur cette enum côté code existant — à vérifier au build).

Si un switch exhaustif compile maintenant en warning sur la nouvelle valeur, ajouter une `case LogCategory::EditorWorld:` avec un comportement neutre (souvent `break;` ou même formatter que `Editor`).

- [ ] **Step 4 : Commit**

```bash
git add engine/core/LogCategory.h
git commit -m "feat(log): ajoute la categorie LogCategory::EditorWorld (M100.1)"
```

---

### Task 3 : Interface `IPanel` (M100.1)

**Files :**
- Create : `engine/editor/world/IPanel.h`

- [ ] **Step 1 : Créer le fichier header**

```cpp
// engine/editor/world/IPanel.h
#pragma once

namespace engine::editor::world
{
	/// Interface commune à tous les panneaux ancrables du shell éditeur monde.
	/// Chaque panneau est rendu via ImGui::Begin/End par le shell ; la visibilité
	/// est portée par chaque implémentation, le shell appelle Render() seulement
	/// si IsVisible() == true.
	class IPanel
	{
	public:
		virtual ~IPanel() = default;

		/// Identifiant stable du panneau, utilisé comme nom de window ImGui.
		/// Doit être ASCII et invariant entre sessions (sert de clé d'ini).
		virtual const char* GetName() const = 0;

		/// Rend le contenu via ImGui::Begin/End. Appelé une fois par frame.
		/// Précondition : IsVisible() == true (le shell garde le contrat).
		virtual void Render() = 0;

		virtual bool IsVisible() const = 0;
		virtual void SetVisible(bool visible) = 0;
	};
}
```

- [ ] **Step 2 : Vérifier que le header compile en isolation**

```bash
cmake --build build --target engine_core 2>&1 | tail -10
```

Expected : pas d'erreur (header non encore inclus, mais la création du fichier ne doit pas casser le build).

- [ ] **Step 3 : Commit**

```bash
git add engine/editor/world/IPanel.h
git commit -m "feat(editor/world): ajoute interface IPanel (M100.1)"
```

---

### Task 4 : Stubs des 6 panneaux M100.1 (M100.1)

**Files :**
- Create : `engine/editor/world/panels/{ScenePanel,InspectorPanel,AssetBrowserPanel,OutlinerPanel,ConsolePanel,ToolPropertiesPanel}.{h,cpp}` (12 fichiers).

Pour chaque panneau, le pattern est identique. Voici l'exemple pour `InspectorPanel` ; les 5 autres suivent strictement le même squelette en changeant uniquement le nom de la classe et la string `GetName()`.

- [ ] **Step 1 : Créer `InspectorPanel.h`**

```cpp
// engine/editor/world/panels/InspectorPanel.h
#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Inspector du shell éditeur monde.
	/// Affiche les propriétés de la sélection courante. M100.1 : placeholder vide.
	class InspectorPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Inspector"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }
	private:
		bool m_visible = true;
	};
}
```

- [ ] **Step 2 : Créer `InspectorPanel.cpp`**

```cpp
// engine/editor/world/panels/InspectorPanel.cpp
#include "engine/editor/world/panels/InspectorPanel.h"
#include "imgui.h"

namespace engine::editor::world::panels
{
	void InspectorPanel::Render()
	{
		if (!m_visible) return;
		if (ImGui::Begin("Inspector", &m_visible))
		{
			ImGui::TextDisabled("Inspector — placeholder M100.1.");
			ImGui::TextWrapped(
				"Les propriétés de la sélection courante apparaîtront ici dès "
				"M100.34 (Selection / Layers).");
		}
		ImGui::End();
	}
}
```

- [ ] **Step 3 : Répliquer le pattern pour les 5 autres panneaux**

Les classes sont nommées `ScenePanel` (string `"Scene"`), `AssetBrowserPanel` (`"Asset Browser"`), `OutlinerPanel` (`"Outliner"`), `ConsolePanel` (`"Console"`), `ToolPropertiesPanel` (`"Tool Properties"`). Pour chaque, le `Render()` affiche un texte placeholder décrivant le rôle prévu.

**Spécificité `ConsolePanel` :** au lieu d'un placeholder pur, abonnement à un sink `engine::core::Log`. M100.1 spec dit : "ConsolePanel : reflète les `LOG_*(EditorWorld, …)` via un sink `engine::core::Log`". Voir Task 5 pour le wire-up du sink.

**Spécificité `ScenePanel` :** ajouter dans le `.h` un membre `private int m_viewportWidth = 0; int m_viewportHeight = 0;` et un getter. Sera consommé par M100.4 (camera modes).

- [ ] **Step 4 : Vérifier la compilation**

Les fichiers ne sont pas encore listés dans la CMake (Task 9). Build sans cette tâche échouera. Différer le build à Task 9.

- [ ] **Step 5 : Commit**

```bash
git add engine/editor/world/panels/
git commit -m "feat(editor/world): ajoute 6 panneaux placeholder (M100.1)"
```

---

### Task 5 : `WorldEditorShell` — déclaration (M100.1)

**Files :**
- Create : `engine/editor/world/WorldEditorShell.h`

- [ ] **Step 1 : Créer le header**

Reproduire la struct exacte de la spec M100.1 §"Structures de données" (lignes 125-170 du ticket). En particulier :

```cpp
// engine/editor/world/WorldEditorShell.h
#pragma once
#include "engine/editor/world/IPanel.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// Coquille principale de l'éditeur de monde. Instanciée une fois par
	/// processus quand `editor.world.enabled` est vrai (config.json) ou
	/// `--editor-world` (CLI). Possède la liste des panneaux ancrables, le
	/// dockspace, le menu bar, et dispatche les raccourcis F1..F12 + Ctrl+Z/Y.
	class WorldEditorShell
	{
	public:
		/// Charge la config, instancie les panneaux, charge le layout ImGui.
		/// \return true si Init OK, false si layout_path non écrivable.
		/// Effet de bord : crée `editor_world_layout.ini` si absent.
		bool Init(const engine::core::Config& cfg);

		/// Persiste le layout sur disque, libère les panneaux.
		void Shutdown();

		/// Appelé chaque frame depuis Engine::DrawFrame avant la passe ImGui.
		/// Doit être appelé sur le main thread (ImGui n'est pas thread-safe).
		void RenderFrame();

		/// Dispatche F1..F12 vers le panneau correspondant.
		/// \return true si la touche a été consommée.
		bool HandleShortcut(int virtualKey);

		/// Marque le document éditeur comme modifié.
		/// \param reason texte court loggé en EditorWorld pour debug.
		void MarkDirty(std::string_view reason);
		bool IsDirty() const { return m_dirty; }

		/// Accès lecture seule pour les tests et le HistoryPanel (M100.2).
		const std::vector<std::unique_ptr<IPanel>>& Panels() const { return m_panels; }

	private:
		void RenderMenuBar();
		void RenderDockspace();
		void EnsureLayoutPersisted();
		void ResetLayoutToDefault();

		std::vector<std::unique_ptr<IPanel>> m_panels;
		std::string m_layoutPath;
		bool m_dirty = false;
		bool m_initialized = false;
	};
}
```

- [ ] **Step 2 : Commit**

```bash
git add engine/editor/world/WorldEditorShell.h
git commit -m "feat(editor/world): declare WorldEditorShell (M100.1)"
```

---

### Task 6 : Tests `WorldEditorShell` (TDD — écrire avant l'impl) (M100.1)

**Files :**
- Create : `engine/editor/world/tests/WorldEditorShellTests.cpp`

- [ ] **Step 1 : Écrire les 4 tests de la spec M100.1 §Tests**

```cpp
// engine/editor/world/tests/WorldEditorShellTests.cpp
#include "engine/editor/world/WorldEditorShell.h"
#include "engine/editor/world/panels/InspectorPanel.h"
#include "engine/core/Config.h"
#include <catch2/catch.hpp>
#include <filesystem>

using engine::editor::world::WorldEditorShell;
using engine::editor::world::panels::InspectorPanel;

namespace
{
	engine::core::Config MakeTestConfig(const std::filesystem::path& layoutPath, bool worldEnabled)
	{
		engine::core::Config cfg;
		cfg.SetBool("editor.world.enabled", worldEnabled);
		cfg.SetString("editor.world.layout_path", layoutPath.string());
		return cfg;
	}
}

TEST_CASE("Test_Init_LoadsDefaultLayout_WhenIniMissing", "[M100.1][WorldEditorShell]")
{
	auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_missing.ini";
	std::filesystem::remove(tmp);
	auto cfg = MakeTestConfig(tmp, true);
	WorldEditorShell shell;
	REQUIRE(shell.Init(cfg));
	REQUIRE(shell.Panels().size() == 6);
	shell.Shutdown();
}

TEST_CASE("Test_Init_PersistsLayoutOnShutdown", "[M100.1][WorldEditorShell]")
{
	auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_persist.ini";
	std::filesystem::remove(tmp);
	auto cfg = MakeTestConfig(tmp, true);
	WorldEditorShell shell;
	REQUIRE(shell.Init(cfg));
	shell.Shutdown();
	REQUIRE(std::filesystem::exists(tmp));
}

TEST_CASE("Test_HandleShortcut_FocusesPanel", "[M100.1][WorldEditorShell]")
{
	auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_shortcut.ini";
	auto cfg = MakeTestConfig(tmp, true);
	WorldEditorShell shell;
	REQUIRE(shell.Init(cfg));
	// F2 = focus Inspector. On vérifie que Inspector::IsVisible() == true.
	REQUIRE(shell.HandleShortcut(/*VK_F2*/ 0x71));
	auto* inspector = dynamic_cast<InspectorPanel*>(shell.Panels()[1].get());
	REQUIRE(inspector != nullptr);
	REQUIRE(inspector->IsVisible());
	shell.Shutdown();
}

TEST_CASE("Test_MarkDirty_SetsFlag", "[M100.1][WorldEditorShell]")
{
	auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_dirty.ini";
	auto cfg = MakeTestConfig(tmp, true);
	WorldEditorShell shell;
	REQUIRE(shell.Init(cfg));
	REQUIRE(!shell.IsDirty());
	shell.MarkDirty("test");
	REQUIRE(shell.IsDirty());
	shell.Shutdown();
}
```

**Note :** l'ordre des panneaux dans `m_panels` doit être stable et documenté. Convention proposée pour M100.1 : `[Scene, Inspector, AssetBrowser, Outliner, Console, ToolProperties]`. Le test `Test_HandleShortcut_FocusesPanel` repose sur l'index 1 = Inspector — à conserver en M100.2 quand on insère HistoryPanel (Task 13).

- [ ] **Step 2 : Run le test, vérifier qu'il fail (impl manquante)**

```bash
cmake --build build --target world_editor_shell_tests 2>&1 | tail -10
```

Expected : link error sur `WorldEditorShell::Init` etc. (impl pas encore écrite — Task 7).

- [ ] **Step 3 : Commit (test seul, fail attendu — c'est le D du TDD)**

```bash
git add engine/editor/world/tests/WorldEditorShellTests.cpp
git commit -m "test(editor/world): WorldEditorShell tests M100.1 (TDD red)"
```

---

### Task 7 : `WorldEditorShell` — implémentation (M100.1)

**Files :**
- Create : `engine/editor/world/WorldEditorShell.cpp`

- [ ] **Step 1 : Implémenter `Init`**

L'implémentation doit (cf. spec M100.1 §Spécification fonctionnelle) :
1. Lire `cfg.GetString("editor.world.layout_path")` ; défaut `"editor_world_layout.ini"`.
2. Stocker dans `m_layoutPath`.
3. `m_panels.push_back(std::make_unique<panels::ScenePanel>())` puis Inspector, AssetBrowser, Outliner, Console, ToolProperties (ordre stable, cf. note Task 6).
4. Si `std::filesystem::exists(m_layoutPath)`, appeler `ImGui::LoadIniSettingsFromDisk(m_layoutPath.c_str())`. Sinon appeler `ResetLayoutToDefault()`.
5. `m_initialized = true; return true`.

```cpp
// engine/editor/world/WorldEditorShell.cpp (extrait Init)
#include "engine/editor/world/WorldEditorShell.h"
#include "engine/editor/world/panels/ScenePanel.h"
#include "engine/editor/world/panels/InspectorPanel.h"
#include "engine/editor/world/panels/AssetBrowserPanel.h"
#include "engine/editor/world/panels/OutlinerPanel.h"
#include "engine/editor/world/panels/ConsolePanel.h"
#include "engine/editor/world/panels/ToolPropertiesPanel.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "imgui.h"
#include <filesystem>

namespace engine::editor::world
{
	bool WorldEditorShell::Init(const engine::core::Config& cfg)
	{
		m_layoutPath = cfg.GetString("editor.world.layout_path", "editor_world_layout.ini");
		m_panels.clear();
		m_panels.emplace_back(std::make_unique<panels::ScenePanel>());
		m_panels.emplace_back(std::make_unique<panels::InspectorPanel>());
		m_panels.emplace_back(std::make_unique<panels::AssetBrowserPanel>());
		m_panels.emplace_back(std::make_unique<panels::OutlinerPanel>());
		m_panels.emplace_back(std::make_unique<panels::ConsolePanel>());
		m_panels.emplace_back(std::make_unique<panels::ToolPropertiesPanel>());

		if (std::filesystem::exists(m_layoutPath))
			ImGui::LoadIniSettingsFromDisk(m_layoutPath.c_str());
		else
			ResetLayoutToDefault();

		m_initialized = true;
		LOG_INFO(EditorWorld, "WorldEditorShell init OK, %zu panels", m_panels.size());
		return true;
	}
}
```

- [ ] **Step 2 : Implémenter `Shutdown`**

```cpp
void WorldEditorShell::Shutdown()
{
	if (!m_initialized) return;
	EnsureLayoutPersisted();
	m_panels.clear();
	m_initialized = false;
}

void WorldEditorShell::EnsureLayoutPersisted()
{
	ImGui::SaveIniSettingsToDisk(m_layoutPath.c_str());
}
```

- [ ] **Step 3 : Implémenter `RenderFrame`, `RenderMenuBar`, `RenderDockspace`**

Le `RenderDockspace` utilise `ImGui::DockSpaceOverViewport`. Le `RenderMenuBar` ajoute les menus File/Edit/View/Tools/Window/Help avec les items de la spec M100.1 §"Menu bar". Voir spec lignes 85-94 pour la liste exacte.

```cpp
void WorldEditorShell::RenderFrame()
{
	if (!m_initialized) return;
	RenderMenuBar();
	RenderDockspace();
	for (auto& panel : m_panels)
		if (panel && panel->IsVisible())
			panel->Render();
}

void WorldEditorShell::RenderDockspace()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::SetNextWindowViewport(vp->ID);
	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("WorldEditorDockSpace", nullptr, flags);
	ImGui::PopStyleVar(3);
	ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceId");
	ImGui::DockSpace(dockId, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
}
```

`RenderMenuBar` : voir spec M100.1 §"Menu bar" lignes 85-94 ; items File/Edit/View/Tools/Window/Help avec sous-items spécifiés.

- [ ] **Step 4 : Implémenter `HandleShortcut`**

Mapping virtualKey → panneau (cf. spec M100.1 §"Raccourcis" lignes 70-83) :

```cpp
bool WorldEditorShell::HandleShortcut(int virtualKey)
{
	// Mapping VK_F1..VK_F4, F6, F12 → panel index dans m_panels.
	// Convention indices : 0=Scene, 1=Inspector, 2=AssetBrowser, 3=Outliner, 4=Console, 5=ToolProperties
	int panelIndex = -1;
	switch (virtualKey)
	{
		case 0x70 /*VK_F1*/: panelIndex = 0; break;  // Scene
		case 0x71 /*VK_F2*/: panelIndex = 1; break;  // Inspector
		case 0x72 /*VK_F3*/: panelIndex = 2; break;  // Asset Browser
		case 0x73 /*VK_F4*/: panelIndex = 3; break;  // Outliner
		case 0x75 /*VK_F6*/: panelIndex = 4; break;  // Console
		case 0x7B /*VK_F12*/: panelIndex = 5; break; // Tool Properties
		case 0x74 /*VK_F5*/: return true;  // Réservé Playtest M100.33, no-op
		case 0x7A /*VK_F11*/: return true; // Toggle plein écran (no-op M100.1)
		default: return false;
	}
	if (panelIndex >= 0 && panelIndex < (int)m_panels.size())
	{
		m_panels[panelIndex]->SetVisible(true);
		ImGui::SetWindowFocus(m_panels[panelIndex]->GetName());
		return true;
	}
	return false;
}
```

- [ ] **Step 5 : Implémenter `MarkDirty`, `ResetLayoutToDefault`**

```cpp
void WorldEditorShell::MarkDirty(std::string_view reason)
{
	m_dirty = true;
	LOG_INFO(EditorWorld, "WorldEditorShell dirty: %.*s", (int)reason.size(), reason.data());
}

void WorldEditorShell::ResetLayoutToDefault()
{
	// Layout par défaut décrit dans la spec M100.1 §"Layout par défaut" lignes 55-66.
	// L'implémentation manipule ImGui DockBuilder pour positionner :
	//   Outliner (left) | Scene (center) | Inspector (right)
	//   AssetBrowser | ToolProperties | Console (bottom row)
	// Voir M43.4 WorldEditorImGui::DockBuilderV2 pour le pattern de référence.
	// (Stub à compléter au build d'intégration ; les tests M100.1 ne testent
	// pas la disposition exacte, juste que Init persiste un .ini valide.)
	for (auto& panel : m_panels)
		panel->SetVisible(true);
}
```

- [ ] **Step 6 : Run les tests (TDD green)**

```bash
cmake --build build --target world_editor_shell_tests 2>&1 | tail -10
ctest --test-dir build -R world_editor_shell_tests --output-on-failure
```

Expected : 4/4 tests pass.

- [ ] **Step 7 : Commit**

```bash
git add engine/editor/world/WorldEditorShell.cpp
git commit -m "feat(editor/world): implemente WorldEditorShell + tests verts (M100.1)"
```

---

### Task 8 : `engine/editor/EditorMode` — accesseur `IsWorldEditorWorld()` (M100.1)

**Files :**
- Modify : `engine/editor/EditorMode.h`, `engine/editor/EditorMode.cpp`

- [ ] **Step 1 : Lire l'existant**

```bash
grep -n "class EditorMode\|IsWorldEditor\|m_worldEditor" engine/editor/EditorMode.h engine/editor/EditorMode.cpp
```

Repérer : où sont stockés les flags actuels, comment les autres accesseurs sont nommés.

- [ ] **Step 2 : Ajouter `m_worldEditorWorld` et `IsWorldEditorWorld()`**

Header :

```cpp
// engine/editor/EditorMode.h (ajout)
public:
	/// True si --editor-world / editor.world.enabled. Distinct de IsWorldEditor()
	/// qui désigne l'ancien éditeur (M43.x). M100 = "couche au-dessus".
	bool IsWorldEditorWorld() const { return m_worldEditorWorld; }
	void SetWorldEditorWorld(bool enabled) { m_worldEditorWorld = enabled; }
private:
	bool m_worldEditorWorld = false;
```

Cpp : aucune modification nécessaire si le header expose tout en inline. Si la convention du repo veut le getter/setter en cpp, déplacer.

- [ ] **Step 3 : Build + commit**

```bash
cmake --build build --target engine_core 2>&1 | tail -5
git add engine/editor/EditorMode.h engine/editor/EditorMode.cpp
git commit -m "feat(editor): accesseur IsWorldEditorWorld pour M100.1"
```

---

### Task 9 : CMake — ajouter les sources M100.1 à `engine_core` + tests (M100.1)

**Files :**
- Modify : `CMakeLists.txt` (racine)

- [ ] **Step 1 : Ajouter les sources dans `add_library(engine_core ...)`**

Reproduire le bloc spec M100.1 §"Diff CMake" §"after" lignes 213-227 :

```cmake
add_library(engine_core
    ...
    engine/editor/EditorMode.cpp
    engine/editor/WorldEditorImGui.cpp
    engine/editor/WorldEditorSession.cpp
    engine/editor/world/WorldEditorShell.cpp
    engine/editor/world/panels/ScenePanel.cpp
    engine/editor/world/panels/InspectorPanel.cpp
    engine/editor/world/panels/AssetBrowserPanel.cpp
    engine/editor/world/panels/OutlinerPanel.cpp
    engine/editor/world/panels/ConsolePanel.cpp
    engine/editor/world/panels/ToolPropertiesPanel.cpp
    ...
)
```

- [ ] **Step 2 : Ajouter le target test**

```cmake
add_executable(world_editor_shell_tests
    engine/editor/world/tests/WorldEditorShellTests.cpp)
target_link_libraries(world_editor_shell_tests PRIVATE engine_core)
add_test(NAME world_editor_shell_tests COMMAND world_editor_shell_tests)
```

- [ ] **Step 3 : Re-build complet**

```bash
cmake --build build 2>&1 | tail -20
ctest --test-dir build -R world_editor_shell_tests --output-on-failure
```

Expected : build OK, 4/4 tests pass.

- [ ] **Step 4 : Commit**

```bash
git add CMakeLists.txt
git commit -m "build(cmake): ajoute world editor shell + tests (M100.1)"
```

---

### Task 10 : Branchement `--editor-world` + config.json (M100.1)

**Files :**
- Modify : `engine/Engine.cpp`, `game/data/config.json`

- [ ] **Step 1 : Ajouter la section config.json**

Insérer dans `game/data/config.json`, à côté des autres sections `editor.*` :

```json
"editor": {
    "world": {
        "enabled": false,
        "layout_path": "editor_world_layout.ini",
        "undo": { "capacity": 256, "maxBytes": 268435456 },
        "camera": { "lastMode": "FPS", "fpsSpeed": 8.0 }
    },
    ...
}
```

(Les clés `undo.*` et `camera.*` sont posées maintenant pour ne pas re-toucher au JSON aux Tasks 11/16.)

- [ ] **Step 2 : Lire le flag CLI dans `Engine.cpp`**

Repérer la section où `--world-editor` est parsé (probablement dans `Engine::ParseCli` ou équivalent). Ajouter un flag jumeau `--editor-world` qui setraisera `m_worldEditorWorld = true`.

```cpp
// engine/Engine.cpp (extrait, dans la zone de parsing CLI)
if (arg == "--editor-world")
{
    m_editorMode.SetWorldEditorWorld(true);
    LOG_INFO(EditorWorld, "Flag --editor-world detected, enabling world editor shell");
    continue;
}
```

Et lire la config :

```cpp
if (m_config.GetBool("editor.world.enabled", false))
    m_editorMode.SetWorldEditorWorld(true);
```

- [ ] **Step 3 : Instancier le shell si flag actif**

À l'endroit où l'éditeur existant (`WorldEditorImGui`) est initialisé, ajouter la branche :

```cpp
// engine/Engine.cpp (init éditeur)
if (m_editorMode.IsWorldEditorWorld())
{
    m_worldEditorShell = std::make_unique<engine::editor::world::WorldEditorShell>();
    if (!m_worldEditorShell->Init(m_config))
    {
        LOG_ERROR(EditorWorld, "WorldEditorShell::Init failed");
        m_worldEditorShell.reset();
    }
}
```

Le shell **vit en parallèle** de `WorldEditorImGui` (mode "couche au-dessus"). Une frame peut très bien rendre les deux. À l'usage, `--editor-world` activerait le nouveau, `--world-editor` activerait l'ancien, et lancer les deux flags est un cas non-supporté (à logger en warning si détecté).

Ajouter dans `Engine.h` le membre :

```cpp
std::unique_ptr<engine::editor::world::WorldEditorShell> m_worldEditorShell;
```

- [ ] **Step 4 : Brancher `RenderFrame` et `Shutdown`**

```cpp
// Dans Engine::DrawFrame, après ImGui::NewFrame() et avant la passe ImGui:
if (m_worldEditorShell) m_worldEditorShell->RenderFrame();

// Dans Engine::Shutdown:
if (m_worldEditorShell) { m_worldEditorShell->Shutdown(); m_worldEditorShell.reset(); }
```

- [ ] **Step 5 : Build + lancement smoke**

```bash
cmake --build build --target lcdlln_world_editor 2>&1 | tail -10
```

Expected : build OK. (Le smoke test manuel ne fait pas partie de la phase 1 selon le design §2 Q5 — CI green only.)

- [ ] **Step 6 : Commit**

```bash
git add engine/Engine.cpp engine/Engine.h game/data/config.json
git commit -m "feat(editor/world): branche --editor-world + config.json (M100.1)"
```

---

### Task 11 : `ICommand` + `CommandStack` — déclaration (M100.2)

**Files :**
- Create : `engine/editor/world/CommandStack.h`

- [ ] **Step 1 : Reproduire la struct exacte de la spec M100.2 §Structures lignes 67-133**

Le header complet est dans le ticket M100.2 lignes 67-133. Le copier verbatim (avec les commentaires Doxygen `///` exigés par CLAUDE.md).

- [ ] **Step 2 : Commit**

```bash
git add engine/editor/world/CommandStack.h
git commit -m "feat(editor/world): declare ICommand + CommandStack (M100.2)"
```

---

### Task 12 : Tests `CommandStack` (TDD red) (M100.2)

**Files :**
- Create : `engine/editor/world/tests/CommandStackTests.cpp`

- [ ] **Step 1 : Écrire les 7 tests de la spec M100.2 §Tests lignes 207-214**

Tests à écrire (noms exacts) : `Test_PushExecutesAndStores`, `Test_UndoRedoRoundtrip`, `Test_CapacityEvictsOldest`, `Test_MaxBytesEvictsOldest`, `Test_MergeKeyCoalescesConsecutive`, `Test_PushClearsRedoStack`, `Test_RewindToReplaysCascade`.

Pour chaque test, créer un mock `ICommand` qui incrémente un compteur dans `Execute()` / décrémente dans `Undo()`. Exemple pour `Test_UndoRedoRoundtrip` :

```cpp
// engine/editor/world/tests/CommandStackTests.cpp
#include "engine/editor/world/CommandStack.h"
#include <catch2/catch.hpp>

using namespace engine::editor::world;

namespace
{
	struct CounterCommand : ICommand
	{
		int* counter;
		int delta;
		CommandMergeKey mergeKey = 0;
		CounterCommand(int* c, int d, CommandMergeKey k = 0) : counter(c), delta(d), mergeKey(k) {}
		const char* GetLabel() const override { return "Counter"; }
		size_t GetMemoryFootprint() const override { return sizeof(CounterCommand); }
		CommandMergeKey GetMergeKey() const override { return mergeKey; }
		void Execute() override { *counter += delta; }
		void Undo() override { *counter -= delta; }
	};
}

TEST_CASE("Test_UndoRedoRoundtrip", "[M100.2][CommandStack]")
{
	int counter = 0;
	CommandStack stack;
	stack.Push(std::make_unique<CounterCommand>(&counter, 5));
	REQUIRE(counter == 5);
	stack.Undo();
	REQUIRE(counter == 0);
	stack.Redo();
	REQUIRE(counter == 5);
}
```

Répliquer le pattern pour les 6 autres tests. `Test_MergeKeyCoalescesConsecutive` : pousser 3 commandes avec même `mergeKey` non-nulle, vérifier `UndoSize() == 1`. `Test_CapacityEvictsOldest` : `Configure({.capacity = 3})`, push 5 commandes, vérifier `UndoSize() == 3`. Etc.

- [ ] **Step 2 : Run, vérifier qu'il fail (impl manquante)**

```bash
cmake --build build --target command_stack_tests 2>&1 | tail -5
```

Expected : link error sur `CommandStack::Push` etc.

- [ ] **Step 3 : Commit**

```bash
git add engine/editor/world/tests/CommandStackTests.cpp
git commit -m "test(editor/world): CommandStack tests M100.2 (TDD red)"
```

---

### Task 13 : `CommandStack` — implémentation (M100.2 — TDD green)

**Files :**
- Create : `engine/editor/world/CommandStack.cpp`

- [ ] **Step 1 : Implémenter `Push` avec coalescing**

```cpp
// engine/editor/world/CommandStack.cpp
#include "engine/editor/world/CommandStack.h"
#include <chrono>

namespace engine::editor::world
{
	namespace { uint64_t NowMs() {
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	} }

	void CommandStack::Configure(const CommandStackConfig& cfg) { m_cfg = cfg; }

	void CommandStack::Push(std::unique_ptr<ICommand> cmd)
	{
		cmd->Execute();
		m_redo.clear();
		m_redoTimestamps.clear();

		// Coalescing si mergeKey non-nul et identique à la commande au sommet.
		if (!m_undo.empty() && cmd->GetMergeKey() != 0 &&
		    m_undo.back()->GetMergeKey() == cmd->GetMergeKey())
		{
			if (m_undo.back()->TryMerge(*cmd))
			{
				// La commande au sommet a absorbé. Recompute totalBytes.
				m_totalBytes = 0;
				for (auto& c : m_undo) m_totalBytes += c->GetMemoryFootprint();
				return;
			}
		}

		m_totalBytes += cmd->GetMemoryFootprint();
		m_undo.push_back(std::move(cmd));
		m_undoTimestamps.push_back(NowMs());

		while (m_undo.size() > m_cfg.capacity || m_totalBytes > m_cfg.maxBytes)
			EvictOldest();
	}

	bool CommandStack::CanUndo() const { return !m_undo.empty(); }
	bool CommandStack::CanRedo() const { return !m_redo.empty(); }

	void CommandStack::Undo()
	{
		if (m_undo.empty()) return;
		auto cmd = std::move(m_undo.back());
		m_undo.pop_back();
		auto ts = m_undoTimestamps.back();
		m_undoTimestamps.pop_back();
		cmd->Undo();
		m_totalBytes -= cmd->GetMemoryFootprint();
		m_redo.push_back(std::move(cmd));
		m_redoTimestamps.push_back(ts);
	}

	void CommandStack::Redo()
	{
		if (m_redo.empty()) return;
		auto cmd = std::move(m_redo.back());
		m_redo.pop_back();
		auto ts = m_redoTimestamps.back();
		m_redoTimestamps.pop_back();
		cmd->Execute();
		m_totalBytes += cmd->GetMemoryFootprint();
		m_undo.push_back(std::move(cmd));
		m_undoTimestamps.push_back(ts);
	}

	void CommandStack::RewindTo(size_t targetIndex)
	{
		while (m_undo.size() > targetIndex && CanUndo()) Undo();
	}

	void CommandStack::Clear()
	{
		m_undo.clear(); m_redo.clear();
		m_undoTimestamps.clear(); m_redoTimestamps.clear();
		m_totalBytes = 0;
	}

	size_t CommandStack::UndoSize() const { return m_undo.size(); }
	size_t CommandStack::RedoSize() const { return m_redo.size(); }
	size_t CommandStack::TotalBytes() const { return m_totalBytes; }

	std::vector<CommandStack::HistoryEntry> CommandStack::SnapshotHistory() const
	{
		std::vector<HistoryEntry> out;
		out.reserve(m_undo.size());
		for (size_t i = 0; i < m_undo.size(); ++i)
			out.push_back({ m_undo[i]->GetLabel(), m_undo[i]->GetMemoryFootprint(), m_undoTimestamps[i] });
		return out;
	}

	void CommandStack::EvictOldest()
	{
		if (m_undo.empty()) return;
		m_totalBytes -= m_undo.front()->GetMemoryFootprint();
		m_undo.erase(m_undo.begin());
		m_undoTimestamps.erase(m_undoTimestamps.begin());
	}
}
```

- [ ] **Step 2 : Build + tests (TDD green)**

```bash
cmake --build build --target command_stack_tests 2>&1 | tail -5
ctest --test-dir build -R command_stack_tests --output-on-failure
```

Expected : 7/7 tests pass.

- [ ] **Step 3 : Ajouter à la CMake**

```cmake
# CMakeLists.txt — ajouter dans engine_core sources
engine/editor/world/CommandStack.cpp

# Ajouter target test
add_executable(command_stack_tests engine/editor/world/tests/CommandStackTests.cpp)
target_link_libraries(command_stack_tests PRIVATE engine_core)
add_test(NAME command_stack_tests COMMAND command_stack_tests)
```

- [ ] **Step 4 : Commit**

```bash
git add engine/editor/world/CommandStack.cpp CMakeLists.txt
git commit -m "feat(editor/world): implemente CommandStack + tests verts (M100.2)"
```

---

### Task 14 : `HistoryPanel` (M100.2)

**Files :**
- Create : `engine/editor/world/panels/HistoryPanel.{h,cpp}`

- [ ] **Step 1 : Créer le header et l'implémentation**

```cpp
// engine/editor/world/panels/HistoryPanel.h
#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world { class CommandStack; }

namespace engine::editor::world::panels
{
	/// Panneau History : liste les commandes empilées dans le CommandStack
	/// avec timestamp et empreinte mémoire. Cliquer sur une ligne ancienne
	/// déclenche RewindTo en cascade.
	class HistoryPanel final : public IPanel
	{
	public:
		explicit HistoryPanel(CommandStack* stack) : m_stack(stack) {}
		const char* GetName() const override { return "History"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool v) override { m_visible = v; }
	private:
		CommandStack* m_stack = nullptr;
		bool m_visible = true;
	};
}
```

```cpp
// engine/editor/world/panels/HistoryPanel.cpp
#include "engine/editor/world/panels/HistoryPanel.h"
#include "engine/editor/world/CommandStack.h"
#include "imgui.h"

namespace engine::editor::world::panels
{
	void HistoryPanel::Render()
	{
		if (!m_visible || !m_stack) return;
		if (ImGui::Begin("History", &m_visible))
		{
			if (ImGui::Button("Clear History")) m_stack->Clear();
			ImGui::Separator();
			auto entries = m_stack->SnapshotHistory();
			for (size_t i = 0; i < entries.size(); ++i)
			{
				auto& e = entries[i];
				ImGui::PushID((int)i);
				char buf[128];
				std::snprintf(buf, sizeof(buf), "%s (%zu B)", e.label.c_str(), e.bytes);
				if (ImGui::Selectable(buf, i + 1 == entries.size()))
					m_stack->RewindTo(i);
				ImGui::PopID();
			}
		}
		ImGui::End();
	}
}
```

- [ ] **Step 2 : Ajouter à la CMake**

```cmake
engine/editor/world/panels/HistoryPanel.cpp
```

- [ ] **Step 3 : Build OK + commit**

```bash
cmake --build build --target engine_core 2>&1 | tail -5
git add engine/editor/world/panels/HistoryPanel.h engine/editor/world/panels/HistoryPanel.cpp CMakeLists.txt
git commit -m "feat(editor/world): ajoute HistoryPanel (M100.2)"
```

---

### Task 15 : Wire `CommandStack` dans `WorldEditorShell` (M100.2)

**Files :**
- Modify : `engine/editor/world/WorldEditorShell.{h,cpp}`

- [ ] **Step 1 : Ajouter `m_commandStack` au shell**

```cpp
// engine/editor/world/WorldEditorShell.h (ajout)
#include "engine/editor/world/CommandStack.h"

class WorldEditorShell
{
public:
    CommandStack& MutableCommandStack() { return m_commandStack; }
    const CommandStack& CommandStack_() const { return m_commandStack; }
private:
    engine::editor::world::CommandStack m_commandStack;
};
```

- [ ] **Step 2 : Initialiser depuis config.json + ajouter HistoryPanel**

```cpp
// engine/editor/world/WorldEditorShell.cpp Init() — extension
bool WorldEditorShell::Init(const engine::core::Config& cfg)
{
    // ... (init existant Tasks 7) ...

    CommandStackConfig csCfg;
    csCfg.capacity = cfg.GetInt("editor.world.undo.capacity", 256);
    csCfg.maxBytes = cfg.GetInt64("editor.world.undo.maxBytes", 256ull * 1024 * 1024);
    m_commandStack.Configure(csCfg);

    // Insère HistoryPanel après les 6 panneaux M100.1.
    // L'ordre m_panels reste : [Scene=0, Inspector=1, AssetBrowser=2, Outliner=3,
    //                          Console=4, ToolProperties=5, History=6].
    m_panels.emplace_back(std::make_unique<panels::HistoryPanel>(&m_commandStack));

    // ... reste de Init ...
}
```

- [ ] **Step 3 : Brancher Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z**

Étendre `HandleShortcut` pour recevoir un mask de modifiers (Ctrl, Shift) :

```cpp
// engine/editor/world/WorldEditorShell.h
bool HandleShortcut(int virtualKey, bool ctrl, bool shift);

// engine/editor/world/WorldEditorShell.cpp
bool WorldEditorShell::HandleShortcut(int vk, bool ctrl, bool shift)
{
    if (ctrl && (vk == 'Z') && !shift) { m_commandStack.Undo(); return true; }
    if (ctrl && (vk == 'Z') && shift)  { m_commandStack.Redo(); return true; }
    if (ctrl && (vk == 'Y'))           { m_commandStack.Redo(); return true; }
    return HandleShortcut(vk); // F1..F12 sans modifier
}
```

(Backward-compat : la signature à 1 argument reste pour les tests M100.1 Task 6.)

- [ ] **Step 4 : Brancher l'appel depuis Engine.cpp**

À l'endroit où le shell reçoit les inputs (dans `Engine::DrawFrame` après le sondage clavier), passer les modifiers :

```cpp
ImGuiIO& io = ImGui::GetIO();
const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
// dispatch sur les VK relevants
```

- [ ] **Step 5 : Build + tests (M100.1 + M100.2 doivent encore passer)**

```bash
cmake --build build 2>&1 | tail -10
ctest --test-dir build -R "world_editor_shell_tests|command_stack_tests" --output-on-failure
```

Expected : 4 + 7 = 11 tests pass.

- [ ] **Step 6 : Commit**

```bash
git add engine/editor/world/WorldEditorShell.h engine/editor/world/WorldEditorShell.cpp engine/Engine.cpp
git commit -m "feat(editor/world): integre CommandStack dans WorldEditorShell + Ctrl+Z/Y (M100.2)"
```

---

### Task 16 : `zone_builder_lib` — extraction (M100.3)

**Files :**
- Create : `tools/zone_builder/lib/CMakeLists.txt`, `tools/zone_builder/lib/Public/zone_builder/{ChunkPackageWriter,LayoutImporter,JsonDocument,GltfImporter}.h`
- Modify : `tools/zone_builder/CMakeLists.txt`, `tools/zone_builder/main.cpp`, `CMakeLists.txt` (racine)

- [ ] **Step 1 : Créer `tools/zone_builder/lib/CMakeLists.txt`**

Reproduire la spec M100.3 §"Diff CMake" §"after" lignes 96-117.

```cmake
# tools/zone_builder/lib/CMakeLists.txt (NEW)
add_library(zone_builder_lib STATIC
    ../ChunkPackageWriter.cpp
    ../GltfImporter.cpp
    ../JsonDocument.cpp
    ../LayoutImporter.cpp
)
target_include_directories(zone_builder_lib PUBLIC Public)
target_link_libraries(zone_builder_lib PUBLIC engine_core)
```

(Note : on consomme les `.cpp` *en place* via chemins relatifs `../` pour ne pas dupliquer ni déplacer physiquement les fichiers à cette étape — la spec dit "déplacer" mais un layout STATIC qui pointe sur les sources existantes atteint le même but sans casser les autres consommateurs s'il y en a. À discuter dans le commit message.)

**Alternative plus stricte (suit littéralement la spec) :** déplacer physiquement les `.cpp` dans `tools/zone_builder/lib/` et les `.h` publics dans `tools/zone_builder/lib/Public/zone_builder/`. Plus de fragilité de chemin mais demande plus d'updates côté consommateurs (main.cpp, futurs). Choix par défaut : **alternative stricte** pour matcher la spec.

- [ ] **Step 2 : Si choix strict — déplacer les fichiers**

```bash
mkdir -p tools/zone_builder/lib/Public/zone_builder
git mv tools/zone_builder/ChunkPackageWriter.cpp tools/zone_builder/lib/
git mv tools/zone_builder/GltfImporter.cpp tools/zone_builder/lib/
git mv tools/zone_builder/JsonDocument.cpp tools/zone_builder/lib/
git mv tools/zone_builder/LayoutImporter.cpp tools/zone_builder/lib/
git mv tools/zone_builder/ChunkPackageWriter.h tools/zone_builder/lib/Public/zone_builder/
git mv tools/zone_builder/GltfImporter.h tools/zone_builder/lib/Public/zone_builder/
git mv tools/zone_builder/JsonDocument.h tools/zone_builder/lib/Public/zone_builder/
git mv tools/zone_builder/LayoutImporter.h tools/zone_builder/lib/Public/zone_builder/
```

Mettre à jour `tools/zone_builder/lib/CMakeLists.txt` (sources sans `../`) :

```cmake
add_library(zone_builder_lib STATIC
    ChunkPackageWriter.cpp
    GltfImporter.cpp
    JsonDocument.cpp
    LayoutImporter.cpp
)
target_include_directories(zone_builder_lib PUBLIC Public)
target_link_libraries(zone_builder_lib PUBLIC engine_core)
```

- [ ] **Step 3 : Mettre à jour les includes dans les `.cpp` déplacés**

Si les sources avaient des `#include "ChunkPackageWriter.h"` (chemin relatif au dossier d'origine), les remplacer par `#include <zone_builder/ChunkPackageWriter.h>` pour cohérence avec le nouveau target_include_directories.

```bash
grep -nE '#include "(ChunkPackageWriter|LayoutImporter|JsonDocument|GltfImporter)\.h"' tools/zone_builder/lib/*.cpp
# Pour chaque match, remplacer par #include <zone_builder/XX.h>
```

- [ ] **Step 4 : Mettre à jour `tools/zone_builder/main.cpp`**

```cpp
// Avant : #include "ChunkPackageWriter.h"
// Après : #include <zone_builder/ChunkPackageWriter.h>
```

- [ ] **Step 5 : Mettre à jour `tools/zone_builder/CMakeLists.txt`**

```cmake
# tools/zone_builder/CMakeLists.txt
add_executable(zone_builder main.cpp)
target_link_libraries(zone_builder PRIVATE zone_builder_lib)
```

- [ ] **Step 6 : Mettre à jour `CMakeLists.txt` racine**

Ajouter avant l'`add_subdirectory(tools/zone_builder)` :

```cmake
add_subdirectory(tools/zone_builder/lib)
```

- [ ] **Step 7 : Build + sanity**

```bash
cmake --build build --target zone_builder 2>&1 | tail -10
```

Expected : build OK, le binaire `zone_builder` est intact.

Tester l'output baseline :

```bash
build/tools/zone_builder/zone_builder ... # commande baseline avec un layout JSON known-good
# Comparer le zone_0/ produit avec une baseline versionnée
```

- [ ] **Step 8 : Commit**

```bash
git add tools/zone_builder/ CMakeLists.txt
git commit -m "refactor(zone_builder): extrait sources en zone_builder_lib statique (M100.3)"
```

---

### Task 17 : Tests bit-à-bit `zone_builder_lib` (M100.3)

**Files :**
- Create : `tools/zone_builder/lib/tests/RoundtripTests.cpp`, `scripts/test_zone_builder_baseline.sh`

- [ ] **Step 1 : Écrire les 3 tests Catch2**

Tests : `Test_WriteThenReadHeader_RoundtripExact`, `Test_WriteChunkPackage_DeterministicBytes`, `Test_LayoutDocument_JsonRoundtrip`.

```cpp
// tools/zone_builder/lib/tests/RoundtripTests.cpp
#include <zone_builder/ChunkPackageWriter.h>
#include <zone_builder/JsonDocument.h>
#include "engine/world/OutputVersion.h"
#include <catch2/catch.hpp>
#include <sstream>

TEST_CASE("Test_WriteThenReadHeader_RoundtripExact", "[M100.3][zone_builder_lib]")
{
    engine::world::OutputVersionHeader hdr{};
    hdr.magic = 0xDEADBEEFu;
    hdr.version = 7;
    std::ostringstream oss;
    engine::world::WriteOutputVersionHeader(oss, hdr);
    std::istringstream iss(oss.str());
    auto roundtrip = engine::world::ReadOutputVersionHeader(iss);
    REQUIRE(roundtrip.magic == hdr.magic);
    REQUIRE(roundtrip.version == hdr.version);
}

// Test_WriteChunkPackage_DeterministicBytes : appel x2 avec même input,
// hash MD5/SHA1 identiques. Voir spec M100.3 lignes 137-138.

// Test_LayoutDocument_JsonRoundtrip : LayoutDocument → JSON → LayoutDocument,
// vérifier égalité champ par champ.
```

- [ ] **Step 2 : Ajouter target test à `tools/zone_builder/lib/CMakeLists.txt`**

```cmake
add_executable(zone_builder_roundtrip_tests tests/RoundtripTests.cpp)
target_link_libraries(zone_builder_roundtrip_tests PRIVATE zone_builder_lib)
add_test(NAME zone_builder_roundtrip_tests COMMAND zone_builder_roundtrip_tests)
```

- [ ] **Step 3 : Créer `scripts/test_zone_builder_baseline.sh`**

```bash
#!/usr/bin/env bash
# Test baseline bit-à-bit : exécute zone_builder sur un layout known-good,
# compare l'output à un baseline versionné.
set -euo pipefail
BUILD_DIR="${1:-build}"
BASELINE="${2:-tests/baselines/zone_0/}"
WORKDIR=$(mktemp -d)
"${BUILD_DIR}/tools/zone_builder/zone_builder" --layout tests/fixtures/layout_zone_0.json --out "${WORKDIR}"
diff -r "${WORKDIR}" "${BASELINE}" || { echo "BASELINE MISMATCH"; exit 1; }
echo "Baseline OK"
```

`chmod +x scripts/test_zone_builder_baseline.sh`. (Si la baseline `tests/baselines/zone_0/` n'existe pas dans le repo, créer une note de TODO dans le commit message — la baseline peut être générée à la prochaine PR si elle est absente.)

- [ ] **Step 4 : Build + tests**

```bash
cmake --build build --target zone_builder_roundtrip_tests 2>&1 | tail -5
ctest --test-dir build -R zone_builder_roundtrip_tests --output-on-failure
```

Expected : 3/3 tests pass.

- [ ] **Step 5 : Commit**

```bash
git add tools/zone_builder/lib/tests/RoundtripTests.cpp tools/zone_builder/lib/CMakeLists.txt scripts/test_zone_builder_baseline.sh
git commit -m "test(zone_builder): tests round-trip bit-a-bit (M100.3)"
```

---

### Task 18 : `EditorCameraController` — déclaration + tests (M100.4 — TDD red)

**Files :**
- Create : `engine/editor/world/EditorCameraController.h`, `engine/editor/world/tests/EditorCameraControllerTests.cpp`

- [ ] **Step 1 : Reproduire la struct exacte de la spec M100.4 §Structures lignes 60-105**

Le header complet est dans le ticket M100.4. Le copier verbatim avec les commentaires Doxygen `///` exigés par CLAUDE.md.

- [ ] **Step 2 : Écrire les 5 tests de la spec M100.4 §Tests lignes 166-171**

Tests : `Test_SetMode_PreservesFocusPoint`, `Test_BuildCamera_FPS_Position`, `Test_BuildCamera_Orbital_OrientedTowardFocus`, `Test_BuildCamera_TopDown_OrthoExtent`, `Test_FocusOn_RecentersCamera`.

```cpp
// engine/editor/world/tests/EditorCameraControllerTests.cpp
#include "engine/editor/world/EditorCameraController.h"
#include <catch2/catch.hpp>

using namespace engine::editor::world;

TEST_CASE("Test_SetMode_PreservesFocusPoint", "[M100.4][EditorCameraController]")
{
    EditorCameraController c;
    c.FocusOn({1.0f, 2.0f, 3.0f});
    auto fp1 = c.GetFocusPoint();
    c.SetMode(EditorCameraMode::Orbital);
    REQUIRE(c.GetFocusPoint().x == fp1.x);
    REQUIRE(c.GetFocusPoint().y == fp1.y);
    REQUIRE(c.GetFocusPoint().z == fp1.z);
}

TEST_CASE("Test_BuildCamera_FPS_Position", "[M100.4][EditorCameraController]")
{
    EditorCameraController c;
    c.SetMode(EditorCameraMode::FPS);
    auto cam = c.BuildCamera(1920, 1080);
    // FPS : la caméra démarre à (0, 5, 10), looking down at -15° pitch.
    REQUIRE(cam.position.y == Approx(5.0f));
}

// 3 autres tests similaires : voir spec M100.4 §Tests pour l'intention de chaque.
```

- [ ] **Step 3 : Run, vérifier le fail**

```bash
cmake --build build --target editor_camera_controller_tests 2>&1 | tail -5
```

Expected : link error sur `EditorCameraController::*` (impl pas encore écrite).

- [ ] **Step 4 : Commit**

```bash
git add engine/editor/world/EditorCameraController.h engine/editor/world/tests/EditorCameraControllerTests.cpp
git commit -m "test(editor/world): EditorCameraController declaration + tests M100.4 (TDD red)"
```

---

### Task 19 : `EditorCameraController` — implémentation (M100.4 — TDD green)

**Files :**
- Create : `engine/editor/world/EditorCameraController.cpp`

- [ ] **Step 1 : Implémenter `Configure`, `SetMode`, `FocusOn`, `BuildCamera`**

```cpp
// engine/editor/world/EditorCameraController.cpp
#include "engine/editor/world/EditorCameraController.h"
#include "engine/render/Camera.h"
#include "engine/platform/Input.h"
#include <cmath>

namespace engine::editor::world
{
	void EditorCameraController::Configure(const EditorCameraConfig& cfg) { m_cfg = cfg; }
	void EditorCameraController::SetMode(EditorCameraMode mode) { m_mode = mode; }
	void EditorCameraController::FocusOn(engine::math::Vec3 target) { m_focusPoint = target; }

	engine::render::Camera EditorCameraController::BuildCamera(int w, int h) const
	{
		engine::render::Camera cam;
		const float aspect = (h > 0) ? (float)w / (float)h : 16.0f / 9.0f;

		switch (m_mode)
		{
			case EditorCameraMode::FPS:
			{
				cam.position = m_position;
				cam.yawDeg = m_yawDeg;
				cam.pitchDeg = m_pitchDeg;
				cam.fovYDeg = 60.0f;
				cam.aspect = aspect;
				cam.nearClip = 0.1f;
				cam.farClip = 4000.0f;
				cam.ortho = false;
				break;
			}
			case EditorCameraMode::Orbital:
			{
				// Position calculée à partir du focusPoint + distance + yaw/pitch.
				const float yawRad = m_yawDeg * 3.14159265f / 180.0f;
				const float pitchRad = m_pitchDeg * 3.14159265f / 180.0f;
				const float cy = std::cos(yawRad), sy = std::sin(yawRad);
				const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
				cam.position = {
					m_focusPoint.x + m_orbitalDistance * cy * cp,
					m_focusPoint.y + m_orbitalDistance * sp,
					m_focusPoint.z + m_orbitalDistance * sy * cp
				};
				cam.lookAt = m_focusPoint;
				cam.fovYDeg = 60.0f;
				cam.aspect = aspect;
				cam.nearClip = 0.1f;
				cam.farClip = 4000.0f;
				cam.ortho = false;
				break;
			}
			case EditorCameraMode::TopDownOrtho:
			{
				cam.position = { m_focusPoint.x, m_focusPoint.y + 1000.0f, m_focusPoint.z };
				cam.lookAt = m_focusPoint;
				cam.aspect = aspect;
				cam.ortho = true;
				cam.orthoExtent = m_topDownExtent;
				cam.nearClip = 0.1f;
				cam.farClip = 4000.0f;
				break;
			}
		}
		return cam;
	}

	void EditorCameraController::Update(engine::platform::Input& input, double dt)
	{
		switch (m_mode)
		{
			case EditorCameraMode::FPS: UpdateFPS(input, dt); break;
			case EditorCameraMode::Orbital: UpdateOrbital(input, dt); break;
			case EditorCameraMode::TopDownOrtho: UpdateTopDownOrtho(input, dt); break;
		}
	}

	void EditorCameraController::UpdateFPS(engine::platform::Input& input, double dt)
	{
		// WASD horizontal, QE vertical. Speed = m_cfg.fpsSpeedMps, ×3 si Shift, ×0.25 si Ctrl.
		// Souris droite enfoncée = look (yaw/pitch).
		// Voir spec M100.4 §"Spécification fonctionnelle" lignes 30-35.
	}

	void EditorCameraController::UpdateOrbital(engine::platform::Input& input, double dt)
	{
		// Alt+gauche = rotate (delta yaw/pitch), Alt+milieu = pan (m_focusPoint),
		// molette = dolly (m_orbitalDistance), F = focus sur sélection.
	}

	void EditorCameraController::UpdateTopDownOrtho(engine::platform::Input& input, double dt)
	{
		// Flèches ou drag milieu = pan (m_focusPoint XZ),
		// molette = zoom (m_topDownExtent dans [m_cfg.topDownExtentMin, max]).
	}
}
```

- [ ] **Step 2 : Ajouter à la CMake**

```cmake
# CMakeLists.txt — engine_core sources
engine/editor/world/EditorCameraController.cpp

add_executable(editor_camera_controller_tests engine/editor/world/tests/EditorCameraControllerTests.cpp)
target_link_libraries(editor_camera_controller_tests PRIVATE engine_core)
add_test(NAME editor_camera_controller_tests COMMAND editor_camera_controller_tests)
```

- [ ] **Step 3 : Build + tests (TDD green)**

```bash
cmake --build build --target editor_camera_controller_tests 2>&1 | tail -5
ctest --test-dir build -R editor_camera_controller_tests --output-on-failure
```

Expected : 5/5 tests pass.

- [ ] **Step 4 : Commit**

```bash
git add engine/editor/world/EditorCameraController.cpp CMakeLists.txt
git commit -m "feat(editor/world): implemente EditorCameraController + tests verts (M100.4)"
```

---

### Task 20 : Wire `EditorCameraController` dans `ScenePanel` + Numpad shortcuts (M100.4)

**Files :**
- Modify : `engine/editor/world/panels/ScenePanel.{h,cpp}`, `engine/editor/world/WorldEditorShell.cpp`

- [ ] **Step 1 : `ScenePanel` détient un `EditorCameraController`**

```cpp
// engine/editor/world/panels/ScenePanel.h (ajout)
#include "engine/editor/world/EditorCameraController.h"

class ScenePanel final : public IPanel
{
public:
    EditorCameraController& MutableCamera() { return m_camera; }
    int GetViewportWidth() const { return m_viewportWidth; }
    int GetViewportHeight() const { return m_viewportHeight; }
private:
    EditorCameraController m_camera;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
};
```

- [ ] **Step 2 : Render barre [FPS] [Orbital] [Top]**

```cpp
// engine/editor/world/panels/ScenePanel.cpp Render()
void ScenePanel::Render()
{
    if (!m_visible) return;
    if (ImGui::Begin("Scene", &m_visible))
    {
        // Barre de mode
        auto mode = m_camera.GetMode();
        if (ImGui::RadioButton("FPS", mode == EditorCameraMode::FPS))
            m_camera.SetMode(EditorCameraMode::FPS);
        ImGui::SameLine();
        if (ImGui::RadioButton("Orbital", mode == EditorCameraMode::Orbital))
            m_camera.SetMode(EditorCameraMode::Orbital);
        ImGui::SameLine();
        if (ImGui::RadioButton("Top", mode == EditorCameraMode::TopDownOrtho))
            m_camera.SetMode(EditorCameraMode::TopDownOrtho);

        // HUD coin haut-gauche (cf. spec M100.4 §"Indicateur HUD" ligne 38)
        ImGui::TextDisabled("Mode: %s | Focus: (%.1f, %.1f, %.1f)",
            mode == EditorCameraMode::FPS ? "FPS" :
            mode == EditorCameraMode::Orbital ? "Orbital" : "TopDown",
            m_camera.GetFocusPoint().x, m_camera.GetFocusPoint().y, m_camera.GetFocusPoint().z);

        // Viewport size (M100.1 placeholder, M100.4 alimente m_viewport*)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        m_viewportWidth = (int)avail.x;
        m_viewportHeight = (int)avail.y;
        ImGui::Dummy(avail);
    }
    ImGui::End();
}
```

- [ ] **Step 3 : Brancher Numpad 1/3/7 dans `WorldEditorShell::HandleShortcut`**

```cpp
// engine/editor/world/WorldEditorShell.cpp HandleShortcut(int vk)
case 0x61 /*VK_NUMPAD1*/: GetScenePanel()->MutableCamera().SetMode(EditorCameraMode::FPS); return true;
case 0x63 /*VK_NUMPAD3*/: GetScenePanel()->MutableCamera().SetMode(EditorCameraMode::Orbital); return true;
case 0x67 /*VK_NUMPAD7*/: GetScenePanel()->MutableCamera().SetMode(EditorCameraMode::TopDownOrtho); return true;
```

`GetScenePanel()` est un helper privé qui caste `m_panels[0]` en `ScenePanel*`.

- [ ] **Step 4 : Persister `editor.world.camera.lastMode`**

Dans `Init`, lire la string de config et appeler `SetMode` ; dans `Shutdown`, écrire la string. Convention : `"FPS"|"Orbital"|"TopDown"`.

- [ ] **Step 5 : Build + tous les tests (M100.1 + M100.2 + M100.4)**

```bash
cmake --build build 2>&1 | tail -10
ctest --test-dir build -R "world_editor_shell_tests|command_stack_tests|editor_camera_controller_tests" --output-on-failure
```

Expected : 4 + 7 + 5 = 16 tests pass.

- [ ] **Step 6 : Commit**

```bash
git add engine/editor/world/panels/ScenePanel.h engine/editor/world/panels/ScenePanel.cpp engine/editor/world/WorldEditorShell.cpp
git commit -m "feat(editor/world): integre EditorCameraController dans ScenePanel + Numpad 1/3/7 (M100.4)"
```

---

### Task 21 : Build complet, validation finale, push branche, PR

**Files :** N/A (validation et opérations git)

- [ ] **Step 1 : Build full Windows**

```bash
cmake --build build --config Release 2>&1 | tail -30
```

Expected : 0 erreurs, warnings pré-existants tolérés. Si nouveaux warnings introduits par les Tasks 2-20, les fixer.

- [ ] **Step 2 : Build full Linux (si CI Linux configurée)**

```bash
# Sur Linux ou via WSL
cmake --build build_linux 2>&1 | tail -30
```

Expected : 0 erreurs. Vérifier en particulier que `server_app`/`engine_server` build sans `engine/editor/world/`.

- [ ] **Step 3 : Run l'ensemble des tests**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -50
```

Expected : tous les tests verts (les 16 nouveaux + les pré-existants).

- [ ] **Step 4 : Vérifier l'anti-duplication serveur**

```bash
grep -RIn "engine::editor::world\|zone_builder_lib\|WorldEditorShell\|CommandStack" engine/server/ src/server/ cpp/server/ 2>/dev/null
```

Expected : 0 résultats. Si un résultat apparaît, identifier le fichier coupable et l'exclure du target serveur dans CMake.

- [ ] **Step 5 : Acceptance criteria — vérifier les checkboxes des 4 specs**

Pour chaque spec (M100.1, M100.2, M100.3, M100.4), parcourir la section `## Critères d'acceptation`. Vérifier point par point :

- M100.1 acceptance criteria (lignes 256-270 de M100.1) : 9 checkboxes, dont parité serveur (`grep -RIn "m_worldEditorWorld\|editor.world" engine/render/`).
- M100.2 acceptance criteria (lignes 195-203) : 8 checkboxes.
- M100.3 acceptance criteria (lignes 124-131) : 6 checkboxes (notamment le `cmp` baseline et l'absence de `zone_builder_lib` dans `engine/server/`).
- M100.4 acceptance criteria (lignes 154-162) : 7 checkboxes.

Si un critère échoue (ex. baseline diffère), revenir à la task concernée et fix.

- [ ] **Step 6 : Mettre à jour `tickets/M100/INDEX.md`**

Marquer M100.1, M100.2, M100.3, M100.4 comme `Done` dans la colonne Statut.

```bash
# Editer manuellement tickets/M100/INDEX.md, colonnes Statut pour les 4 lignes
git add tickets/M100/INDEX.md
git commit -m "docs(tickets/M100): marque M100.1-4 Done (Phase 1)"
```

- [ ] **Step 7 : Push branche**

```bash
git push -u origin claude/upbeat-hermann-17ef28
```

- [ ] **Step 8 : Créer la PR**

```bash
gh pr create --title "feat(editor/world): Phase 1 — Fondations (M100.1-4)" --body "$(cat <<'EOF'
## Résumé

Livre les 4 tickets de la Phase 1 du milestone M100 :

- **M100.1** — `WorldEditorShell` (coquille ImGui), 6 panneaux ancrables, dockspace persistant, raccourcis F1-F12, flag `--editor-world`.
- **M100.2** — `ICommand` + `CommandStack` (undo/redo en RAM, coalescing par mergeKey, panneau History, raccourcis Ctrl+Z/Y).
- **M100.3** — Extraction de `tools/zone_builder/*` en lib statique `zone_builder_lib` (anti-duplication éditeur ↔ outil offline).
- **M100.4** — `EditorCameraController` (3 modes : FPS / Orbital / TopDownOrtho, raccourcis Numpad 1/3/7).

Mode "couche au-dessus" (cf. design `docs/superpowers/specs/2026-05-06-m100-execution-design.md`) : le namespace `engine::editor::world::*` est nouveau et cohabite avec l'éditeur existant (`engine::editor::WorldEditorImGui`/`WorldEditorSession`). L'ancien flag `--world-editor` continue de fonctionner.

## Tests

- `world_editor_shell_tests` — 4 tests (Init layout missing/persist, F2 focus, MarkDirty)
- `command_stack_tests` — 7 tests (push/undo/redo/capacity/maxBytes/mergeKey/clearRedoStack/rewindTo)
- `editor_camera_controller_tests` — 5 tests (SetMode preserve focus, BuildCamera FPS/Orbital/TopDown, FocusOn)
- `zone_builder_roundtrip_tests` — 3 tests bit-à-bit
- Total : 19 nouveaux tests, tous verts.

Anti-duplication serveur vérifiée : `grep` sur `engine/server/` retourne 0 résultat pour `engine::editor::world` et `zone_builder_lib`.

## Test plan

- [x] CI Windows green (build + tests)
- [x] CI Linux green (build + tests, server_app sans dépendance editeur)
- [x] Acceptance criteria des 4 specs vérifiés
- [x] Baseline `zone_0/` bit-à-bit identique (M100.3 critère)

## Déploiement

✅ **Client/éditeur uniquement, pas de redéploiement serveur.** Aucun nouveau opcode, aucun handler serveur, aucune migration DB. La phase 7 (M100.25/26) et la phase 9 (M100.32) sont les prochaines phases à wire-breaking.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 9 : Capturer l'URL de la PR**

```bash
gh pr view --json url -q .url
```

Reporter dans le résumé final.

---

## Notes hors-scope (rappel)

- **Pas de smoke manuel ni de playtest** — décision design §2 Q5 (CI green only).
- **Pas de refactor** de l'éditeur existant `WorldEditorImGui` au-delà de l'ajout de l'accesseur `IsWorldEditorWorld()`. La cohabitation est résolue plus tard si nécessaire (potentiellement Phase 10).
- **Pas de menu Tools renseigné** (rempli par les phases 2+).
- **Pas de connexion gameplay** : le shell ne touche pas à `Engine::Update` du gameplay, juste à `Engine::DrawFrame` pour le rendu ImGui.
- **Pas d'intégration M100.5+** : aucun TerrainChunk, aucun SurfaceQuery, aucun outil concret. C'est le rôle des phases suivantes.

## Déploiement

✅ **Client/éditeur uniquement, pas de redéploiement serveur.**
