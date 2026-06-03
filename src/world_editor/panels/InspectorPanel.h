#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/scene/EditorSceneModel.h"

namespace engine::editor::world::panels
{
	/// Panneau Inspector du shell éditeur monde. Affiche les propriétés de
	/// l'entité actuellement sélectionnée (`EditorSelection`) résolue via
	/// `EditorSceneModel` : type, nom, transform (position / rotation / échelle).
	/// Lecture seule à ce stade (l'édition undoable via `CommandStack` viendra
	/// dans un incrément ultérieur du bloc D). Le modèle et la sélection sont
	/// possédés par le Shell ; pointeurs non possédants.
	class InspectorPanel final : public IPanel
	{
	public:
		/// \param sceneModel vue agrégée des entités (non possédé ; peut être nul).
		/// \param selection état de sélection partagé (non possédé ; peut être nul).
		InspectorPanel(engine::editor::scene::EditorSceneModel* sceneModel,
			engine::editor::scene::EditorSelection* selection)
			: m_sceneModel(sceneModel), m_selection(selection) {}

		const char* GetName() const override { return "Inspector"; }

		/// Rend les propriétés de la sélection courante.
		/// Effet de bord : crée une window ImGui nommée "Inspector".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		engine::editor::scene::EditorSceneModel* m_sceneModel = nullptr;
		engine::editor::scene::EditorSelection*  m_selection  = nullptr;
		bool m_visible = true;
	};
}
