#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/scene/EditorSceneModel.h" // scene::EntityId, scene::EntityTransform

#include <functional>

namespace engine::editor::world
{
	/// Commande undoable de modification du transform d'une entité (sous-projet 1,
	/// bloc D). Découplée du document concret : l'écriture passe par un foncteur
	/// `Writer` fourni à la construction (capture, côté Engine, les documents
	/// mutables et applique le transform à la bonne entité par `EntityId`).
	/// `Execute` applique le nouveau transform, `Undo` restaure l'ancien.
	///
	/// Coalescing : les éditions consécutives de la **même** entité (un même
	/// drag de slider) fusionnent via `TryMerge` — le `mergeKey` encode
	/// l'`EntityId`. Résultat : un seul élément d'historique par geste, tout en
	/// gardant l'`m_old` initial pour un Undo complet du geste.
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class SetEntityTransformCommand final : public ICommand
	{
	public:
		/// Foncteur d'écriture : applique `transform` à l'entité `id` dans le
		/// document concret. Doit rester valide tant que la commande vit dans la
		/// pile undo (capture typiquement des pointeurs de documents stables).
		using Writer = std::function<void(scene::EntityId, const scene::EntityTransform&)>;

		SetEntityTransformCommand(scene::EntityId id,
			const scene::EntityTransform& oldT,
			const scene::EntityTransform& newT,
			Writer writer);

		const char* GetLabel() const override { return "Modifier transform"; }
		size_t GetMemoryFootprint() const override { return sizeof(SetEntityTransformCommand); }
		CommandMergeKey GetMergeKey() const override { return m_mergeKey; }
		void Execute() override;
		void Undo() override;
		bool TryMerge(const ICommand& other) override;

	private:
		scene::EntityId        m_id;
		scene::EntityTransform m_old;
		scene::EntityTransform m_new;
		Writer                 m_writer;
		CommandMergeKey        m_mergeKey = 0;
	};
}
