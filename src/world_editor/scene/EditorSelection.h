#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

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
	/// Roadmap-6 (2026-07-19) : MULTI-sélection ordonnée. La sélection est une
	/// liste d'`EntityId` sans doublon ; l'entité **primaire** (celle du gizmo
	/// et de l'Inspector) est la DERNIÈRE sélectionnée — `Current()` la
	/// retourne, ce qui préserve la compatibilité de tous les consommateurs
	/// mono-sélection existants (Outliner, Inspector, Dupliquer/Supprimer).
	///
	/// Contrainte thread : main thread (comme le reste de l'éditeur ; ImGui
	/// n'est pas thread-safe). Pur CPU, aucune dépendance Vulkan/ImGui.
	class EditorSelection
	{
	public:
		using OnChangedCallback = std::function<void(EntityId)>;

		/// Sélectionne `id` seul (remplace TOUTE la sélection). No-op (aucune
		/// notification) si la sélection est déjà exactement `{id}`. Un `id` de
		/// kind None équivaut à `Clear()`.
		/// Effet de bord : invoque le callback OnChanged si la sélection change.
		void Select(EntityId id);

		/// Roadmap-6 — Bascule `id` dans la sélection (Ctrl+Maj+clic) : l'ajoute
		/// s'il est absent (il devient l'entité primaire), le retire s'il est
		/// présent (la primaire redevient le dernier restant). No-op pour un
		/// kind None.
		/// Effet de bord : invoque le callback OnChanged si la sélection change.
		void Toggle(EntityId id);

		/// Vide la sélection (kind = None). No-op si déjà vide.
		/// Effet de bord : invoque le callback OnChanged si la sélection change.
		void Clear();

		/// Entité primaire = dernière sélectionnée (kind = None si aucune).
		EntityId Current() const { return m_items.empty() ? EntityId{} : m_items.back(); }

		/// true si au moins une entité est sélectionnée.
		bool HasSelection() const { return !m_items.empty(); }

		/// Roadmap-6 — true si `id` fait partie de la sélection.
		bool Contains(EntityId id) const;

		/// Roadmap-6 — Nombre d'entités sélectionnées.
		size_t Count() const { return m_items.size(); }

		/// Roadmap-6 — Liste ordonnée (ordre de sélection, primaire en dernier).
		/// Valide jusqu'à la prochaine mutation de la sélection.
		const std::vector<EntityId>& Items() const { return m_items; }

		/// Installe l'observateur de changement (un seul ; remplace le précédent).
		/// Le callback reçoit l'entité PRIMAIRE courante (None si sélection vide).
		void SetOnChanged(OnChangedCallback cb) { m_onChanged = std::move(cb); }

	private:
		/// Invoque l'observateur avec l'entité primaire courante.
		void Notify() { if (m_onChanged) m_onChanged(Current()); }

		std::vector<EntityId> m_items; ///< sans doublon, primaire = back()
		OnChangedCallback m_onChanged;
	};
}
