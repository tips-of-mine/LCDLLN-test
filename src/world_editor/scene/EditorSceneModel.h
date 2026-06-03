#pragma once

#include "src/world_editor/scene/EditorSelection.h"
#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::editor { struct WorldMapEditDocument; }
namespace engine::editor::world::volumes { class MeshInsertDocument; }
namespace engine::editor::world::volumes::dungeons { class DungeonPortalDocument; }

namespace engine::editor::scene
{
	/// Transform éditable d'une entité : position monde (m), rotation Euler XYZ
	/// (degrés), échelle uniforme. Représentation commune à tous les types
	/// d'entités à transform simple (instances de layout, mesh inserts, portails).
	struct EntityTransform
	{
		engine::math::Vec3 position{};
		engine::math::Vec3 eulerDeg{};
		float uniformScale = 1.0f;
	};

	/// Entité agrégée de la scène, pour l'Outliner / l'Inspector (bloc B).
	struct SceneEntity
	{
		EntityId id{};
		std::string label;
		bool hasTransform = false;
		EntityTransform transform{};
	};

	/// Vue agrégée, en lecture, des entités éditables d'une zone (sous-projet 1,
	/// bloc B). Ne possède AUCUNE donnée : référence les documents sources
	/// (layout, mesh inserts, portails de donjon) et reconstruit une liste plate
	/// à la demande via `Rebuild()`.
	///
	/// `EntityId.index` = position dans la liste source du type concerné au
	/// moment du `Rebuild` (non stable après édition structurelle — acceptable
	/// pour un Outliner immediate-mode SP1 qui reconstruit chaque frame ; des
	/// IDs stables par guid relèvent d'un raffinement ultérieur).
	///
	/// Water (lacs/rivières) est volontairement absent ici : géométrie
	/// polygonale sans transform simple, ajouté dans un incrément ultérieur.
	///
	/// Contrainte thread : main thread (comme le reste de l'éditeur).
	class EditorSceneModel
	{
	public:
		/// Lie les documents sources (non possédés ; doivent survivre au modèle).
		/// Un pointeur nul = type non agrégé (utile pour des tests ciblés).
		void Bind(const engine::editor::WorldMapEditDocument* layoutDoc,
			const engine::editor::world::volumes::MeshInsertDocument* meshDoc,
			const engine::editor::world::volumes::dungeons::DungeonPortalDocument* dungeonDoc);

		/// Reconstruit la liste plate des entités depuis les documents liés.
		/// Inclut toujours l'entité implicite `Terrain` en tête (sans transform).
		void Rebuild();

		/// Liste courante (valide jusqu'au prochain `Rebuild`).
		const std::vector<SceneEntity>& Entities() const { return m_entities; }

		/// Retourne l'entité d'`id` donné, ou nullptr si absente.
		const SceneEntity* Find(EntityId id) const;

	private:
		const engine::editor::WorldMapEditDocument* m_layoutDoc = nullptr;
		const engine::editor::world::volumes::MeshInsertDocument* m_meshDoc = nullptr;
		const engine::editor::world::volumes::dungeons::DungeonPortalDocument* m_dungeonDoc = nullptr;
		std::vector<SceneEntity> m_entities;
	};
}
