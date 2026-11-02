#pragma once
// ADM.0 — Journal d'audit des commandes de jeu (table gm_command_audit, migration 0085).
//
// Écrit par le rappel d'audit du ChatCommandRouter (MasterGmCommands) : une ligne
// par commande réservée (rôle minimum modérateur ou plus) et par refus faute de
// rôle. Lu par « /gmaudit [n] [login] » (administrateur).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	/// Une ligne du journal.
	struct GmAuditRecord
	{
		std::string createdAtUtc;  ///< « AAAA-MM-JJ HH:MM:SS » (lecture seulement).
		uint64_t accountId = 0;
		std::string role;          ///< « player », « game_master »…
		std::string command;       ///< nom tapé, sans « / ».
		std::string args;          ///< tronqué à 255 octets.
		std::string result;        ///< « ok » ou « denied ».
		std::string targetName;    ///< cible nommée (vide si inconnue).
		std::string login;         ///< login du compte (lecture seulement, jointure accounts).
	};

	/// Ajoute \p record. \return false si la base est indisponible (non bloquant :
	/// l'appelant journalise et continue).
	bool InsertGmAudit(db::ConnectionPool& pool, const GmAuditRecord& record);

	/// Les \p limit dernières lignes (plus récentes d'abord), filtrées par login
	/// d'appelant ou nom de cible si \p filter n'est pas vide.
	/// \return false si la base est indisponible.
	bool ListGmAudit(db::ConnectionPool& pool, std::string_view filter, size_t limit, std::vector<GmAuditRecord>& out);
}
