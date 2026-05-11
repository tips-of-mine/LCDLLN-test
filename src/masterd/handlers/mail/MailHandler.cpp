// CMANGOS.18 (Phase 3.18 step 3) — Implementation MailHandler.

#include "src/masterd/handlers/mail/MailHandler.h"

#include "src/masterd/mail/Mail.h"
#include "src/masterd/mail/MailManager.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/MailPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>

namespace engine::server
{
	namespace
	{
		/// Renvoie l'epoch ms UTC. Aligné sur les autres handlers (ex. ChatRelayHandler).
		uint64_t NowUnixMsUtc()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}

		/// Convertit un MailOpResult (côté manager) en MailSendErrorCode (côté wire).
		uint8_t MapSendError(engine::server::mail::MailOpResult r)
		{
			using engine::server::mail::MailOpResult;
			using engine::network::MailSendErrorCode;
			switch (r)
			{
			case MailOpResult::OK:                  return static_cast<uint8_t>(MailSendErrorCode::Ok);
			case MailOpResult::SubjectTooLong:      return static_cast<uint8_t>(MailSendErrorCode::SubjectTooLong);
			case MailOpResult::BodyTooLong:         return static_cast<uint8_t>(MailSendErrorCode::BodyTooLong);
			case MailOpResult::AttachmentsTooMany:  return static_cast<uint8_t>(MailSendErrorCode::AttachmentsTooMany);
			default:
				// Le manager peut retourner MailNotFound / WrongReceiver / AlreadyTaken /
				// CodNotPaid pour Send (ne devrait pas dans les faits) — on les rabat sur
				// RecipientNotFound car c'est ce qui s'en rapproche côté UI.
				return static_cast<uint8_t>(MailSendErrorCode::RecipientNotFound);
			}
		}

