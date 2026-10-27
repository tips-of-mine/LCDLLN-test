// ETAPE5.6 — tests du vocabulaire des transferts d'or par courrier (en-tête seul).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/mail/MailTransfer.h"

#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using namespace engine::mail;
}

int main()
{
	// 1. Noms en base : aller-retour, inconnu refusé.
	for (auto s : { MailTransferState::Pending, MailTransferState::Locked, MailTransferState::Delivered,
			MailTransferState::Cancelled, MailTransferState::Refunded })
		Check(ParseMailTransferState(MailTransferStateName(s)) == s, "nom aller-retour");
	Check(!ParseMailTransferState("LOCKED") && !ParseMailTransferState(""), "nom inconnu refusé");

	// 2. Machine à états : seuls les passages prévus.
	Check(CanTransition(MailTransferState::Pending, MailTransferState::Locked), "pending → locked");
	Check(CanTransition(MailTransferState::Pending, MailTransferState::Cancelled), "pending → cancelled");
	Check(CanTransition(MailTransferState::Locked, MailTransferState::Delivered), "locked → delivered");
	Check(CanTransition(MailTransferState::Locked, MailTransferState::Refunded), "locked → refunded");
	Check(!CanTransition(MailTransferState::Locked, MailTransferState::Cancelled), "locked ↛ cancelled (or déjà prélevé)");
	Check(!CanTransition(MailTransferState::Pending, MailTransferState::Delivered), "pending ↛ delivered (rien à livrer)");
	Check(!CanTransition(MailTransferState::Cancelled, MailTransferState::Locked), "cancelled ↛ locked (annulation réelle)");
	Check(!CanTransition(MailTransferState::Delivered, MailTransferState::Refunded), "delivered terminal");
	Check(IsTerminal(MailTransferState::Refunded) && !IsTerminal(MailTransferState::Locked), "états terminaux");

	// 3. Identifiants et délai.
	Check(MakeMailTransferOperationId(42u) == "mail-42", "identifiant d'opération");
	Check(MakeMailboxDepositOperationId("mail-42", "refund") == "mail-42-refund", "identifiant de dépôt");
	Check(!PendingTransferExpired(1000u, 999u) && PendingTransferExpired(1000u, 1000u), "délai");

	if (s_failCount == 0)
		std::puts("[OK] mail_transfer_tests");
	return s_failCount == 0 ? 0 : 1;
}
