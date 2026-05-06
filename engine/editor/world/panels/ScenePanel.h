#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Scene du shell éditeur monde (M100.1 — placeholder).
	/// Contiendra la vue 3D principale du monde en édition à partir de
	/// M100.4 (caméra) et M100.34 (intégration rendu offscreen).
	///
	/// Les dimensions de viewport sont exposées pour permettre à
	/// EditorCameraController (M100.4) et au pipeline de rendu offscreen
	/// (M100.34) de redimensionner leurs ressources GPU.
	class ScenePanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Scene"; }

		/// Rend le placeholder texte. À M100.4 : intègre l'image rendue
		/// offscreen via ImGui::Image. Effet de bord : crée une window ImGui
		/// nommée "Scene" et met à jour les membres m_viewportWidth/Height.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Largeur courante de la zone client du panneau, en pixels.
		/// Mise à jour à chaque Render(). Vaut 0 avant la première frame.
		/// Consommé par M100.4 (EditorCameraController) et M100.34 (rendu).
		int GetViewportWidth() const { return m_viewportWidth; }

		/// Hauteur courante de la zone client du panneau, en pixels.
		int GetViewportHeight() const { return m_viewportHeight; }

	private:
		bool m_visible = true;
		int m_viewportWidth = 0;
		int m_viewportHeight = 0;
	};
}
