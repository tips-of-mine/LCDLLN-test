// CMANGOS.18 (Phase 3.18 step 3) — Implementation MailHandler.

#include "src/masterd/handlers/mail/MailHandler.h"

#include "src/masterd/mail/Mail.h"
#include "src/masterd/mail/MailManager.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/masterd/mail/MailTransferService.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/ShardPayloads.h"

#include <limits>
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
		// ETAPE5.6 — or joint : il faut être en jeu (le shard propriétaire prélève l'or) et
		// que le transfert en deux phases soit disponible.
		std::optional<SessionCharacterMap::CharacterInfo> senderChar;
		std::optional<uint32_t> senderShardConn;
		if (parsed->copperGold > 0u)
		{
			if (m_transfers == nullptr || !m_transfers->IsAvailable() || m_charMap == nullptr || m_registry == nullptr
				|| parsed->copperGold > std::numeric_limits<uint32_t>::max())
			{
				auto pkt = BuildMailSendResponsePacket(static_cast<uint8_t>(MailSendErrorCode::TransferUnavailable), 0ull,
					requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
			senderChar = m_charMap->GetByConnId(connId);
			if (senderChar)
				senderShardConn = m_registry->GetShardConnection(senderChar->shardId);
			if (!senderChar || senderChar->accountId != senderAccountId || senderChar->characterId == 0u
				|| senderChar->shardId == 0u || !senderShardConn)
			{
				auto pkt = BuildMailSendResponsePacket(static_cast<uint8_t>(MailSendErrorCode::NotInWorld), 0ull,
					requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
		}

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

		// ETAPE5.6 — phase 1 : transfert « pending » puis demande de prélèvement au shard de
		// l'expéditeur. L'or n'est retirable qu'une fois prélevé (locked). Sans réponse dans
		// le délai, SweepExpiredTransfers annule (l'expéditeur n'a rien perdu).
		if (mailId != 0u && parsed->copperGold > 0u && senderChar)
		{
			MailTransferRow row;
			row.mailId = mailId;
			row.senderAccountId = senderAccountId;
			row.senderCharacterId = senderChar->characterId;
			row.senderShardId = senderChar->shardId;
			row.receiverAccountId = parsed->recipientAccountId;
			row.gold = parsed->copperGold;
			row.deadlineMs = NowUnixMsUtc() + engine::mail::kMailDebitMaxDelayMs;
			if (!m_transfers->CreatePending(row))
			{
				// Pas de transfert : le courrier ne doit pas porter d'or fictif.
				uint64_t ignored = 0;
				(void)m_mgr->TakeGold(mailId, parsed->recipientAccountId, ignored);
				(void)m_mgr->Delete(mailId, parsed->recipientAccountId);
				auto pkt = BuildMailSendResponsePacket(static_cast<uint8_t>(MailSendErrorCode::TransferUnavailable), 0ull,
					requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
			engine::network::MailDebitPayload debit;
			debit.operationId = engine::mail::MakeMailTransferOperationId(mailId);
			debit.characterId = senderChar->characterId;
			debit.gold = parsed->copperGold;
			const auto pkt = engine::network::BuildMailDebitPacket(debit);
			const bool sent = !pkt.empty() && m_server->Send(*senderShardConn, pkt);
			LOG_INFO(Net, "[MailHandler] ETAPE5.6 prélèvement demandé (operation={}, character={}, shard={}, gold={}, envoye={})",
				debit.operationId, debit.characterId, senderChar->shardId, debit.gold, sent);
		}

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

		// ETAPE5.6 — crédit RÉEL avant de vider les pièces jointes :
		//   - courrier de joueur avec transfert : livraison de l'or prélevé (deux phases) ;
		//   - courrier système (expéditeur 0, ex. cadeau d'anniversaire) : dépôt direct de
		//     l'or et des objets ;
		//   - ancien courrier de joueur sans transfert : or jamais prélevé, rien à créditer.
		// Le personnage crédité est celui de la connexion (il faut être en jeu) ; son shard
		// le crédite aussitôt (ordre 215). Les contrôles du courrier (existence,
		// destinataire, contre-remboursement) passent AVANT tout crédit.
		uint64_t deliveredGold = 0;
		const bool transfersOn = m_transfers != nullptr && m_transfers->IsAvailable();
		if (transfersOn)
		{
			const auto mail = m_mgr->Peek(parsed->mailId);
			if (!mail || mail->receiverAccountId != accountId
				|| (mail->copperCod > 0u && parsed->paidCopperCod < mail->copperCod))
			{
				const MailTakeErrorCode code = !mail ? MailTakeErrorCode::NotFound
					: (mail->receiverAccountId != accountId ? MailTakeErrorCode::WrongReceiver : MailTakeErrorCode::CodNotPaid);
				auto pkt = BuildMailTakeAttachmentsResponsePacket(static_cast<uint8_t>(code), parsed->mailId, 0ull,
					requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
			std::optional<SessionCharacterMap::CharacterInfo> receiverChar;
			if (m_charMap != nullptr)
				receiverChar = m_charMap->GetByConnId(connId);
			const bool inWorld = receiverChar && receiverChar->accountId == accountId && receiverChar->characterId != 0u;
			const auto row = m_transfers->FindByMail(parsed->mailId);
			const bool systemMail = mail->senderAccountId == 0u && (mail->copperGold > 0u || !mail->items.empty());
			const bool hasCredit = (row && (row->state == engine::mail::MailTransferState::Locked
					|| row->state == engine::mail::MailTransferState::Pending))
				|| (!row && systemMail);
			auto reply = [&](MailTakeErrorCode code) {
				auto pkt = BuildMailTakeAttachmentsResponsePacket(static_cast<uint8_t>(code), parsed->mailId, 0ull,
					requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
			};
			if (hasCredit && !inWorld)
			{
				reply(row && row->state == engine::mail::MailTransferState::Pending
					? MailTakeErrorCode::TransferPending : MailTakeErrorCode::NotInWorld);
				return;
			}
			MailDeliverResult delivered = MailDeliverResult::NoTransfer;
			if (row)
			{
				delivered = inWorld ? m_transfers->Deliver(parsed->mailId, accountId, receiverChar->characterId)
					: (row->state == engine::mail::MailTransferState::Delivered ? MailDeliverResult::AlreadyDelivered
						: MailDeliverResult::Cancelled);
				if (delivered == MailDeliverResult::Delivered)
					deliveredGold = row->gold;
			}
			else if (systemMail)
			{
				std::vector<std::pair<uint32_t, uint32_t>> items;
				for (const auto& it : mail->items)
					items.emplace_back(it.itemTemplateId, it.count);
				delivered = m_transfers->DepositSystemMail(parsed->mailId, accountId, receiverChar->characterId,
					mail->copperGold, items);
				if (delivered == MailDeliverResult::Delivered)
					deliveredGold = mail->copperGold;
			}
			else if (mail->copperGold > 0u)
			{
				LOG_INFO(Net, "[MailHandler] ETAPE5.6 courrier {} sans transfert : or fictif non crédité", parsed->mailId);
			}
			if (delivered == MailDeliverResult::Pending || delivered == MailDeliverResult::Failed)
			{
				reply(MailTakeErrorCode::TransferPending); // rien n'a changé : réessayer
				return;
			}
			// Crédit déposé (maintenant ou lors d'un essai précédent dont la réponse s'est
			// perdue) : le shard du destinataire le crédite sans attendre la reconnexion.
			if ((delivered == MailDeliverResult::Delivered || delivered == MailDeliverResult::AlreadyDelivered)
				&& m_registry != nullptr && inWorld)
			{
				if (auto shardConn = m_registry->GetShardConnection(receiverChar->shardId))
				{
					const auto poke = engine::network::BuildMailboxPokePacket(receiverChar->characterId);
					if (!poke.empty())
						(void)m_server->Send(*shardConn, poke);
				}
			}
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

		// ETAPE5.6 — montant RÉELLEMENT crédité (0 sans transfert livré).
		goldTaken = transfersOn ? deliveredGold : goldTaken;
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

	// -------------------------------------------------------------------------
	// ETAPE5.6 — or joint à un courrier : réponses des shards, délais, annulation.

	void MailHandler::HandleShardDebitResult(uint32_t connId, const uint8_t* payload, size_t payloadSize)
	{
		if (m_transfers == nullptr || m_registry == nullptr)
			return;
		// Avant toute lecture en base : la connexion doit être celle d'un shard enregistré
		// (un client qui enverrait 214 ne provoque aucune requête).
		bool fromShard = false;
		for (const auto& shard : m_registry->ListShards())
		{
			const auto c = m_registry->GetShardConnection(shard.shard_id);
			if (c && *c == connId)
			{
				fromShard = true;
				break;
			}
		}
		if (!fromShard)
		{
			LOG_WARN(Net, "[MailHandler] ETAPE5.6 réponse de prélèvement hors connexion de shard ignorée (connId={})", connId);
			return;
		}
		const auto parsed = engine::network::ParseMailDebitResultPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[MailHandler] ETAPE5.6 réponse de prélèvement illisible (connId={})", connId);
			return;
		}
		const auto row = m_transfers->FindByOperation(parsed->operationId);
		if (!row)
		{
			LOG_WARN(Net, "[MailHandler] ETAPE5.6 réponse pour un transfert inconnu (operation={})", parsed->operationId);
			return;
		}
		// Seul le shard chargé du prélèvement peut répondre (connexion du registre).
		const auto shardConn = m_registry->GetShardConnection(row->senderShardId);
		if (!shardConn || *shardConn != connId)
		{
			LOG_WARN(Net, "[MailHandler] ETAPE5.6 réponse refusée : connexion {} n'est pas celle du shard {} (operation={})",
				connId, row->senderShardId, parsed->operationId);
			return;
		}
		const auto result = static_cast<engine::mail::MailDebitResult>(parsed->result);
		if (result == engine::mail::MailDebitResult::Debited)
		{
			LOG_INFO(Net, "[MailHandler] ETAPE5.6 or prélevé (operation={}, gold={})", row->operationId, row->gold);
			return;
		}
		if (m_transfers->CancelPending(row->operationId))
			OnTransferCancelled(*row, "prélèvement refusé par le shard");
		else
			LOG_INFO(Net, "[MailHandler] ETAPE5.6 refus du shard sans effet (operation={}, deja traité)", row->operationId);
	}

	void MailHandler::SweepExpiredTransfers(uint64_t nowMs)
	{
		if (m_transfers == nullptr || !m_transfers->IsAvailable())
			return;
		for (const MailTransferRow& row : m_transfers->ListExpiredPending(nowMs))
		{
			// Compare-and-set : si le shard a prélevé juste avant, l'annulation échoue et
			// le transfert reste valable (locked).
			if (m_transfers->CancelPending(row.operationId))
				OnTransferCancelled(row, "délai de prélèvement dépassé");
		}
	}

	void MailHandler::OnTransferCancelled(const MailTransferRow& row, const char* why)
	{
		// L'or n'a jamais quitté l'expéditeur : il disparaît du courrier.
		if (m_mgr != nullptr)
		{
			uint64_t ignored = 0;
			(void)m_mgr->TakeGold(row.mailId, row.receiverAccountId, ignored);
		}
		LOG_WARN(Net, "[MailHandler] ETAPE5.6 envoi d'or annulé (operation={}, gold={}) : {}", row.operationId, row.gold, why);
		if (m_server == nullptr || m_connMap == nullptr || m_sessionMgr == nullptr)
			return;
		const uint64_t ts = NowUnixMsUtc();
		const std::string text = "Envoi de " + std::to_string(row.gold) + " pièces d'or annulé (" + why
			+ ") : votre or ne vous a pas été retiré.";
		for (const auto& [conn, sessId] : m_connMap->Snapshot())
		{
			const auto acc = m_sessionMgr->GetAccountId(sessId);
			if (!acc || *acc != row.senderAccountId)
				continue;
			auto pkt = engine::network::BuildChatRelayPacket(ts, static_cast<uint8_t>(engine::net::ChatChannel::Server),
				"[Courrier]", text, sessId);
			if (!pkt.empty())
				m_server->Send(conn, pkt);
		}
	}
}
