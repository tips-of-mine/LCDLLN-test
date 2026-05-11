#pragma once
// Wave 11 Persistence Cinematics — ICinematicStore : interface abstraite pour
// persister les cinematiques deja vues par un account. Deux impls :
//   - InMemoryCinematicStore : map en RAM, utilise quand le pool MySQL n'est
//     pas dispo (mode dev / no-DB). L'etat est perdu au reboot du master.
//   - MysqlCinematicStore    : backed par la table cinematic_seen (migration
//     0058). Idempotence via INSERT IGNORE.
//
// L'API est minimale : marquer une sequence vue, lire si vue, lister les
// sequences vues par un account (pour de futurs hooks Quest / Loot qui
// voudraient gater les replays cote serveur).
//
// Aucune impl ne leve d'exception : tout retour booleen indique succes / no-op
// (MarkSeen sur une ligne deja presente retourne true silencieusement).

#include <cstdint>
#include <vector>

namespace engine::server::cinematics
{
	/// Interface store cinematic_seen. Pattern Wave 5 (cf. MysqlGuildStore,
	/// MysqlMailStore) : la classe concrete decide si elle persiste vraiment
	/// (MySQL) ou seulement en RAM. Pas d'exception, best-effort.
	class ICinematicStore
	{
	public:
		virtual ~ICinematicStore() = default;

		/// Marque la sequence comme vue par l'account. Idempotent (INSERT
		/// IGNORE / no-op si deja vue). Retourne true si la persistance n'a
		/// pas echoue (l'idempotence est consideree comme succes).
		///
		/// \param accountId  account propriétaire de la "vue".
		/// \param sequenceId id de la cinematic (cf. seq<id>.json client).
		/// \param nowMs      timestamp first-seen en ms wall-clock (utilise
		///                   uniquement si l'insertion cree une nouvelle ligne).
		virtual bool MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs) = 0;

		/// True si l'account a deja vu la sequence.
		virtual bool HasSeen(uint64_t accountId, uint32_t sequenceId) const = 0;

		/// Liste des sequences vues par un account. Tri non garanti.
		virtual std::vector<uint32_t> ListSeen(uint64_t accountId) const = 0;
	};
}
