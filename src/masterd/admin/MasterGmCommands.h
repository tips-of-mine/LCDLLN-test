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
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
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
