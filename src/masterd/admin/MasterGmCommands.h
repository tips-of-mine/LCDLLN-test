#pragma once
// ADM.0 — Commandes de jeu côté master : aide, relais vers les shards, audit.
//
// Ce module enregistre dans le ChatCommandRouter du ChatRelayHandler :
//   - les commandes exécutées par le master (« /help », « /search »,
//     « /gmaudit »… puis, au fil des tickets ADM, modération et serveur) ;
//   - un relais générique pour chaque entrée "shard_command" de
//     slash_commands.json : le rôle est vérifié ici, puis la commande part au shard
//     qui héberge le personnage de l'appelant (GmShardRelay, opcodes 216/217) ;
//   - le rappel d'audit (table gm_command_audit) pour les commandes réservées et
//     les refus.
// Les réponses sont des lignes « système » envoyées dans le chat de l'appelant
// (canal Server), découpées à 250 octets (limite d'une ligne de chat).
//
// Thread : appelé depuis la boucle réseau du master (handler de paquets) et sa
// boucle principale (Tick) — même thread dans masterd/main_linux.cpp.

#include "src/masterd/account/AccountRole.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::reputation
{
	class ReputationManager;
	class MysqlReputationStore;
}

namespace engine::server::chat
{
	class ChatCommandRouter;
	struct CommandCallContext;
}

namespace engine::server
{
	class NetServer;
	class AccountStore;
	class ConnectionSessionMap;
	class SessionManager;
	class SessionCharacterMap;
	class ShardRegistry;
	class SlashCommandRegistry;
	class GmShardRelay;
	struct SlashCommandEntry;

	/// Services du master utilisés par les commandes (pointeurs non possédés, qui
	/// doivent survivre au module). Un pointeur nul désactive les commandes qui en
	/// dépendent (réponse « indisponible »).
	struct MasterGmServices
	{
		chat::ChatCommandRouter* router = nullptr;
		const SlashCommandRegistry* registry = nullptr;
		AccountStore* accounts = nullptr;
		NetServer* server = nullptr;
		ConnectionSessionMap* connMap = nullptr;
		SessionManager* sessions = nullptr;
		SessionCharacterMap* charMap = nullptr;
		ShardRegistry* shards = nullptr;
		db::ConnectionPool* db = nullptr;
		GmShardRelay* relay = nullptr;
		/// ADM.3 — réputations (/setrep) : autorité en mémoire + persistance (nullptr = sans base).
		reputation::ReputationManager* reputation = nullptr;
		reputation::MysqlReputationStore* reputationStore = nullptr;
		/// Horloge monotone en secondes (délais du relais).
		std::function<double()> nowSeconds;
	};

	class MasterGmCommands final
	{
	public:
		explicit MasterGmCommands(MasterGmServices services) : m_s(std::move(services)) {}

		/// Enregistre toutes les commandes master + les relais shard + l'audit.
		/// À appeler une fois, après le chargement du registre.
		void RegisterAll();

		/// Paquet 217 reçu sur \p connId : vérifie que la connexion est celle d'un shard
		/// enregistré, décode et remet le résultat au relais.
		void OnShardResultPacket(uint32_t connId, const uint8_t* payload, size_t payloadSize);

		/// Délais du relais (à appeler à chaque tour de la boucle principale).
		void Tick();

		/// Envoie \p line (découpée à 250 octets) à la connexion \p connId / session
		/// \p sessionId. Sans effet si le serveur n'est pas câblé.
		void Reply(uint32_t connId, uint64_t sessionId, std::string_view line) const;

		/// Envoie \p line au compte \p accountId (connexion \p connIdHint si elle lui
		/// appartient encore, sinon recherche parmi les sessions actives).
		void ReplyToAccount(uint64_t accountId, uint32_t connIdHint, std::string_view line) const;

		/// Envoie plusieurs lignes (Reply en boucle).
		void ReplyLines(uint32_t connId, uint64_t sessionId, const std::vector<std::string>& lines) const;

	private:
		/// ADM.0 — aide et outils : /help, /search, /gmaudit.
		void RegisterCore();
		/// ADM.0 — relais générique des entrées "shard_command" du registre.
		void RegisterShardForwarders();
		/// ADM.0 — rappel d'audit (table gm_command_audit + journal).
		void InstallAuditSink();
		/// ADM.2 — informations : /online, /ping, /serverstats, /info (ligne de compte
		/// ajoutée par le master avant le relais vers le shard du joueur visé).
		void RegisterInformation();

		/// Personnage en jeu nommé \p name (sans casse) : connexion et fiche, nullopt sinon.
		struct OnlineCharacter
		{
			uint32_t connId = 0;
			uint64_t accountId = 0;
			uint64_t characterId = 0;
			std::string characterName;
			uint32_t shardId = 0;
		};
		std::optional<OnlineCharacter> FindOnlineCharacter(std::string_view name) const;

		/// ADM.3 — personnage : /setrep (réputation tenue par le master).
		void RegisterCharacter();

		/// ADM.8 — modération : /unmute, /mutes, /unban, /bans, /warn, /history,
		/// /notify, /gmw, /csr (MasterGmModeration.cpp).
		void RegisterModeration();

		/// Envoie \p line à la connexion \p connId (session retrouvée dans la table
		/// des connexions). Sans effet si la connexion n'existe plus.
		void SendToConnection(uint32_t connId, std::string_view line) const;

		/// Envoie \p line à toutes les connexions authentifiées. \return nombre d'envois.
		size_t BroadcastLine(std::string_view line) const;

		/// Compte visé par \p name : personnage en jeu (sans casse), sinon login.
		/// \return 0 si introuvable.
		uint64_t ResolveAccount(std::string_view name) const;

		/// Relaie la commande \p entry tapée par \p ctx au shard de son personnage.
		/// \param actorCharacterOverride 0 = personnage de l'appelant ; sinon le
		///        personnage pour le compte duquel la commande s'exécute (/csr).
		void ForwardToShard(const chat::CommandCallContext& ctx, const SlashCommandEntry& entry,
			std::string_view args, uint64_t actorCharacterOverride = 0, uint32_t shardOverride = 0) const;

		/// Rôle courant d'un compte (Player si l'AccountStore manque).
		AccountRole RoleOf(uint64_t accountId) const;

		MasterGmServices m_s;
	};
}
