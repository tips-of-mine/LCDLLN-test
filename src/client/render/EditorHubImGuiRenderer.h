#pragma once

namespace engine::editor { class EditorMode; }

namespace engine::render
{
	/// M43.4 — Panneau "Editor Hub" overlay pour le mode `--editor`.
	///
	/// Remplace l'affichage dans le titre de la fenêtre par un panneau ImGui non-modal
	/// ancré en haut-gauche. Lit `EditorMode::GetHubTitle()` (déjà composé par
	/// `RefreshShell`) et `EditorMode::IsDirty()`.
	///
	/// Windows uniquement — le contexte ImGui est porté par `WorldEditorImGui` qui
	/// n'existe que sous WIN32 (cf. `ChatImGuiRenderer` pour le pattern).
	///
	/// Pas de mutation de l'état éditeur : panneau strictement read-only.
	class EditorHubImGuiRenderer final
	{
	public:
		void BindEditorMode(engine::editor::EditorMode* editor) { m_editor = editor; }
		void Render(float viewportW, float viewportH);

	private:
		engine::editor::EditorMode* m_editor = nullptr;
	};
}
