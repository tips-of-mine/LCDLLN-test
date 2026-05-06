#include "engine/editor/world/WorldEditorShell.h"

#include "engine/editor/world/panels/ScenePanel.h"
#include "engine/editor/world/panels/InspectorPanel.h"
#include "engine/editor/world/panels/AssetBrowserPanel.h"
#include "engine/editor/world/panels/OutlinerPanel.h"
#include "engine/editor/world/panels/ConsolePanel.h"
#include "engine/editor/world/panels/ToolPropertiesPanel.h"
#include "engine/editor/world/panels/HistoryPanel.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <filesystem>
#include <string>

namespace engine::editor::world
{
	/// Initialise la coquille : lit `editor.world.layout_path`, instancie les
	/// 6 panneaux dans l'ordre stable, charge le fichier .ini de layout s'il
	/// existe, sinon réinitialise un layout par défaut. L'ordre des panneaux
	/// est figé : 0=Scene, 1=Inspector, 2=AssetBrowser, 3=Outliner, 4=Console,
	/// 5=ToolProperties — référencé par les tests M100.1 et HistoryPanel
	/// (M100.2).
	bool WorldEditorShell::Init(const engine::core::Config& cfg)
	{
		m_layoutPath = cfg.GetString("editor.world.layout_path", "editor_world_layout.ini");

		// M100.2 — Configure la pile undo/redo depuis config.json. Defaults :
		// 256 commandes, 256 MiB. Le cast est sûr : capacity et maxBytes sont
		// stockés en size_t mais Config::GetInt renvoie int64_t.
		CommandStackConfig csCfg;
		csCfg.capacity = static_cast<size_t>(
			cfg.GetInt("editor.world.undo.capacity", 256));
		csCfg.maxBytes = static_cast<size_t>(
			cfg.GetInt("editor.world.undo.maxBytes", 256ll * 1024ll * 1024ll));
		m_commandStack.Configure(csCfg);

		m_panels.clear();
		m_panels.emplace_back(std::make_unique<panels::ScenePanel>());
		m_panels.emplace_back(std::make_unique<panels::InspectorPanel>());
		m_panels.emplace_back(std::make_unique<panels::AssetBrowserPanel>());
		m_panels.emplace_back(std::make_unique<panels::OutlinerPanel>());
		m_panels.emplace_back(std::make_unique<panels::ConsolePanel>());
		m_panels.emplace_back(std::make_unique<panels::ToolPropertiesPanel>());
		// M100.2 — Insère HistoryPanel après les 6 panneaux M100.1. L'ordre
		// devient : [Scene=0, Inspector=1, AssetBrowser=2, Outliner=3,
		//            Console=4, ToolProperties=5, History=6].
		m_panels.emplace_back(std::make_unique<panels::HistoryPanel>(&m_commandStack));

#if defined(_WIN32)
		std::error_code ec;
		if (std::filesystem::exists(m_layoutPath, ec) && !ec)
		{
			ImGui::LoadIniSettingsFromDisk(m_layoutPath.c_str());
		}
		else
		{
			ResetLayoutToDefault();
		}
#else
		ResetLayoutToDefault();
#endif

		m_initialized = true;
		LOG_INFO(EditorWorld, "WorldEditorShell init OK, {} panels, layout='{}'",
			m_panels.size(), m_layoutPath);
		return true;
	}

	/// Persiste le layout sur disque puis libère les panneaux. Idempotent.
	void WorldEditorShell::Shutdown()
	{
		if (!m_initialized) return;
		EnsureLayoutPersisted();
		m_panels.clear();
		m_initialized = false;
		LOG_INFO(EditorWorld, "WorldEditorShell shutdown");
	}

	/// Sauvegarde l'état dock courant d'ImGui dans `m_layoutPath`.
	/// Effet de bord : écriture fichier disque.
	void WorldEditorShell::EnsureLayoutPersisted()
	{
#if defined(_WIN32)
		if (m_layoutPath.empty()) return;
		ImGui::SaveIniSettingsToDisk(m_layoutPath.c_str());
#endif
	}

	/// Rend le menu bar, le dockspace, puis chaque panneau visible. Doit
	/// être appelé après ImGui::NewFrame et avant ImGui::Render. Si Init n'a
	/// pas été appelé avec succès, no-op.
	void WorldEditorShell::RenderFrame()
	{
		if (!m_initialized) return;
#if defined(_WIN32)
		RenderMenuBar();
		RenderDockspace();
		for (auto& panel : m_panels)
		{
			if (panel && panel->IsVisible())
			{
				panel->Render();
			}
		}
#endif
	}

