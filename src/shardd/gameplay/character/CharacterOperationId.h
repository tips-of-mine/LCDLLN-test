#pragma once
// ETAPE2.4 (chantier 3.4) — identifiant unique d'une opération à deux personnages
// (échange, transfert de monnaie), clé primaire de `character_operations`.
//
// Format : `<type>-s<shard>-<ms UTC>-<compteur>`, ex. `trade-s1-1761465600000-42`.
//   - le shard distingue deux shards ;
//   - l'heure UTC en millisecondes distingue deux démarrages du même shard ;
//   - le compteur (propre au processus, croissant) distingue deux opérations de
//     la même milliseconde.
// Tient dans VARCHAR(80) (70 caractères au plus) : type tronqué à 16 caractères.
//
// Logique pure, testée par character_operation_id_tests.

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Construit l'identifiant d'une opération.
	/// \param kind type d'opération (« trade », « wallet_transfer »), tronqué à 16 caractères.
	/// \param shardId identifiant du shard (clé de config shard.id).
	/// \param unixMs heure UTC en millisecondes.
	/// \param counter compteur croissant du processus.
	inline std::string MakeCharacterOperationId(std::string_view kind, uint32_t shardId, uint64_t unixMs, uint64_t counter)
	{
		std::string id;
		id.reserve(80);
		id.append(kind.substr(0, 16));
		id.append("-s");
		id.append(std::to_string(shardId));
		id.push_back('-');
		id.append(std::to_string(unixMs));
		id.push_back('-');
		id.append(std::to_string(counter));
		return id;
	}
}
