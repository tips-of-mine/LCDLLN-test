#pragma once
// CharacterOwnership — règle « un seul propriétaire par personnage » (ETAPE1.2).
//
// Le master attribue un verrou de propriété à chaque entrée en jeu
// (CharacterEnterWorldHandler) : colonnes `characters.owner_shard_id` (shard qui
// héberge le personnage) et `characters.owner_generation` (compteur incrémenté à
// chaque nouvelle admission), migration 0077. Le shard reçoit la génération dans
// le push d'admission et la présente à chaque sauvegarde : il ne peut écrire le
// personnage que s'il en est toujours le propriétaire en base.
//
// Cas couverts :
//   - un shard « zombie » (déclaré mort par le master, puis revenu) ne peut plus
//     écraser un personnage repris ailleurs : owner_shard_id a été remis à NULL ou
//     la génération a augmenté ;
//   - un ancien processus encore en train de sauvegarder après une ré-admission
//     (déconnexion / reconnexion rapide) ne peut plus écrire : génération dépassée.
//
// Logique pure, sans dépendance base de données : testée par character_ownership_tests.

#include <cstdint>

namespace engine::server::db
{
	/// Résultat du contrôle de propriété avant une écriture de personnage.
	enum class CharacterOwnershipCheck : uint8_t
	{
		/// Le shard est le propriétaire courant : écriture autorisée.
		Owned,
		/// Aucun verrou en base ni côté shard (master ou base antérieurs à la
		/// migration 0077) : écriture autorisée, comportement historique.
		LegacyUnlocked,
		/// Un autre propriétaire (ou une génération plus récente) existe : écriture refusée.
		NotOwner,
	};

	/// Décide si un shard peut écrire un personnage.
	/// \param dbOwnerShardId  `characters.owner_shard_id` lu en base (0 si NULL).
	/// \param dbGeneration    `characters.owner_generation` lu en base.
	/// \param myShardId       identifiant de ce shard (config `shard.id`).
	/// \param myGeneration    génération reçue dans le push d'admission (0 si aucune).
	constexpr CharacterOwnershipCheck CheckCharacterOwnership(uint32_t dbOwnerShardId, uint64_t dbGeneration,
		uint32_t myShardId, uint64_t myGeneration)
	{
		if (myGeneration == 0u)
		{
			// Admis sans verrou : seulement tant que personne n'a jamais verrouillé ce personnage.
			return dbGeneration == 0u ? CharacterOwnershipCheck::LegacyUnlocked : CharacterOwnershipCheck::NotOwner;
		}
		if (dbOwnerShardId == myShardId && dbGeneration == myGeneration)
		{
			return CharacterOwnershipCheck::Owned;
		}
		return CharacterOwnershipCheck::NotOwner;
	}

	/// Vrai si le résultat autorise l'écriture.
	constexpr bool IsWriteAllowed(CharacterOwnershipCheck check)
	{
		return check != CharacterOwnershipCheck::NotOwner;
	}
}
