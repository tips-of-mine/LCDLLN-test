#pragma once
// ETAPE5.6 (chantier 5.2 du rapport City of Heroes) — or joint à un courrier, en deux
// phases entre le master (courrier, comptes) et le shard propriétaire de l'expéditeur
// (bourse). Vocabulaire commun aux deux côtés ; la table `mail_transfers`
// (migration 0082) est le verrou partagé. En-tête seul, testé par
// mail_transfer_tests.
//
//   pending ──(shard : or prélevé, même transaction)──▶ locked ──(retrait)──▶ delivered
//      │                                                  │
//      └──(refus, délai, absent)──▶ cancelled             └──(courrier expiré)──▶ refunded
//
// Chaque passage est un compare-and-set en base (UPDATE … WHERE state = <attendu>) :
// le prélèvement (shard) et l'annulation (master) s'excluent mutuellement.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::mail
{
	enum class MailTransferState : uint8_t
	{
		Pending = 0,
		Locked = 1,
		Delivered = 2,
		Cancelled = 3,
		Refunded = 4,
	};

	/// Valeur de la colonne `mail_transfers.state`.
	inline const char* MailTransferStateName(MailTransferState s)
	{
		switch (s)
		{
		case MailTransferState::Pending: return "pending";
		case MailTransferState::Locked: return "locked";
		case MailTransferState::Delivered: return "delivered";
		case MailTransferState::Cancelled: return "cancelled";
		case MailTransferState::Refunded: return "refunded";
		}
		return "pending";
	}

	inline std::optional<MailTransferState> ParseMailTransferState(std::string_view s)
	{
		if (s == "pending") return MailTransferState::Pending;
		if (s == "locked") return MailTransferState::Locked;
		if (s == "delivered") return MailTransferState::Delivered;
		if (s == "cancelled") return MailTransferState::Cancelled;
		if (s == "refunded") return MailTransferState::Refunded;
		return std::nullopt;
	}

	/// Passages autorisés (les seuls que le code tente en base).
	inline bool CanTransition(MailTransferState from, MailTransferState to)
	{
		switch (from)
		{
		case MailTransferState::Pending:
			return to == MailTransferState::Locked || to == MailTransferState::Cancelled;
		case MailTransferState::Locked:
			return to == MailTransferState::Delivered || to == MailTransferState::Refunded;
		default:
			return false; // états terminaux
		}
	}

	inline bool IsTerminal(MailTransferState s)
	{
		return s == MailTransferState::Delivered || s == MailTransferState::Cancelled || s == MailTransferState::Refunded;
	}

	/// Identifiant d'opération idempotent d'un transfert : un par courrier.
	inline std::string MakeMailTransferOperationId(uint64_t mailId)
	{
		return "mail-" + std::to_string(mailId);
	}

	/// Identifiant du dépôt dans character_mailbox pour une étape (« deliver », « refund »).
	inline std::string MakeMailboxDepositOperationId(std::string_view transferOperationId, std::string_view step)
	{
		std::string id(transferOperationId);
		id += '-';
		id += step;
		return id;
	}

	/// Réponse du shard à une demande de prélèvement (opcode 214).
	enum class MailDebitResult : uint8_t
	{
		Debited = 0,        ///< or prélevé (ou déjà prélevé : réponse idempotente)
		NotHere = 1,        ///< personnage absent de ce shard (déconnecté, transféré)
		Insufficient = 2,   ///< or insuffisant
		Cancelled = 3,      ///< le master a annulé avant le prélèvement
		Failed = 4,         ///< erreur de base (le master annulera au délai)
		Unsupported = 5,    ///< shard sans base de données
	};

	/// Délai maximal d'attente du prélèvement (au-delà, le master annule).
	constexpr uint64_t kMailDebitMaxDelayMs = 30'000u;

	/// Vrai si un transfert `pending` créé avec \p deadlineMs doit être annulé à \p nowMs.
	inline bool PendingTransferExpired(uint64_t deadlineMs, uint64_t nowMs)
	{
		return nowMs >= deadlineMs;
	}
}
