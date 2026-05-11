/// Tests unitaires CPU pour WorldEditorShell (M100.1).
///
/// Ces tests vérifient que la coquille de l'éditeur monde gère correctement
/// son cycle de vie sans dépendre du device Vulkan ni d'une fenêtre native :
///   - Init avec un fichier de layout absent : passe et démarre sur le
///     layout par défaut (ResetLayoutToDefault).
///   - Init + Shutdown : persiste le layout sur disque (ImGui::SaveIniSettings).
///   - HandleShortcut(VK_F2) : marque le panneau Inspector visible et
///     consomme la touche.
///   - MarkDirty : met à jour le flag IsDirty().
///
/// Comme WorldEditorShell::Init / Shutdown / RenderFrame appellent des
/// fonctions ImGui (LoadIniSettingsFromDisk / SaveIniSettingsToDisk /
/// SetWindowFocus), les tests créent un contexte ImGui isolé en
/// préambule (kAutoContext) et le détruisent en fin de test. Aucune
/// passe Vulkan ni device n'est nécessaire pour ces appels (ImGui
/// fonctions de gestion de configuration et focus n'ont pas de backend).
///
/// Ces tests sont Windows-only (la branche WIN32 du root CMakeLists est
/// la seule où ImGui est compilé) — cohérent avec le fait que l'éditeur
/// monde est un binaire Windows / Vulkan.

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/panels/InspectorPanel.h"
#include "src/shared/core/Config.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	/// RAII : crée un contexte ImGui isolé pour la durée d'un test puis le
	/// détruit. ImGui::CreateContext doit précéder tout appel à
	/// ImGui::LoadIniSettingsFromDisk / SaveIniSettingsToDisk / SetWindowFocus.
	struct ImGuiTestContext
	{
#if defined(_WIN32)
		ImGuiContext* ctx = nullptr;
		ImGuiTestContext() {
			ctx = ImGui::CreateContext();
			// ImGui exige IniFilename non nul pour SaveIniSettingsToDisk fonctionne
			// proprement (sinon le fichier est lu/écrit via Save/LoadIniSettings...).
			// Nos tests passent un chemin via WorldEditorShell, on neutralise donc
			// le filename par défaut.
			ImGui::GetIO().IniFilename = nullptr;
		}
		~ImGuiTestContext() {
			if (ctx) ImGui::DestroyContext(ctx);
		}
#else
		ImGuiTestContext() = default;
#endif
	};

	/// Construit un Config rempli avec les clés `editor.world.*` requises par
	/// WorldEditorShell::Init. \p layoutPath est le chemin absolu du .ini de
	/// layout pour ce test, \p worldEnabled active la branche `editor.world`.
	engine::core::Config MakeTestConfig(const std::filesystem::path& layoutPath, bool worldEnabled)
	{
		engine::core::Config cfg;
		cfg.SetValue("editor.world.enabled", engine::core::Config::Value{ worldEnabled });
		cfg.SetValue("editor.world.layout_path", engine::core::Config::Value{ layoutPath.string() });
		return cfg;
	}

	/// Vérifie qu'Init sans fichier .ini ne plante pas et instancie les 7
	/// panneaux dans l'ordre stable Scene/Inspector/AssetBrowser/Outliner/
	/// Console/ToolProperties (6 panneaux M100.1) puis History (M100.2).
	void Test_Init_LoadsDefaultLayout_WhenIniMissing()
	{
		ImGuiTestContext ctxGuard;
		const auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_missing.ini";
		std::error_code ec;
		std::filesystem::remove(tmp, ec);
		auto cfg = MakeTestConfig(tmp, true);
		engine::editor::world::WorldEditorShell shell;
		REQUIRE(shell.Init(cfg));
		REQUIRE(shell.Panels().size() == 7u);
		shell.Shutdown();

		// Best effort cleanup
		std::filesystem::remove(tmp, ec);
	}

	/// Vérifie que Shutdown persiste le layout sur disque (le fichier doit
	/// exister après Shutdown même s'il n'existait pas avant Init).
	void Test_Init_PersistsLayoutOnShutdown()
	{
		ImGuiTestContext ctxGuard;
		const auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_persist.ini";
		std::error_code ec;
		std::filesystem::remove(tmp, ec);
		auto cfg = MakeTestConfig(tmp, true);
		engine::editor::world::WorldEditorShell shell;
		REQUIRE(shell.Init(cfg));
		shell.Shutdown();
		REQUIRE(std::filesystem::exists(tmp));

		std::filesystem::remove(tmp, ec);
	}

	/// Vérifie que HandleShortcut(VK_F2) cible le panneau Inspector
	/// (index 1 dans l'ordre stable) et consomme la touche.
	void Test_HandleShortcut_FocusesPanel()
	{
		ImGuiTestContext ctxGuard;
		const auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_shortcut.ini";
		std::error_code ec;
		std::filesystem::remove(tmp, ec);
		auto cfg = MakeTestConfig(tmp, true);
		engine::editor::world::WorldEditorShell shell;
		REQUIRE(shell.Init(cfg));

		// VK_F2 = 0x71. Doit être consommé et rendre Inspector visible.
		REQUIRE(shell.HandleShortcut(0x71));

		auto* inspector = dynamic_cast<engine::editor::world::panels::InspectorPanel*>(shell.Panels()[1].get());
		REQUIRE(inspector != nullptr);
		REQUIRE(inspector->IsVisible());

		shell.Shutdown();
		std::filesystem::remove(tmp, ec);
	}

	/// Vérifie que MarkDirty met IsDirty() à true (et qu'avant l'appel
	/// IsDirty() == false).
	void Test_MarkDirty_SetsFlag()
	{
		ImGuiTestContext ctxGuard;
		const auto tmp = std::filesystem::temp_directory_path() / "world_editor_test_layout_dirty.ini";
		std::error_code ec;
		std::filesystem::remove(tmp, ec);
		auto cfg = MakeTestConfig(tmp, true);
		engine::editor::world::WorldEditorShell shell;
		REQUIRE(shell.Init(cfg));
		REQUIRE(!shell.IsDirty());
		shell.MarkDirty("test");
		REQUIRE(shell.IsDirty());
		shell.Shutdown();
		std::filesystem::remove(tmp, ec);
	}
}

int main()
{
	Test_Init_LoadsDefaultLayout_WhenIniMissing();
	Test_Init_PersistsLayoutOnShutdown();
	Test_HandleShortcut_FocusesPanel();
	Test_MarkDirty_SetsFlag();

	if (g_failed == 0)
	{
		std::printf("[PASS] WorldEditorShellTests (4/4)\n");
		return 0;
	}
	std::printf("[FAIL] WorldEditorShellTests: %d failure(s)\n", g_failed);
	return 1;
}
