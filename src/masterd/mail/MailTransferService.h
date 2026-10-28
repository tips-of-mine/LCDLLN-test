#pragma once
// ETAPE5.6 (chantier 5.2 du rapport City of Heroes) — côté master de l'or joint à un
// courrier, en deux phases. Voir src/shared/mail/MailTransfer.h (états, identifiants)
// et la migration 0082 (table mail_transfers, verrou commun master / shard).
//
// Le master ne touche jamais une bourse : il crée le transfert (pending), demande au
// shard de l'expéditeur de prélever (opcode 213), et n'écrit que des passages
// compare-and-set (UPDATE … WHERE state = <attendu>) et des dépôts idempotents dans
// character_mailbox (crédités par le shard du destinataire). Toutes les fonctions
// empruntent une connexion au pool : thread-safe.

#include "src/shared/mail/MailTransfer.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	/// Ligne de mail_transfers.
	struct MailTransferRow
	{
		std::string operationId;
		uint64_t mailId = 0;
		uint64_t senderAccountId = 0;
		uint64_t senderCharacterId = 0;
		uint32_t senderShardId = 0;
		uint64_t receiverAccountId = 0;
		uint64_t gold = 0;
		engine::mail::MailTransferState state = engine::mail::MailTransferState::Pending;
		uint64_t deadlineMs = 0;
	};

	/// Issue d'une livraison au destinataire (retrait de l'or).
	enum class MailDeliverResult : uint8_t
	{
		Delivered = 0,        ///< or déposé dans character_mailbox du personnage destinataire
		AlreadyDelivered = 1, ///< déjà livré (rejeu idempotent)
		Pending = 2,          ///< prélèvement pas encore confirmé : réessayer plus tard
		NoTransfer = 3,       ///< aucun transfert pour ce courrier (ancien courrier : pas d'or réel)
		Cancelled = 4,        ///< prélèvement annulé ou or rendu : rien à livrer
		Failed = 5,           ///< base indisponible
	};

	class MailTransferService final
	{
	public:
		explicit MailTransferService(db::ConnectionPool* pool = nullptr) : m_pool(pool) {}
		void SetPool(db::ConnectionPool* pool) { m_pool = pool; }
		bool IsAvailable() const;

		/// Crée le transfert `pending` du courrier \p row.mailId (operationId et état
		/// imposés). \return false si la base est indisponible ou si le courrier a déjà un
		/// transfert.
		bool CreatePending(MailTransferRow row);

		std::optional<MailTransferRow> FindByOperation(const std::string& operationId) const;
		std::optional<MailTransferRow> FindByMail(uint64_t mailId) const;

		/// pending → cancelled. \return vrai si CE passage a eu lieu (le shard n'a donc
		/// pas prélevé et ne prélèvera plus).
		bool CancelPending(const std::string& operationId);

		/// Retrait par le destinataire \p receiverAccountId, crédité au personnage
		/// \p receiverCharacterId : locked → delivered + dépôt character_mailbox
		/// (identifiant <op>-deliver), dans une transaction.
		MailDeliverResult Deliver(uint64_t mailId, uint64_t receiverAccountId, uint64_t receiverCharacterId);

		/// Courrier SYSTÈME (expéditeur 0 : cadeau d'anniversaire…) : rien à prélever. Dépose
		/// l'or et les objets (\p items : (itemId, quantité)) pour \p receiverCharacterId et
		/// marque le transfert « delivered » (garde durable), dans une transaction.
		/// \return Delivered, AlreadyDelivered (rejeu) ou Failed.
		MailDeliverResult DepositSystemMail(uint64_t mailId, uint64_t receiverAccountId, uint64_t receiverCharacterId,
			uint64_t gold, const std::vector<std::pair<uint32_t, uint32_t>>& items);

				/// Transferts `pending` dont le délai est dépassé à \p nowMs (au plus \p limit).
		std::vector<MailTransferRow> ListExpiredPending(uint64_t nowMs, size_t limit = 64) const;

		/// Rend l'or en dépôt des courriers expirés (ou disparus) : locked → refunded +
		/// dépôt character_mailbox à l'expéditeur (<op>-refund), par transfert, dans une
		/// transaction. Annule aussi les `pending` de ces courriers. À appeler AVANT la
		/// purge des courriers expirés. \return nombre de transferts rendus.
		size_t RefundExpiredMails(uint64_t nowMs, size_t limit = 256);

	private:
		db::ConnectionPool* m_pool = nullptr;
	};
}
