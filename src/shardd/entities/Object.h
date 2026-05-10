#pragma once
// Object : classe de base pour toutes les entites MMO (Player, Creature,
// GameObject, etc.). Detient :
//   - un ObjectGuid identifiant (immuable apres construction)
//   - une UpdateMask dimensionnee pour les fields delta replication.
//
// Future : sera derivee par Unit -> Player / Creature, puis specialisee
// pour exposer ses propres UpdateField<T> (cf. ticket Entities hierarchy
// pour la specification de la hierarchie complete).
//
// Wave 7 foundation : interface minimale pour permettre la suite des PRs
// (chaque sous-type ajoutera ses propres champs et tests).

#include "src/shardd/entities/ObjectGuid.h"
#include "src/shardd/entities/UpdateMask.h"

#include <cstdint>
#include <cstddef>

namespace engine::server::entities
{
	/// Base class polymorphe pour toutes les entites MMO.
	class Object
	{
	public:
		/// Constructeur : initialise le guid + alloue le mask pour \p fieldCount champs.
		/// \param guid identifiant immuable de l'objet (logique : c'est sa cle).
		/// \param fieldCount nombre de UpdateField<T> que la classe derivee va exposer.
		///        Determine la taille du bitmask (ceil(fieldCount/32) chunks).
		Object(ObjectGuid guid, size_t fieldCount)
			: m_guid(guid), m_mask(fieldCount) {}

		/// Destructeur virtuel : Object est polymorphe (derive par Unit, Player, etc.).
		virtual ~Object() = default;

		/// Lecture du guid (immutable).
		ObjectGuid Guid() const noexcept { return m_guid; }

		/// Lecture du type discriminant (shortcut sur Guid().Type()).
		ObjectType Type() const noexcept { return m_guid.Type(); }

		/// Mask de mise a jour mutable (utilise par les UpdateField<T> derives).
		UpdateMask& Mask() noexcept { return m_mask; }
		const UpdateMask& Mask() const noexcept { return m_mask; }

		/// Reset le mask apres replication reussie au client. A appeler par
		/// la couche de replication apres avoir construit + envoye un delta.
		void OnReplicationSent() noexcept { m_mask.Clear(); }

		/// True si l'objet a au moins un champ modifie depuis le dernier
		/// OnReplicationSent() (ou depuis la construction si jamais reset).
		bool IsDirty() const noexcept { return !m_mask.Empty(); }

	protected:
		ObjectGuid  m_guid;
		UpdateMask  m_mask;
	};
}
