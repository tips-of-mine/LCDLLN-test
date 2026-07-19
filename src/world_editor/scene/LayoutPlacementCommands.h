#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/ui/WorldMapEditDocument.h"

#include <functional>
#include <string>
#include <utility>

namespace engine::editor::scene
{
	/// Roadmap-6 (2026-07-19) — Foncteurs d'édition du document layout pour le
	/// PLACEMENT/DÉPLACEMENT undoable d'instances (props/arbres/rochers).
	/// Installés par l'Engine au point d'usage (capture `[this]` : l'Engine
	/// survit aux commandes dans la pile undo). Toutes les opérations sont
	/// adressées par GUID (chaîne stable du document layout), JAMAIS par index
	/// (instable après édition structurelle) — même règle que `EntityEditOps`.
	struct LayoutPlacementOps
	{
		/// Ajoute l'instance (guid déjà posé) en fin de document.
		/// \return false si le document n'est pas disponible.
		std::function<bool(const engine::editor::WorldMapEditLayoutInstance&)> add;

		/// Retire du document l'instance de guid donné.
		/// \return false si l'instance n'est plus présente.
		std::function<bool(const std::string&)> removeByGuid;

		/// Écrit la position monde (mètres) de l'instance de guid donné.
		/// \return false si l'instance n'est plus présente.
		std::function<bool(const std::string&, double, double, double)> setPositionByGuid;

		/// true si les trois foncteurs sont installés.
		bool IsInstalled() const { return add && removeByGuid && setPositionByGuid; }
	};

	/// Roadmap-6 — Commande undoable de PLACEMENT d'une nouvelle instance de
	/// layout (clic « poser » du mode placement). `Execute` ajoute l'instance
	/// (le guid, généré AVANT la construction, est conservé au Redo) ; `Undo`
	/// la retire par guid.
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class PlaceLayoutInstanceCommand final : public engine::editor::world::ICommand
	{
	public:
		/// \param instance Instance COMPLÈTE à poser (guid déjà généré).
		/// \param ops Foncteurs d'édition (doivent survivre à la commande dans
		///        la pile undo — capture Engine `[this]`).
		PlaceLayoutInstanceCommand(engine::editor::WorldMapEditLayoutInstance instance,
			LayoutPlacementOps ops)
			: m_instance(std::move(instance)), m_ops(std::move(ops)) {}

		const char* GetLabel() const override { return "Placer une instance"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(PlaceLayoutInstanceCommand)
				+ m_instance.guid.capacity()
				+ m_instance.gltfContentRelativePath.capacity()
				+ m_instance.speciesId.capacity();
		}

		void Execute() override { if (m_ops.add) (void)m_ops.add(m_instance); }
		void Undo() override { if (m_ops.removeByGuid) (void)m_ops.removeByGuid(m_instance.guid); }

	private:
		engine::editor::WorldMapEditLayoutInstance m_instance;
		LayoutPlacementOps m_ops;
	};

	/// Roadmap-6 — Commande undoable de DÉPLACEMENT d'une instance de layout
	/// existante (clic « déplacer ici » du mode placement avec une instance
	/// sélectionnée). `Execute` écrit la nouvelle position, `Undo` restaure
	/// l'ancienne — par guid stable. Pas de coalescing (chaque clic est une
	/// étape d'annulation distincte, contrairement au drag de gizmo).
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class MoveLayoutInstanceCommand final : public engine::editor::world::ICommand
	{
	public:
		/// \param guid Instance ciblée (chaîne stable du document layout).
		/// \param oldX,oldY,oldZ Position monde AVANT (restaurée au Undo), en mètres.
		/// \param newX,newY,newZ Position monde APRÈS (appliquée au Execute), en mètres.
		MoveLayoutInstanceCommand(std::string guid,
			double oldX, double oldY, double oldZ,
			double newX, double newY, double newZ,
			LayoutPlacementOps ops)
			: m_guid(std::move(guid))
			, m_oldX(oldX), m_oldY(oldY), m_oldZ(oldZ)
			, m_newX(newX), m_newY(newY), m_newZ(newZ)
			, m_ops(std::move(ops)) {}

		const char* GetLabel() const override { return "Déplacer une instance"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(MoveLayoutInstanceCommand) + m_guid.capacity();
		}

		void Execute() override
		{
			if (m_ops.setPositionByGuid) (void)m_ops.setPositionByGuid(m_guid, m_newX, m_newY, m_newZ);
		}
		void Undo() override
		{
			if (m_ops.setPositionByGuid) (void)m_ops.setPositionByGuid(m_guid, m_oldX, m_oldY, m_oldZ);
		}

	private:
		std::string m_guid;
		double m_oldX, m_oldY, m_oldZ;
		double m_newX, m_newY, m_newZ;
		LayoutPlacementOps m_ops;
	};
}
