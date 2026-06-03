#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace engine::editor::scene
{
	/// Type d'entité éditable d'une zone (sous-projet 1, bloc B). Discrimine la
	/// cible d'une sélection dans l'éditeur monde. L'ordre/les valeurs sont
	/// stables (sérialisation éventuelle, switch UI).
	enum class EntityKind : uint8_t
	{
		None           = 0, ///< Aucune sélection.
		Terrain        = 1, ///< Le terrain de la zone (entité implicite unique).
		Water          = 2, ///< Un plan d'eau (lac / rivière / océan).
		MeshInsert     = 3, ///< Un volume inséré (grotte / arche / surplomb).
		DungeonPortal  = 4, ///< Un portail de donjon.
		LayoutInstance = 5, ///< Une instance de layout (prop / arbre).
	};

	/// Identifiant stable d'une entité de scène : `(kind, index)` où `index`
	/// désigne la position dans le document du type concerné. `index` est
	/// ignoré pour `Terrain` et `None`.
	struct EntityId
	{
		EntityKind kind  = EntityKind::None;
		uint32_t   index = 0u;

		bool operator==(const EntityId& o) const { return kind == o.kind && index == o.index; }
		bool operator!=(const EntityId& o) const { return !(*this == o); }
	};

	/// État de sélection partagé de l'éditeur monde (bloc B). Possédé par
	/// `WorldEditorShell`, référencé par les panneaux (Outliner / Inspector) et
	/// par le picking viewport. Notifie un unique observateur à chaque
	/// changement **effectif** de sélection (pas de notification si la cible est
	/// déjà sélectionnée).
	///
	/// Contrainte thread : main thread (comme le reste de l'éditeur ; ImGui
	/// n'est pas thread-safe). Pur CPU, aucune dépendance Vulkan/ImGui.
	class EditorSelection
	{
	public:
		using OnChangedCallback = std::function<void(EntityId)>;

		/// Sélectionne `id`. No-op (aucune notification) si `id` est déjà la
		/// sélection courante.
		/// Effet de bord : invoque le callback OnChanged si la sélection change.
		void Select(EntityId id);

		/// Vide la sélection (kind = None). No-op si déjà vide.
		/// Effet de bord : invoque le callback OnChanged si la sélection change.
		void Clear();

		/// Sélection courante (kind = None si aucune).
		EntityId Current() const { return m_current; }

		/// true si une entité est sélectionnée (kind != None).
		bool HasSelection() const { return m_current.kind != EntityKind::None; }

		/// Installe l'observateur de changement (un seul ; remplace le précédent).
		void SetOnChanged(OnChangedCallback cb) { m_onChanged = std::move(cb); }

	private:
		EntityId m_current{};
		OnChangedCallback m_onChanged;
	};
}
