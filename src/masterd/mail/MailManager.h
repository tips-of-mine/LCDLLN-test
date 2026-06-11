#pragma once
// CMANGOS.18 (Phase 3.18a) — MailManager : logique metier de la
// messagerie in-game. Validation + workflow Send / Take / Delete /
// ResolveExpired.
//
// **Pur dans cette PR** : pas de wiring opcodes, pas de notification
// reseau. Le manager opere sur un store abstrait (testable avec
// InMemoryMailStore). La couche reseau (MailHandler + opcodes wire)
// + la persistance MySQL viendront en sub-PRs ulterieures.

// Audit 2026-06-10 (Lot B1) — THREAD-SAFE : les handlers du master sont
// dispatchés sur un pool de workers NetServer (défaut 4) ; chaque méthode
// publique du manager verrouille m_mutex (sérialise les séquences
// lecture-modification-écriture sur le store, quel qu'il soit).

#include "src/masterd/mail/Mail.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace engine::server::mail
{
	/// Interface du store de mails. Production = MySqlMailStore (a
	/// venir), tests = InMemoryMailStore.
	class IMailStore
	{
	public:
		virtual ~IMailStore() = default;

		/// Insere un nouveau mail. Le store assigne le `mailId` et le
		/// retourne (ecrit dans `out.mailId`). Echec → 0.
		virtual uint64_t Insert(Mail& out) = 0;

		/// Cherche un mail par id. nullopt si pas trouve.
		virtual std::optional<Mail> Find(uint64_t mailId) const = 0;

		/// Tous les mails recus par \p receiverAccountId. Ordre indefini.
		virtual std::vector<Mail> ListInbox(uint64_t receiverAccountId) const = 0;

		/// Met a jour un mail existant (state, items, gold). Retourne false
		/// si pas trouve.
		virtual bool Update(const Mail& mail) = 0;

		/// Supprime un mail par id. Retourne false si pas trouve.
		virtual bool Delete(uint64_t mailId) = 0;

		/// Retourne les mailIds expires (expiresTsMs <= nowMs ET expiresTsMs > 0).
		/// Pas de side-effect — c'est `MailManager::ResolveExpired` qui
		/// les retourne ou les supprime.
		virtual std::vector<uint64_t> FindExpired(uint64_t nowMs) const = 0;
	};

	/// Resultat d'une operation MailManager.
	enum class MailOpResult : uint8_t
	{
		OK = 0,
		MailNotFound          = 1,
		WrongReceiver         = 2,    ///< Le mailId existe mais pas pour cet account.
		AlreadyTaken          = 3,    ///< Items / gold deja retires.
		CodNotPaid            = 4,    ///< COD requis avant Take.
		AttachmentsTooMany    = 5,
		SubjectTooLong        = 6,
		BodyTooLong           = 7,
	};

	struct MailManagerConfig
	{
		/// Duree par defaut avant expiration d'un mail recu (ms UTC).
		/// Default 30 jours. Caller peut override par mail via `expiresTsMs`.
		uint64_t defaultExpirationMs = 30ull * 24 * 60 * 60 * 1000;
	};

	class MailManager
	{
	public:
		explicit MailManager(IMailStore* store, MailManagerConfig cfg = {});

		const MailManagerConfig& Config() const noexcept { return m_cfg; }

		/// Envoie un mail. Valide les limites (subject/body/attachments).
		/// Si \p expiresTsMs == 0, le manager applique
		/// `nowMs + defaultExpirationMs`. Retourne `mailId` (>0) en cas
		/// de succes, 0 en cas d'echec.
		uint64_t Send(uint64_t senderAccountId, uint64_t receiverAccountId,
			std::string_view subject, std::string_view body,
			std::vector<MailItemAttachment> items,
			uint64_t copperGold, uint64_t copperCod,
			uint64_t nowMs, uint64_t expiresTsMs = 0,
			MailOpResult* outErr = nullptr);

		/// Marque un mail comme `Read`. Idempotent.
		MailOpResult MarkRead(uint64_t mailId, uint64_t receiverAccountId);

		/// Retire les items du mail. Avant retrait :
		/// - si `copperCod > 0`, exige `paidCopper >= copperCod` (le caller
		///   a deja preleve le gold du wallet du receiver et l'a credite
		///   au sender).
		/// - vide la liste d'items du mail (atomic en RAM ; le caller doit
		///   wrapper en transaction DB en production).
		/// Retourne les items extraits dans \p outItems en cas de succes.
		MailOpResult TakeItems(uint64_t mailId, uint64_t receiverAccountId,
			uint64_t paidCopper, std::vector<MailItemAttachment>& outItems);

		/// Retire le gold du mail. Met `copperGold` a 0.
		MailOpResult TakeGold(uint64_t mailId, uint64_t receiverAccountId,
			uint64_t& outCopper);

		/// Supprime un mail. Echec si items / gold encore presents (le
		/// caller doit Take avant Delete). Use case : delete vide = retrait
		/// final apres Take.
		MailOpResult Delete(uint64_t mailId, uint64_t receiverAccountId);

		/// Retourne tous les mails de l'inbox du receveur, tries par
		/// `sentTsMs` decroissant (plus recent en premier).
		std::vector<Mail> Inbox(uint64_t receiverAccountId) const;

		/// Resout les mails expires : items + gold non retires retournent
		/// au sender (sub-PR ulterieure : pour cette PR, on retourne juste
		/// les mailIds candidates, le caller decide quoi faire).
		std::vector<uint64_t> ResolveExpired(uint64_t nowMs) const;

	private:
		/// Audit Lot B1 — sérialise les opérations mail entre workers (les
		/// séquences Find→Update du store ne sont pas atomiques sans ça).
		mutable std::mutex m_mutex;
		IMailStore*       m_store;
		MailManagerConfig m_cfg;
	};
}
