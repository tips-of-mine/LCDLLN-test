#pragma once
// ADM.8 — Accès base des commandes de modération (/unmute, /mutes, /bans).
//
// Tables existantes : chat_mutes (migration 0044 : account_id, until_ts en ms UTC,
// 0 = permanent, reason) et accounts (account_status : 0 actif, 1 banni, 2
// verrouillé — /ban pose 2). Aucune migration ajoutée.

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
	/// Une sanction de chat en cours.
	struct GmMuteRow
	{
		uint64_t accountId = 0;
		std::string login;
		uint64_t untilTsMs = 0; ///< 0 = permanent
		std::string reason;
	};

	/// Lève la sanction de chat de \p accountId. \return nombre de lignes supprimées
	/// (0 = pas de sanction), -1 si la base est indisponible.
	int DeleteChatMute(db::ConnectionPool& pool, uint64_t accountId);

	/// Sanctions de chat en cours à \p nowMs (permanentes comprises), plus récentes
	/// d'abord, au plus \p limit. \return false si la base est indisponible.
	bool ListActiveMutes(db::ConnectionPool& pool, uint64_t nowMs, size_t limit, std::vector<GmMuteRow>& out);

	/// Logins des comptes bannis ou verrouillés (account_status 1 ou 2) contenant
	/// \p filter (vide = tous), au plus \p limit. \return false si base indisponible.
	bool ListLockedAccounts(db::ConnectionPool& pool, std::string_view filter, size_t limit, std::vector<std::string>& out);
}
