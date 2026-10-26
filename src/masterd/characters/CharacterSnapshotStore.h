#pragma once
// ETAPE2.6 — CharacterSnapshotStore : instantanés restaurables, côté master.
//
// Le master ne fait que LIRE les instantanés (character_snapshots, écrits par le
// shard à chaque déconnexion) et POSER une demande de restauration
// (character_restore_requests). La restauration elle-même est appliquée par le
// shard propriétaire à la prochaine connexion du personnage : le master n'écrit
// jamais l'état d'un personnage (règle d'un seul écrivain, ETAPE1.2 / 2.1).
// Utilisé par les commandes MJ /snapshots et /restore (main_linux).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server
{
	/// Un instantané tel que listé pour un MJ.
	struct CharacterSnapshotInfo
	{
		uint64_t id = 0;
		std::string createdAtUtc; ///< « AAAA-MM-JJ hh:mm:ss »
		std::string reason;
		uint32_t shardId = 0;
	};

	/// Résultat d'une demande de restauration.
	enum class CharacterRestoreRequestResult : uint8_t
	{
		Requested,          ///< Demande posée (remplace une demande en attente).
		CharacterNotFound,  ///< Aucun personnage actif de ce nom.
		SnapshotNotFound,   ///< L'instantané n'existe pas ou appartient à un autre personnage.
		Failed,             ///< Base indisponible ou erreur SQL.
	};

	/// Identifiant d'un personnage actif d'après son nom exact (nullopt si absent).
	std::optional<uint64_t> FindActiveCharacterIdByName(db::ConnectionPool& pool, std::string_view name);

	/// Les \p maxCount instantanés les plus récents du personnage (du plus récent au
	/// plus ancien). \return false si la base est indisponible.
	bool ListCharacterSnapshots(db::ConnectionPool& pool, uint64_t characterId, size_t maxCount,
		std::vector<CharacterSnapshotInfo>& out);

	/// Pose (ou remplace) la demande de restauration de \p characterId vers
	/// l'instantané \p snapshotId, au nom du compte MJ \p gmAccountId.
	CharacterRestoreRequestResult RequestCharacterRestore(db::ConnectionPool& pool, uint64_t characterId,
		uint64_t snapshotId, uint64_t gmAccountId);
}
