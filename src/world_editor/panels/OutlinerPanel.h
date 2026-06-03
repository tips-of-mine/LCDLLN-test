#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/scene/EditorSceneModel.h"

namespace engine::editor::world::panels
{
	/// Panneau Outliner du shell éditeur monde. Affiche la liste des entités de
	/// la zone (Terrain, instances de layout, volumes, portails de donjon)
	/// agrégées par `EditorSceneModel`, groupées par type, et permet de
	/// sélectionner une entité (met à jour l'`EditorSelection` partagé consommé
	/// par l'Inspector). Le modèle et la sélection sont possédés par le Shell ;
	/// ce panneau ne fait que les lire/écrire via des pointeurs non possédants.
	class OutlinerPanel final : public IPanel
	{
	public:
		/// \param sceneModel vue agrégée des entités (non possédé ; peut être nul).
		/// \param selection état de sélection partagé (non possédé ; peut être nul).
		OutlinerPanel(engine::editor::scene::EditorSceneModel* sceneModel,
			engine::editor::scene::EditorSelection* selection)
			: m_sceneModel(sceneModel), m_selection(selection) {}

		const char* GetName() const override { return "Outliner"; }

		/// Rend la liste des entités groupée par type, avec sélection.
		/// Effet de bord : crée une window ImGui nommée "Outliner" + modifie
		/// l'`EditorSelection` partagé au clic sur un élément.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		/// Rend un en-tête repliable listant toutes les entités du `kind` donné
		/// (Selectable → met à jour la sélection partagée ; surbrillance sur
		/// l'entité courante). No-op hors Windows / sans modèle. Effet de bord :
		/// ImGui state.
		void RenderKindGroup(const char* header, engine::editor::scene::EntityKind kind);

		engine::editor::scene::EditorSceneModel* m_sceneModel = nullptr;
		engine::editor::scene::EditorSelection*  m_selection  = nullptr;
		bool m_visible = true;
	};
}
