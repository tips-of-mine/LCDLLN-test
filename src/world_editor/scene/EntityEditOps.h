#pragma once

#include "src/world_editor/scene/EditorSelection.h"
#include "src/world_editor/ui/WorldMapEditDocument.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <functional>

namespace engine::editor::scene
{
	/// Lot 5 (2026-07-18) — Instantané complet d'une entité de scène
	/// supprimable/duplicable. Capturé AVANT une suppression (pour pouvoir la
	/// réinsérer au Undo) ou APRÈS une duplication (pour pouvoir retirer la
	/// copie au Undo, et la réinsérer à l'identique au Redo). Un seul des
	/// trois membres « payload » est significatif, discriminé par `kind` :
	///   - LayoutInstance → `layout` (+ `layoutIndex`, position d'origine dans
	///     `WorldMapEditDocument::layoutInstances` pour réinsérer au même rang) ;
	///   - MeshInsert     → `meshInsert` (guid uint64 stable) ;
	///   - DungeonPortal  → `portal` (guid uint64 stable).
	struct EntitySnapshot
	{
		EntityKind kind = EntityKind::None;

		engine::editor::WorldMapEditLayoutInstance layout{};
		/// Position d'origine dans `layoutInstances` (réinsertion au même rang
		/// par `restore` — bornée à la taille courante du vecteur).
		uint32_t layoutIndex = 0u;

		engine::editor::world::volumes::MeshInsertInstance meshInsert{};
		engine::editor::world::volumes::dungeons::DungeonPortalInstance portal{};
	};

	/// Lot 5 (2026-07-18) — Foncteurs d'édition STRUCTURELLE des entités de
	/// scène (suppression / réinsertion / duplication), installés par l'Engine
	/// sur le shell (même pattern que `WorldEditorShell::TransformWriter` :
	/// l'Engine capture les documents mutables — session layout, mesh inserts,
	/// portails — que le shell ne possède pas tous). Consommés par
	/// `DeleteEntityCommand` / `DuplicateEntityCommand`.
	///
	/// Contrainte thread : main thread (comme tout `ICommand`). Les foncteurs
	/// doivent rester valides tant que des commandes vivent dans la pile undo
	/// (capture typique : `[this]` Engine, qui survit à l'éditeur).
	struct EntityEditOps
	{
		/// Copie l'entité `id` (kind + index de scène courant) dans `out`,
		/// SANS la retirer du document. \return false si l'entité n'existe pas
		/// ou n'est pas d'un kind éditable (Terrain / Water / None).
		std::function<bool(EntityId, EntitySnapshot&)> capture;

		/// Retire du document l'entité décrite par le snapshot — par guid
		/// (stable), jamais par index (instable après édition structurelle).
		/// \return false si l'entité n'est plus présente.
		std::function<bool(const EntitySnapshot&)> remove;

		/// Réinsère le snapshot dans son document : au rang `layoutIndex`
		/// (borné) pour LayoutInstance, par `Add` (guid conservé) pour les
		/// volumes. \return false si le document cible n'est pas disponible.
		std::function<bool(const EntitySnapshot&)> restore;

		/// Ajoute au document une COPIE du snapshot `src` avec un nouveau guid
		/// et un léger décalage de position (visibilité de la copie), et écrit
		/// dans `outCopy` le snapshot de la copie créée (pour Undo/Redo).
		/// \return false si le document cible n'est pas disponible.
		std::function<bool(const EntitySnapshot&, EntitySnapshot&)> duplicate;

		/// true si les quatre foncteurs sont installés (l'Engine a câblé les
		/// documents) — condition d'activation des actions Dupliquer/Supprimer.
		bool IsInstalled() const { return capture && remove && restore && duplicate; }
	};
}
