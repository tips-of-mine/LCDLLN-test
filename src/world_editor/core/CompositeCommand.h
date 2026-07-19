#pragma once

#include "src/world_editor/core/CommandStack.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine::editor::world
{
	/// Roadmap-6 (2026-07-19) — Commande composite : regroupe N commandes
	/// enfants en UNE seule étape d'historique undo/redo. Utilisée par les
	/// opérations multi-sélection (Dupliquer/Supprimer N entités, drag de
	/// gizmo sur N entités) pour qu'un seul Ctrl+Z annule tout le geste.
	///
	/// `Execute` exécute les enfants dans l'ordre d'AJOUT ; `Undo` les annule
	/// dans l'ordre INVERSE (le dernier exécuté est le premier annulé), comme
	/// une transaction. Le `mergeKey` reste 0 (défaut ICommand) : deux gestes
	/// composites successifs ne fusionnent JAMAIS entre eux — chaque geste est
	/// une étape d'annulation distincte.
	///
	/// L'appelant est responsable de l'ORDRE des enfants quand il importe
	/// (ex. suppressions par index décroissant pour que la capture du 1er
	/// Execute ne soit pas invalidée par les retraits précédents).
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class CompositeCommand final : public ICommand
	{
	public:
		/// \param label Libellé affiché dans l'historique (copié ; ex.
		///        "Supprimer 3 entités"). Vie aussi longue que la commande.
		explicit CompositeCommand(std::string label)
			: m_label(std::move(label)) {}

		/// Ajoute une commande enfant (exécutée en ordre d'ajout au Execute,
		/// en ordre inverse au Undo). À appeler AVANT le Push sur la pile.
		void AddChild(std::unique_ptr<ICommand> child)
		{
			if (child) m_children.push_back(std::move(child));
		}

		/// true si aucune commande enfant (ne rien pousser dans ce cas).
		bool Empty() const { return m_children.empty(); }

		/// Nombre de commandes enfants (tests, libellés).
		size_t ChildCount() const { return m_children.size(); }

		const char* GetLabel() const override { return m_label.c_str(); }

		/// Empreinte mémoire = structure + libellé + somme des enfants (pour
		/// l'éviction par budget mémoire du CommandStack).
		size_t GetMemoryFootprint() const override
		{
			size_t total = sizeof(CompositeCommand) + m_label.capacity();
			for (const auto& c : m_children) total += c->GetMemoryFootprint();
			return total;
		}

		/// Exécute tous les enfants dans l'ordre d'ajout.
		void Execute() override
		{
			for (auto& c : m_children) c->Execute();
		}

		/// Annule tous les enfants dans l'ordre inverse d'ajout.
		void Undo() override
		{
			for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
				(*it)->Undo();
		}

	private:
		std::string m_label;
		std::vector<std::unique_ptr<ICommand>> m_children;
	};
}
