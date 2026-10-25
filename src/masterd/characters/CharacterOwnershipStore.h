#pragma once
// CharacterOwnershipStore — côté master, attribution et libération du verrou de
// propriété des personnages (ETAPE1.2 : « un seul propriétaire par personnage »).
//
// Colonnes (migration 0077) : `characters.owner_shard_id` (shard propriétaire, NULL
// = libre), `characters.owner_generation` (incrémenté à chaque attribution),
// `characters.owner_acquired_at` (UTC). Le master est le seul à attribuer ou
// libérer le verrou ; seul le shard propriétaire écrit l'état de jeu du personnage
// (contrôle côté shard : db::CheckCharacterOwnership dans ServerApp::SaveCharacterToDb).
//
// Thread-safety : fonctions sans état ; chaque appel utilise la connexion passée
// ou en emprunte une au pool.

#include <cstdint>
#include <optional>

struct MYSQL;

namespace engine::server::db
{
	class ConnectionPool;
	class SqlPreparedStatementCache;
}

namespace engine::server
{
	/// Attribue le personnage \p characterId au shard \p shardId : owner_shard_id = shardId,
	/// owner_generation + 1. Gaté par \p accountId (un compte ne peut verrouiller que ses
	/// personnages) et `deleted_at IS NULL`.
	/// \return la nouvelle génération, ou std::nullopt si la mise à jour a échoué
	///         (personnage introuvable, colonnes absentes : base antérieure à 0077).
	///         Dans ce cas l'appelant admet le personnage sans verrou (mode legacy).
	std::optional<uint64_t> AcquireCharacterOwnership(MYSQL* mysql, db::SqlPreparedStatementCache* cache,
		uint64_t characterId, uint64_t accountId, uint32_t shardId);

	/// Libère tous les verrous détenus par \p shardId (owner_shard_id = NULL, génération
	/// conservée). Appelée quand le master déclare un shard mort : si ce shard revient,
	/// ses sauvegardes sont refusées (il n'est plus propriétaire).
	/// \return nombre de personnages libérés, ou std::nullopt si la base est indisponible.
	std::optional<uint64_t> ReleaseShardOwnership(db::ConnectionPool& pool, uint32_t shardId);

	/// Vrai si le personnage est actuellement verrouillé par un shard (owner_shard_id non
	/// NULL). Sert à CharacterSavePositionHandler : le master n'écrit plus la position
	/// d'un personnage en jeu. Faux si la requête échoue (comportement historique).
	bool IsCharacterOwnedByShard(MYSQL* mysql, db::SqlPreparedStatementCache* cache,
		uint64_t characterId, uint64_t accountId);
}
