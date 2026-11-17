// Engine — versions neutres des crochets d'intégration éditeur, pour le JEU.
//
// lcdlln.exe n'embarque pas l'éditeur : ni la bibliothèque engine_editor
// (src/world_editor/*), ni EngineEditor.cpp. Ce fichier, compilé uniquement
// dans engine_app, fournit à la place les fonctions membres d'Engine appelées
// depuis le code commun (Engine.cpp, EngineInit.cpp, EngineUpdateUi.cpp) :
// elles ne font rien, et les options de lancement de l'éditeur sont ignorées
// avec un avertissement (l'éditeur se lance via lcdlln_world_editor.exe).
//
// Chaque crochet était déjà sans effet dans le jeu (gardé par m_worldEditorExe,
// m_editorEnabled ou l'existence de l'état éditeur) : comportement inchangé.
// La documentation de chaque fonction est dans Engine.h.

#include "src/client/app/Engine.h"
#include "src/client/app/EngineHelpers.h"

namespace engine
{
	using namespace engine::helpers;

	// L'état éditeur n'est jamais créé dans le jeu (Engine::Ed() n'y est pas
	// défini) : le pointeur reste nul, rien à libérer.
	void Engine::EditorStateDeleter::operator()(EditorState* state) const noexcept
	{
		(void)state;
	}

	void Engine::ResolveEditorLaunchFlags(int argc, char** argv, bool& worldEditorWorldFlag)
	{
		m_worldEditorExe = false;
		m_editorEnabled = false;
		worldEditorWorldFlag = false;

		const bool editorRequested = HasCliFlag(argc, argv, "--world-editor")
			|| HasCliFlag(argc, argv, "--editor")
			|| HasCliFlag(argc, argv, "--editor-world")
			|| m_cfg.GetBool("editor.enabled", false)
			|| m_cfg.GetBool("editor.world.enabled", false);
		if (editorRequested)
		{
			LOG_WARN(Core,
				"[Boot] Options éditeur ignorées (--editor, --editor-world, --world-editor, "
				"editor.enabled, editor.world.enabled) : l'éditeur n'est pas inclus dans lcdlln.exe, "
				"utiliser lcdlln_world_editor.exe");
		}
	}

	void Engine::InitWorldEditorSession() {}

	void Engine::InitEditorModes(int argc, char** argv, bool worldEditorWorldFlag)
	{
		(void)argc;
		(void)argv;
		(void)worldEditorWorldFlag;
	}

	void Engine::ShutdownEditorViewportTarget() {}

#if defined(_WIN32)
	void Engine::ShutdownEditorUiBeforeImGui() {}

	void Engine::ReleaseWorldEditorImGui() {}
#endif

	void Engine::ShutdownEditorComponents() {}

	void Engine::HandleWorldEditorShortcuts() {}

#if defined(_WIN32)
	void Engine::TickWorldEditor() {}

	void Engine::ResizeWorldEditorViewportTargetIfNeeded() {}

	void Engine::RenderWorldEditorShellIfReady(engine::RenderState& out)
	{
		(void)out;
	}
#endif

	void Engine::CopySceneToEditorViewport(engine::render::FrameResources& fr, VkExtent2D extent)
	{
		(void)fr;
		(void)extent;
	}

#if defined(_WIN32)
	void Engine::InitWorldEditorImGuiPanels() {}
#endif

	bool Engine::HasEditorMode() const
	{
		return false;
	}

	void Engine::UpdateEditorModeFrame(engine::RenderState& out, double dt)
	{
		(void)out;
		(void)dt;
	}

	bool Engine::HasEditorHubImGui() const
	{
		return false;
	}

	bool Engine::TryGetEditorWaterScene(const engine::world::water::WaterScene*& scene, bool& dirty) const
	{
		(void)scene;
		(void)dirty;
		return false;
	}

	bool Engine::ClearEditorWaterDirty()
	{
		return false;
	}

	void Engine::UpdateWorldEditorCamera(engine::RenderState& out, double dt, float mouseSensitivity, bool invertY,
		engine::render::MovementLayout movementLayout)
	{
		(void)out;
		(void)dt;
		(void)mouseSensitivity;
		(void)invertY;
		(void)movementLayout;
	}

#if defined(_WIN32)
	void Engine::UpdateWorldEditorViewportOverlay(engine::RenderState& out)
	{
		(void)out;
	}

	void Engine::ApplyWorldEditorImGuiTheme() {}

	void Engine::BindWorldEditorDayNight() {}

	void Engine::CreateEditorHubImGui() {}

	void Engine::RenderEditorHubImGui(float dw, float dh)
	{
		(void)dw;
		(void)dh;
	}
#endif
}
