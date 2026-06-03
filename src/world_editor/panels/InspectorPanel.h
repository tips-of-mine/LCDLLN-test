#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/scene/EditorSceneModel.h"
#include "src/world_editor/inspector/SetEntityTransformCommand.h"

namespace engine::editor::world::panels
{
	/// Panneau Inspector du shell éditeur monde. Affiche **et édite** les
	/// propriétés de l'entité sélectionnée (`EditorSelection`) résolue via
	/// `EditorSceneModel` : type, nom, transform (position / rotation Y /
	/// échelle). L'édition d'un champ pousse un `SetEntityTransformCommand` sur
	/// la pile (undo/redo) ; le foncteur d'écriture est fourni par le Shell
	/// (installé par l'Engine). Tout est non possédant (pointeurs).
	class InspectorPanel final : public IPanel
	{
	public:
		/// \param sceneModel vue agrégée des entités (non possédé ; peut être nul).
		/// \param selection état de sélection partagé (non possédé ; peut être nul).
		/// \param commandStack pile undo/redo où pousser les éditions (peut être nul).
		/// \param writer foncteur d'écriture de transform (peut être nul / non installé).
		InspectorPanel(engine::editor::scene::EditorSceneModel* sceneModel,
			engine::editor::scene::EditorSelection* selection,
			CommandStack* commandStack,
			SetEntityTransformCommand::Writer* writer)
			: m_sceneModel(sceneModel)
			, m_selection(selection)
			, m_commandStack(commandStack)
			, m_writer(writer) {}

		const char* GetName() const override { return "Inspector"; }

		/// Rend (et permet d'éditer) les propriétés de la sélection courante.
		/// Effet de bord : window ImGui "Inspector" ; au changement d'un champ,
		/// pousse un `SetEntityTransformCommand` sur la pile.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		engine::editor::scene::EditorSceneModel* m_sceneModel = nullptr;
		engine::editor::scene::EditorSelection*  m_selection  = nullptr;
		CommandStack*                            m_commandStack = nullptr;
		SetEntityTransformCommand::Writer*       m_writer       = nullptr;
		bool m_visible = true;
	};
}