		/// Convertit un MailOpResult en MailDeleteErrorCode wire.
		uint8_t MapDeleteError(engine::server::mail::MailOpResult r)
		{
			using engine::server::mail::MailOpResult;
			using engine::network::MailDeleteErrorCode;
			switch (r)
			{
			case MailOpResult::OK:            return static_cast<uint8_t>(MailDeleteErrorCode::Ok);
			case MailOpResult::MailNotFound:  return static_cast<uint8_t>(MailDeleteErrorCode::NotFound);
			case MailOpResult::WrongReceiver: return static_cast<uint8_t>(MailDeleteErrorCode::WrongReceiver);
			case MailOpResult::AlreadyTaken:  return static_cast<uint8_t>(MailDeleteErrorCode::HasAttachments);
			default:                          return static_cast<uint8_t>(MailDeleteErrorCode::NotFound);
			}
		}
	}

	void MailHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_mgr || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[MailHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Résolution session/account. Si l'un manque, on retourne une réponse
		// type-specific avec error=Unauthorized (le client peut alors prompter
		// une re-auth). Pas d'ErrorPacket générique : la réponse mail est
		// plus simple à exploiter dans l'UI mailbox.
		uint64_t accountId = 0;
		bool sessionOk = false;
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (connSessionId && *connSessionId != 0u
			&& sessionIdHeader != 0u && *connSessionId == sessionIdHeader)
		{
			auto acc = m_sessionMgr->GetAccountId(*connSessionId);
			if (acc && *acc != 0u)
			{
				accountId = *acc;
				sessionOk = true;
			}
		}

		if (!sessionOk)
		{
			// Construire la réponse Unauthorized type-specific selon l'opcode.
			std::vector<uint8_t> pkt;
			switch (opcode)
			{
			case kOpcodeMailSendRequest:
				pkt = BuildMailSendResponsePacket(
					static_cast<uint8_t>(MailSendErrorCode::Unauthorized), 0ull,
					requestId, sessionIdHeader);
				break;
			case kOpcodeMailListInboxRequest:
				pkt = BuildMailListInboxResponsePacket(
					static_cast<uint8_t>(MailSendErrorCode::Unauthorized), {},
					requestId, sessionIdHeader);
				break;
			case kOpcodeMailReadRequest:
				pkt = BuildMailReadResponsePacket(
					static_cast<uint8_t>(MailReadErrorCode::Unauthorized), 0ull, "",
					requestId, sessionIdHeader);
				break;
			case kOpcodeMailTakeAttachmentsRequest:
				pkt = BuildMailTakeAttachmentsResponsePacket(
					static_cast<uint8_t>(MailTakeErrorCode::Unauthorized), 0ull, 0ull,
					requestId, sessionIdHeader);
				break;
			case kOpcodeMailDeleteRequest:
				pkt = BuildMailDeleteResponsePacket(
					static_cast<uint8_t>(MailDeleteErrorCode::Unauthorized), 0ull,
					requestId, sessionIdHeader);
				break;
			default:
				return; // pas un opcode Mail : ignore.
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeMailSendRequest:
			HandleSend(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeMailListInboxRequest:
			HandleListInbox(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeMailReadRequest:
			HandleRead(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeMailTakeAttachmentsRequest:
			HandleTakeAttachments(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeMailDeleteRequest:
			HandleDelete(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			// Pas un opcode Mail : ignore silencieusement.
			break;
		}
	}

	// -------------------------------------------------------------------------

	void MailHandler::HandleSend(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t senderAccountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseMailSendRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildMailSendResponsePacket(
				static_cast<uint8_t>(MailSendErrorCode::RecipientNotFound), 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// MailManager::Send valide les limites subject/body/attachments. Pour
		// la résolution recipient → exists check : si le store DB renvoie 0
		// d'inserts à cause d'une FK violation, on retournera RecipientNotFound.
		// Le manager actuel insère sans vérifier l'existence du receiver côté
		// accounts — TBD step 3.b si on veut un précheck explicite (perf : un
		// SELECT id FROM accounts WHERE id=? avant Insert).
		engine::server::mail::MailOpResult opErr = engine::server::mail::MailOpResult::OK;
		const uint64_t mailId = m_mgr->Send(
			senderAccountId,
			parsed->recipientAccountId,
			parsed->subject,
			parsed->body,
			/*items*/ {},
			parsed->copperGold,
			parsed->copperCod,
			NowUnixMsUtc(),
			/*expiresTsMs*/ 0ull,
			&opErr);

		uint8_t errCode = MapSendError(opErr);
		if (mailId == 0ull && opErr == engine::server::mail::MailOpResult::OK)
		{
			// Insert DB a échoué (FK invalide, store down, …). Le manager retourne
			// 0 sans set outErr dans ce chemin → on rabat sur RecipientNotFound.
			errCode = static_cast<uint8_t>(MailSendErrorCode::RecipientNotFound);
		}

		LOG_INFO(Net, "[MailHandler] Send sender={} recipient={} mailId={} err={}",
			senderAccountId, parsed->recipientAccountId, mailId, static_cast<int>(opErr));

		auto pkt = BuildMailSendResponsePacket(errCode, mailId, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void MailHandler::HandleListInbox(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		std::vector<engine::server::mail::Mail> inbox = m_mgr->Inbox(accountId);
		std::vector<MailInboxEntry> entries;
		entries.reserve(inbox.size());
		for (const auto& m : inbox)
		{
			MailInboxEntry e;
			e.mailId          = m.mailId;
			e.senderAccountId = m.senderAccountId;
			e.subject         = m.subject;
			e.sentTsMs        = m.sentTsMs;
			e.expiresTsMs     = m.expiresTsMs;
			e.state           = static_cast<uint8_t>(m.state);
			e.copperGold      = m.copperGold;
			e.copperCod       = m.copperCod;
			entries.push_back(std::move(e));
		}

		LOG_INFO(Net, "[MailHandler] ListInbox account={} count={}", accountId, entries.size());

		auto pkt = BuildMailListInboxResponsePacket(0u, entries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void MailHandler::HandleRead(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseMailReadRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildMailReadResponsePacket(
				static_cast<uint8_t>(MailReadErrorCode::NotFound), 0ull, "",
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const auto opErr = m_mgr->MarkRead(parsed->mailId, accountId);
		if (opErr != engine::server::mail::MailOpResult::OK)
		{
			using engine::server::mail::MailOpResult;
			uint8_t code = static_cast<uint8_t>(MailReadErrorCode::NotFound);
			if (opErr == MailOpResult::WrongReceiver)
				code = static_cast<uint8_t>(MailReadErrorCode::WrongReceiver);
			auto pkt = BuildMailReadResponsePacket(code, parsed->mailId, "",
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Pour récupérer le body, on relit l'inbox du destinataire et on cherche
		// le mailId. C'est suboptimal (full-scan), mais le MailManager actuel
		// ne fournit pas de Find direct par (mailId, receiverAccountId).
		// step 3.b : ajouter un IMailStore::FindForReceiver pour éviter le scan.
		std::string body;
		const auto inbox = m_mgr->Inbox(accountId);
		for (const auto& m : inbox)
		{
			if (m.mailId == parsed->mailId)
			{
				body = m.body;
				break;
			}
		}

		LOG_INFO(Net, "[MailHandler] Read account={} mailId={} bodyLen={}",
			accountId, parsed->mailId, body.size());

		auto pkt = BuildMailReadResponsePacket(
			static_cast<uint8_t>(MailReadErrorCode::Ok), parsed->mailId, body,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void MailHandler::HandleTakeAttachments(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseMailTakeAttachmentsRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildMailTakeAttachmentsResponsePacket(
				static_cast<uint8_t>(MailTakeErrorCode::NotFound), 0ull, 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Cette PR ne câble que le retrait du gold. Les items seront wirés en
		// step 3.b une fois la spec d'inventaire stabilisée. On appelle quand
		// même TakeItems pour valider COD + retirer les items côté store (s'ils
		// existent), mais on ignore outItems pour la réponse wire.
		std::vector<engine::server::mail::MailItemAttachment> outItems;
		const auto itemsErr = m_mgr->TakeItems(parsed->mailId, accountId,
			parsed->paidCopperCod, outItems);

		// On accepte AlreadyTaken pour les items (mail vide d'items, juste du
		// gold) — on enchaîne sur TakeGold dans tous les cas où l'erreur n'est
		// pas un blocage receiver/COD.
		using engine::server::mail::MailOpResult;
		if (itemsErr != MailOpResult::OK
			&& itemsErr != MailOpResult::AlreadyTaken)
		{
			uint8_t code;
			switch (itemsErr)
			{
			case MailOpResult::MailNotFound:  code = static_cast<uint8_t>(MailTakeErrorCode::NotFound); break;
			case MailOpResult::WrongReceiver: code = static_cast<uint8_t>(MailTakeErrorCode::WrongReceiver); break;
			case MailOpResult::CodNotPaid:    code = static_cast<uint8_t>(MailTakeErrorCode::CodNotPaid); break;
			default:                          code = static_cast<uint8_t>(MailTakeErrorCode::NotFound); break;
			}
			auto pkt = BuildMailTakeAttachmentsResponsePacket(code, parsed->mailId, 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		uint64_t goldTaken = 0ull;
		const auto goldErr = m_mgr->TakeGold(parsed->mailId, accountId, goldTaken);
		// AlreadyTaken pour le gold = mail sans gold → OK quand même (le caller
		// a peut-être seulement voulu prendre les items).
		uint8_t code = static_cast<uint8_t>(MailTakeErrorCode::Ok);
		if (goldErr != MailOpResult::OK && goldErr != MailOpResult::AlreadyTaken)
		{
			switch (goldErr)
			{
			case MailOpResult::MailNotFound:  code = static_cast<uint8_t>(MailTakeErrorCode::NotFound); break;
			case MailOpResult::WrongReceiver: code = static_cast<uint8_t>(MailTakeErrorCode::WrongReceiver); break;
			default:                          code = static_cast<uint8_t>(MailTakeErrorCode::NotFound); break;
			}
		}

		LOG_INFO(Net, "[MailHandler] Take account={} mailId={} goldTaken={} code={}",
			accountId, parsed->mailId, goldTaken, static_cast<int>(code));

		auto pkt = BuildMailTakeAttachmentsResponsePacket(code, parsed->mailId, goldTaken,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void MailHandler::HandleDelete(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseMailDeleteRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildMailDeleteResponsePacket(
				static_cast<uint8_t>(MailDeleteErrorCode::NotFound), 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		const auto opErr = m_mgr->Delete(parsed->mailId, accountId);
		const uint8_t code = MapDeleteError(opErr);

		LOG_INFO(Net, "[MailHandler] Delete account={} mailId={} code={}",
			accountId, parsed->mailId, static_cast<int>(code));

		auto pkt = BuildMailDeleteResponsePacket(code, parsed->mailId,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