	/// Rend la barre de menu File/Edit/View/Tools/Window/Help. M100.1 : la
	/// plupart des items sont des stubs no-op qui loguent leur déclenchement.
	/// File/New/Open/Save sont stubs jusqu'aux tickets save/load. Edit/Undo
	/// /Redo grisés (activés en M100.2). View propose le toggle de chaque
	/// panneau et Reset Layout. Tools vide. Window expose 4 layouts pré-
	/// définis (3 identiques au Default en M100.1). Help/About : version
	/// figée pour l'instant.
	void WorldEditorShell::RenderMenuBar()
	{
#if defined(_WIN32)
		if (!ImGui::BeginMainMenuBar()) return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New", nullptr, false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/New (no-op M100.1, save/load à venir)");
			}
			if (ImGui::MenuItem("Open", nullptr, false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/Open (no-op M100.1, save/load à venir)");
			}
			if (ImGui::MenuItem("Save", "Ctrl+S", false, true))
			{
				MarkDirty("File/Save no-op M100.1");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Ctrl+Q", false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/Exit (no-op M100.1, dialog à venir)");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			// M100.2 — Undo/Redo branchés sur le CommandStack. Les items sont
			// grisés quand la pile correspondante est vide.
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_commandStack.CanUndo()))
			{
				m_commandStack.Undo();
			}
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_commandStack.CanRedo()))
			{
				m_commandStack.Redo();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			for (auto& panel : m_panels)
			{
				if (!panel) continue;
				bool visible = panel->IsVisible();
				if (ImGui::MenuItem(panel->GetName(), nullptr, &visible))
				{
					panel->SetVisible(visible);
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu View/Reset Layout");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools"))
		{
			ImGui::TextDisabled("(à remplir par les tickets outils)");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			// Les 3 derniers layouts sont identiques au Default en M100.1.
			if (ImGui::MenuItem("Default Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Default Layout");
			}
			if (ImGui::MenuItem("Sculpting Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Sculpting Layout (alias Default M100.1)");
			}
			if (ImGui::MenuItem("Painting Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Painting Layout (alias Default M100.1)");
			}
			if (ImGui::MenuItem("Placement Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Placement Layout (alias Default M100.1)");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::Text("LCDLLN World Editor — M100.1 bootstrap");
			ImGui::Separator();
			ImGui::TextDisabled("Version + commit hash : à intégrer dans un ticket dédié.");
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
#endif
	}

	/// Crée le dockspace plein écran qui héberge tous les panneaux. Utilise
	/// le viewport principal d'ImGui.
	void WorldEditorShell::RenderDockspace()
	{
#if defined(_WIN32)
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("WorldEditorDockSpace", nullptr, flags);
		ImGui::PopStyleVar(3);
		const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceId");
		ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::End();
#endif
	}

	/// Mapping VK_F1..VK_F4, F6, F12 → panel index dans m_panels (ordre
	/// stable Scene/Inspector/AssetBrowser/Outliner/Console/ToolProperties).
	/// F5 (playtest) et F11 (fullscreen) sont consommés mais no-op M100.1.
	bool WorldEditorShell::HandleShortcut(int virtualKey)
	{
		int panelIndex = -1;
		switch (virtualKey)
		{
			case 0x70: panelIndex = 0; break;  // VK_F1  → Scene
			case 0x71: panelIndex = 1; break;  // VK_F2  → Inspector
			case 0x72: panelIndex = 2; break;  // VK_F3  → Asset Browser
			case 0x73: panelIndex = 3; break;  // VK_F4  → Outliner
			case 0x75: panelIndex = 4; break;  // VK_F6  → Console
			case 0x7B: panelIndex = 5; break;  // VK_F12 → Tool Properties
			case 0x74:                          // VK_F5  → Réservé Playtest M100.33
				LOG_INFO(EditorWorld, "F5 (playtest) — réservé M100.33, no-op");
				return true;
			case 0x7A:                          // VK_F11 → Toggle plein écran
				LOG_INFO(EditorWorld, "F11 (fullscreen toggle) — no-op M100.1");
				return true;
			default:
				return false;
		}

		if (panelIndex >= 0 && panelIndex < static_cast<int>(m_panels.size()))
		{
			m_panels[panelIndex]->SetVisible(true);
#if defined(_WIN32)
			ImGui::SetWindowFocus(m_panels[panelIndex]->GetName());
#endif
			return true;
		}
		return false;
	}

	/// M100.2 — Surcharge avec modifiers : intercepte Ctrl+Z, Ctrl+Shift+Z,
	/// Ctrl+Y avant de déléguer à la version sans modifiers (F1..F12).
	/// Logge le déclenchement undo/redo en EditorWorld pour faciliter le
	/// debug "où est passée mon undo ?". 'Z' et 'Y' sont les codes ASCII
	/// majuscule, qui correspondent aux VK_* Win32 standards.
	bool WorldEditorShell::HandleShortcut(int virtualKey, bool ctrl, bool shift)
	{
		if (ctrl && virtualKey == 'Z' && !shift)
		{
			m_commandStack.Undo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Z -> Undo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		if (ctrl && virtualKey == 'Z' && shift)
		{
			m_commandStack.Redo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Shift+Z -> Redo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		if (ctrl && virtualKey == 'Y')
		{
			m_commandStack.Redo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Y -> Redo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		return HandleShortcut(virtualKey);
	}

	/// Marque le document éditeur comme modifié et logge la raison.
	void WorldEditorShell::MarkDirty(std::string_view reason)
	{
		m_dirty = true;
		// std::format ne supporte pas string_view via {} directement avant C++23
		// pour certains compilers ; on convertit en std::string pour être large.
		LOG_INFO(EditorWorld, "WorldEditorShell dirty: {}", std::string(reason));
	}

	/// Réinitialise un layout par défaut minimal : rend tous les panneaux
	/// visibles. La disposition fine (Outliner | Scene | Inspector / Asset
	/// Browser | Tool Properties | Console) sera implémentée via ImGui
	/// DockBuilder dans un ticket d'intégration ultérieur — les tests
	/// M100.1 n'observent que la persistance du .ini (pas la disposition).
	void WorldEditorShell::ResetLayoutToDefault()
	{
		for (auto& panel : m_panels)
		{
			if (panel) panel->SetVisible(true);
		}
	}
}
