#pragma once
// GridNotifier : foncteur Visitor specialise pour collecter les
// destinataires d'un broadcast localise (typiquement les Player dans
// l'AoI). Se branche sur GridVisit via GridVisitWithVisitor.
//
// Conception : GridNotifier ne SAIT PAS comment determiner si un
// EntityId est un Player. Il delegue a un PlayerFilter (predicate)
// fourni par le caller — typiquement une lambda qui interroge
// ObjectAccessor (Wave 19) : `[&](EntityId id) { return
// accessor.GetSnapshot(id) && accessor.GetSnapshot(id)->isPlayer; }`.
//
// Apres GridVisitWithVisitor, le caller utilise Recipients() pour
// envoyer le packet sur les sessions correspondantes (resolution
// EntityId -> SessionId via la session map, Wave 12+).

#include "src/shared/network/ReplicationTypes.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace engine::server
{
	/// Foncteur Visitor : collecte les EntityId qui satisfont un predicate
	/// (typiquement "est un Player"). Stateful : conserve la liste pour
	/// reuse par le caller post-visite.
	class GridNotifier
	{
	public:
		/// Predicate : retourne true si l'EntityId doit etre inclus dans
		/// Recipients(). Caller fournit la logique (ex: lookup
		/// ObjectAccessor).
		using PlayerFilter = std::function<bool(EntityId)>;

		/// \param isPlayer predicate. Si null, AUCUN id n'est collecte
		///        (defensif : pas de broadcast accidentel a toutes les
		///        entites du voisinage).
		explicit GridNotifier(PlayerFilter isPlayer)
			: m_isPlayer(std::move(isPlayer))
		{}

		/// Appelee par GridVisitWithVisitor pour chaque EntityId visite.
		/// Filtre via le predicate et accumule si match.
		void Visit(EntityId id)
		{
			if (m_isPlayer && m_isPlayer(id))
				m_recipients.push_back(id);
		}

		/// Liste des EntityId collectes par filtre. Order : ordre de
		/// visite (depend de GridVisit, non garanti stable).
		const std::vector<EntityId>& Recipients() const noexcept { return m_recipients; }

		/// Nombre de destinataires collectes (alias de Recipients().size()).
		size_t Count() const noexcept { return m_recipients.size(); }

		/// Reset interne — utile pour reutiliser un GridNotifier sur
		/// plusieurs broadcasts successifs sans realloc.
		void Reset() noexcept { m_recipients.clear(); }

	private:
		PlayerFilter          m_isPlayer;
		std::vector<EntityId> m_recipients;
	};
}
