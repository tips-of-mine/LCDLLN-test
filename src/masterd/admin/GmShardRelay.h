#pragma once
// ADM.0 — Relais des commandes de jeu (MJ) du master vers les shards.
//
// Une commande de statut "shard_command" (slash_commands.json) est exécutée par
// le shard qui héberge le personnage de l'appelant : l'état de jeu (position,
// vie, inventaire…) n'existe que là. Le master :
//   1. vérifie le rôle (ChatCommandRouter) ;
//   2. appelle Forward() : un identifiant de requête est attribué, la demande est
//      mémorisée et le paquet 216 part vers le shard ;
//   3. reçoit 217 (OnResult) : les lignes sont renvoyées dans le chat de l'appelant ;
//   4. Tick() : une demande restée sans réponse au-delà du délai est abandonnée et
//      l'appelant est prévenu.
//
// Module sans réseau : l'envoi et la réponse passent par des rappels (testable,
// cf. gm_shard_relay_tests). Thread-safe (mutex interne).

#include "src/shared/network/ShardPayloads.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	class GmShardRelay final
	{
	public:
		/// Envoie \p packet au shard \p shardId. \return false si le shard est injoignable.
		using SendFn = std::function<bool(uint32_t shardId, const std::vector<uint8_t>& packet)>;
		/// Affiche \p line dans le chat du compte \p accountId (connexion \p connId si connue).
		using ReplyFn = std::function<void(uint64_t accountId, uint32_t connId, const std::string& line)>;

		/// Résultat de Forward.
		enum class ForwardResult : uint8_t
		{
			Sent,        ///< demande partie, réponse attendue.
			NoShard,     ///< shardId == 0 : personnage pas en jeu.
			SendFailed,  ///< shard injoignable (connexion absente ou envoi refusé).
			Busy,        ///< trop de demandes en attente pour ce compte.
			Invalid,     ///< paquet impossible à construire (nom vide ou trop long).
		};

		/// \param timeoutSeconds délai de réponse d'un shard avant abandon (s).
		explicit GmShardRelay(double timeoutSeconds = 8.0) : m_timeoutSeconds(timeoutSeconds) {}

		void SetSend(SendFn fn) { m_send = std::move(fn); }
		void SetReply(ReplyFn fn) { m_reply = std::move(fn); }

		/// Relaie \p request (requestId attribué ici) au shard \p shardId.
		/// \param connId   connexion de l'appelant (réponse ciblée).
		/// \param nowSeconds horloge monotone (s).
		ForwardResult Forward(uint32_t shardId, uint32_t connId, engine::network::GmCommandRequestPayload request,
			double nowSeconds);

		/// Résultat reçu du shard \p fromShardId. Une réponse inconnue, déjà traitée ou
		/// venant d'un autre shard que celui interrogé est ignorée (journalisée).
		/// \return true si la réponse a été affichée.
		bool OnResult(uint32_t fromShardId, const engine::network::GmCommandResultPayload& result);

		/// Abandonne les demandes plus vieilles que le délai et prévient l'appelant.
		/// \return nombre de demandes abandonnées.
		size_t Tick(double nowSeconds);

		/// Demandes en attente de réponse.
		size_t PendingCount() const;

		/// Au plus N demandes en vol par compte (anti-rafale).
		static constexpr size_t kMaxPendingPerAccount = 8u;

	private:
		struct Pending
		{
			uint32_t shardId = 0;
			uint32_t connId = 0;
			uint64_t accountId = 0;
			std::string command;
			double sentAtSeconds = 0.0;
		};

		double m_timeoutSeconds = 8.0;
		SendFn m_send;
		ReplyFn m_reply;
		mutable std::mutex m_mutex;
		uint64_t m_nextRequestId = 1;
		std::unordered_map<uint64_t, Pending> m_pending;
	};
}
