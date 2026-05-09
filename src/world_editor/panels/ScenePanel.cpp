#include "src/world_editor/world/panels/ScenePanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Convertit un `EditorCameraMode` en libellé court affiché dans le HUD
	/// du panneau Scene. Ne dépend pas d'ImGui ; reste utilisable hors WIN32.
	static const char* CameraModeLabel(engine::editor::world::EditorCameraMode mode)
	{
		switch (mode)
		{
			case engine::editor::world::EditorCameraMode::FPS:          return "FPS";
			case engine::editor::world::EditorCameraMode::Orbital:      return "Orbital";
			case engine::editor::world::EditorCameraMode::TopDownOrtho: return "TopDown";
		}
		return "?";
	}

	/// Affiche la barre de mode caméra, le HUD focus puis le placeholder
	/// viewport. M100.4 : ajoute trois RadioButton qui reflètent le mode
	/// courant et permettent le toggle souris (équivalent Numpad 1/3/7).
	/// Capture la taille courante de la zone client pour que le rendu
	/// offscreen (M100.34) puisse y attacher son image.
	void ScenePanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Scene", &m_visible))
		{
			// Barre de mode caméra (cf. spec M100.4 §"Spécification fonctionnelle"
			// ligne 36 : "Bouton dans la barre du panneau Scene : [FPS] [Orbital] [Top]").
			const auto mode = m_camera.GetMode();
			if (ImGui::RadioButton("FPS", mode == engine::editor::world::EditorCameraMode::FPS))
				m_camera.SetMode(engine::editor::world::EditorCameraMode::FPS);
			ImGui::SameLine();
			if (ImGui::RadioButton("Orbital", mode == engine::editor::world::EditorCameraMode::Orbital))
				m_camera.SetMode(engine::editor::world::EditorCameraMode::Orbital);
			ImGui::SameLine();
			if (ImGui::RadioButton("Top", mode == engine::editor::world::EditorCameraMode::TopDownOrtho))
				m_camera.SetMode(engine::editor::world::EditorCameraMode::TopDownOrtho);

			// HUD coin haut-gauche (cf. spec M100.4 §"Indicateur HUD" ligne 38).
			const auto fp = m_camera.GetFocusPoint();
			ImGui::TextDisabled("Mode: %s | Focus: (%.1f, %.1f, %.1f)",
				CameraModeLabel(m_camera.GetMode()), fp.x, fp.y, fp.z);

			// Met à jour la taille du viewport — consommée par M100.4 (caméra,
			// pour le calcul d'aspect dans BuildCamera) et M100.34 (rendu).
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			m_viewportWidth = static_cast<int>(avail.x);
			m_viewportHeight = static_cast<int>(avail.y);
			ImGui::TextDisabled("Scene viewport — placeholder M100.1.");
			ImGui::TextWrapped(
				"La vue 3D principale du monde en édition apparaîtra ici "
				"à partir de M100.34 (integration rendu offscreen).");
			ImGui::Text("Viewport courant : %d x %d px", m_viewportWidth, m_viewportHeight);
		}
		ImGui::End();
#endif
	}
}
