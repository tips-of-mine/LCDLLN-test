#pragma once
// ADM.0 — Catalogue des commandes de jeu exécutées par le shard.
//
// Le master décide QUI peut appeler une commande (rôle minimum lu dans
// game/data/config/slash_commands.json, entrées de statut "shard_command") et la
// relaie au shard qui héberge le personnage de l'appelant (opcode 216). Ce
// catalogue liste les commandes que CE binaire de shard sait exécuter, avec le
// même rôle minimum : le shard refait le contrôle (défense en profondeur) et le
// test slash_commands_catalog_tests vérifie que le JSON et ce tableau concordent
// (nom, rôle) — un oubli d'un côté casse la CI au lieu d'un « commande inconnue »
// en jeu.
//
// Module pur (en-tête seul) : inclus par ServerAppGm.cpp (shard, Windows et Linux)
// et par le test du master.
//
// Pour ajouter une commande shard : (1) une ligne ici, (2) l'entrée JSON avec
// "status": "shard_command" et le même "minRole", (3) le handler dans
// src/shared/server_bootstrap/ServerAppGm.cpp (table kGmHandlers).

#include <cstdint>
#include <string_view>

namespace engine::server::gm
{
	/// Rôles de compte, valeurs identiques à engine::server::AccountRole (master) :
	/// le wire transporte l'octet tel quel.
	enum class GmRole : uint8_t
	{
		Player = 0,
		Moderator = 1,
		GameMaster = 2,
		Administrator = 3,
		Console = 4,
	};

	/// Une commande exécutable par le shard.
	struct GmShardCommandInfo
	{
		std::string_view name;   ///< nom canonique (minuscules, sans « / »).
		GmRole minRole;          ///< rôle minimum (identique au JSON).
	};

	/// Toutes les commandes exécutées par le shard (nom canonique uniquement : les
	/// alias sont résolus par le master avant l'envoi).
	inline constexpr GmShardCommandInfo kGmShardCommands[] = {
		// ADM.0 — socle
		{ "shardinfo", GmRole::GameMaster },
	};

	/// Entrée du catalogue pour \p name (nom canonique exact), nullptr sinon.
	inline const GmShardCommandInfo* FindGmShardCommand(std::string_view name)
	{
		for (const GmShardCommandInfo& c : kGmShardCommands)
		{
			if (c.name == name)
				return &c;
		}
		return nullptr;
	}
}
