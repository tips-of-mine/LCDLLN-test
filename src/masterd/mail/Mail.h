#pragma once
// CMANGOS.18 (Phase 3.18a) — Mail core types : Mail struct + ItemAttachment.
// Minimal data structures pour la messagerie in-game cmangos.
// **Pur** : pas de DB persistence dans cette PR. MailManager utilise
// un store abstrait (cf. AbstractMailStore.h, InMemoryMailStore.h).
//
// Wire integration + persistance MySQL viendront en sub-PRs ulterieures.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::mail
{
	/// Item attache a un mail. Reference le template item + count.
	/// Pas de stat / proprietes ici — juste de quoi instancier l'item
	/// chez le destinataire au moment du Take.
	struct MailItemAttachment
	{
		uint32_t itemTemplateId = 0;
		uint32_t count          = 1;
	};

	/// Etat d'un mail dans la boite. Aligne sur le pattern cmangos.
	enum class MailState : uint8_t
	{
		Unread   = 0,  ///< Recu, pas encore ouvert.
		Read     = 1,  ///< Ouvert mais attachments encore presents.
		Returned = 2,  ///< Refuse → retourne expediteur (ex. boite pleine).
		Deleted  = 3,  ///< Supprime par le destinataire (placeholder).
	};

	/// Mail complet en memoire / serialise.
	struct Mail
	{
		uint64_t mailId       = 0;       ///< PK auto-increment.
		uint64_t senderAccountId   = 0;
		uint64_t receiverAccountId = 0;
		std::string subject;             ///< UTF-8, max 255 bytes.
		std::string body;                ///< UTF-8, max ~16k bytes.
		std::vector<MailItemAttachment> items;

		/// Or transferé (en copper, monnaie LCDLLN). 0 = pas de gold.
		uint64_t copperGold   = 0;

		/// Cash on Delivery — montant du a payer par le destinataire
		/// AVANT de retirer les items. 0 = pas de COD.
		uint64_t copperCod    = 0;

		uint64_t sentTsMs     = 0;       ///< epoch ms UTC.
		uint64_t expiresTsMs  = 0;       ///< epoch ms UTC ; 0 = jamais.
		MailState state       = MailState::Unread;
	};

	/// Limites canoniques (alignes sur les tables DB qui viendront).
	inline constexpr size_t kMaxMailSubjectBytes = 255;
	inline constexpr size_t kMaxMailBodyBytes    = 16 * 1024;
	inline constexpr size_t kMaxMailAttachments  = 12;
}
