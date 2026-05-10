// CMANGOS.27 (Phase 4.27 step 3+4) -- Implementation TradeHandler.

#include "src/masterd/handlers/trade/TradeHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/masterd/trade/TradeSessionRegistry.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/TradePayloads.h"

#include <utility>

namespace engine::server
{
	namespace
	{
		/// Convertit une TradeState (enum class) en uint8_t pour le wire.
		uint8_t StateToWire(engine::server::trade::TradeState s)
		{
			return static_cast<uint8_t>(s);
		}

		/// Renvoie l'offer du joueur sender dans la session (cote sender).
		/// Utile pour pousser au partenaire l'etat *vu cote sender* en miroir.
		const engine::server::trade::TradeOffer&
		OfferOf(const engine::server::trade::TradeSession& sess, uint64_t accountId)
		{
			if (accountId == sess.PlayerA()) return sess.OfferA();
			return sess.OfferB();
		}
	}

	// =========================================================================
	// HandlePacket : entry point dispatch + session/account resolution
	// =========================================================================

	void TradeHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_reg || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[TradeHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account. Si l'un manque, on retourne une reponse
		// type-specific avec error=Unauthorized.
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
			std::vector<uint8_t> pkt;
			const uint8_t kUnauth = static_cast<uint8_t>(TradeErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeTradeBeginRequest:
				pkt = BuildTradeBeginResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeTradeSetOfferRequest:
				pkt = BuildTradeSetOfferResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeTradeLockRequest:
				pkt = BuildTradeLockResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeTradeCommitRequest:
				pkt = BuildTradeCommitResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeTradeCancelRequest:
				// Cancel n'a pas de response wire dediee : on reutilise la
				// notification cote sender pour signaler l'echec d'auth.
				pkt = BuildTradeCancelNotificationPacket(0u, "unauthorized", sessionIdHeader);
				break;
			default:
				return;
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeTradeBeginRequest:
			HandleBegin(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeTradeSetOfferRequest:
			HandleSetOffer(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeTradeLockRequest:
			HandleLock(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeTradeCommitRequest:
			HandleCommit(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeTradeCancelRequest:
			HandleCancel(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// =========================================================================
	// FindConnIdForAccount / FindSessionIdForAccount
	// =========================================================================

	uint32_t TradeHandler::FindConnIdForAccount(uint64_t accountId) const
	{
		if (!m_connMap || !m_sessionMgr) return 0u;
		const auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessionId] : snapshot)
		{
			auto acc = m_sessionMgr->GetAccountId(sessionId);
			if (acc && *acc == accountId)
				return connId;
		}
		return 0u;
	}

	uint64_t TradeHandler::FindSessionIdForAccount(uint64_t accountId) const
	{
		if (!m_connMap || !m_sessionMgr) return 0u;
		const auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessionId] : snapshot)
		{
			(void)connId;
			auto acc = m_sessionMgr->GetAccountId(sessionId);
			if (acc && *acc == accountId)
				return sessionId;
		}
		return 0u;
	}

	bool TradeHandler::SendPacketToAccount(uint64_t accountId, const std::vector<uint8_t>& packet)
	{
		if (packet.empty()) return false;
		const uint32_t connId = FindConnIdForAccount(accountId);
		if (connId == 0u) return false;
		(void)m_server->Send(connId, packet);
		return true;
	}

	// =========================================================================
	// HandleBegin
	// =========================================================================

	void TradeHandler::HandleBegin(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseTradeBeginRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Begin parse failed account={} size={}", accountId, payloadSize);
			return;
		}

		const uint64_t targetAcc = parsed->targetAccountId;

		// Self-trade : explicitement rejete (le client peut tenter par bug
		// d'UI, et la TradeSession::SetOffer ne distinguerait pas A == B).
		if (targetAcc == 0u || targetAcc == accountId)
		{
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::SelfTrade), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Begin SelfTrade account={}", accountId);
			return;
		}

		// Verifie que ni A ni B ne sont deja dans une autre trade.
		if (m_reg->IsInTrade(accountId) || m_reg->IsInTrade(targetAcc))
		{
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::PartnerInTrade), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Begin PartnerInTrade sender={} target={}",
				accountId, targetAcc);
			return;
		}

		// Verifie que le target est online (a une connexion active sur le master).
		const uint32_t targetConn = FindConnIdForAccount(targetAcc);
		const uint64_t targetSessionIdHeader = FindSessionIdForAccount(targetAcc);
		if (targetConn == 0u || targetSessionIdHeader == 0u)
		{
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::PartnerOffline), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Begin PartnerOffline sender={} target={}",
				accountId, targetAcc);
			return;
		}

		// Cree la session.
		const uint64_t sid = m_reg->Begin(accountId, targetAcc);
		if (sid == 0u)
		{
			// Race : registry a refuse (l'un des 2 a entame une trade entre
			// nos checks). On retourne PartnerInTrade.
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::PartnerInTrade), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Begin race PartnerInTrade sender={} target={}",
				accountId, targetAcc);
			return;
		}

		// Reponse au sender (initiateur).
		{
			auto pkt = BuildTradeBeginResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::Ok), sid, targetAcc,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		}

		// Push BeginNotification au target (avec son propre sessionIdHeader).
		{
			auto pkt = BuildTradeBeginNotificationPacket(sid, accountId, targetSessionIdHeader);
			if (!pkt.empty())
				m_server->Send(targetConn, pkt);
		}

		LOG_INFO(Net, "[TradeHandler] Begin OK sender={} target={} sid={}",
			accountId, targetAcc, sid);
	}

	// =========================================================================
	// HandleSetOffer
	// =========================================================================

	void TradeHandler::HandleSetOffer(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseTradeSetOfferRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		auto* sess = m_reg->GetById(parsed->sessionId);
		if (!sess)
		{
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] SetOffer InvalidSession account={} sid={}",
				accountId, parsed->sessionId);
			return;
		}

		if (sess->PlayerA() != accountId && sess->PlayerB() != accountId)
		{
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::NotPartOfSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] SetOffer NotPartOfSession account={} sid={}",
				accountId, parsed->sessionId);
			return;
		}

		// Etats terminaux : refuse.
		if (sess->State() == engine::server::trade::TradeState::Committed
		 || sess->State() == engine::server::trade::TradeState::Cancelled)
		{
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::SessionTerminal),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		engine::server::trade::TradeOffer offer;
		offer.copper = parsed->copperGold;
		offer.items  = parsed->itemGuids;

		const bool ok = sess->SetOffer(accountId, std::move(offer));
		if (!ok)
		{
			// Le seul cas de refus restant cote SetOffer (apres les checks
			// ci-dessus) : tentative d'editer son offer alors qu'on est deja
			// locke (LockedA/B/BothLocked cote sender).
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::WrongState),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] SetOffer WrongState account={} sid={} state={}",
				accountId, parsed->sessionId, static_cast<unsigned>(sess->State()));
			return;
		}

		// Reponse au sender.
		{
			auto pkt = BuildTradeSetOfferResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::Ok),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		}

		// Push StateUpdateNotification au partenaire avec l'offer mise a jour
		// du sender (le partenaire voit "l'autre cote").
		const uint64_t partnerAcc = (accountId == sess->PlayerA())
			? sess->PlayerB() : sess->PlayerA();
		const uint32_t partnerConn = FindConnIdForAccount(partnerAcc);
		const uint64_t partnerSessionId = FindSessionIdForAccount(partnerAcc);
		if (partnerConn != 0u && partnerSessionId != 0u)
		{
			const auto& senderOffer = OfferOf(*sess, accountId);
			auto pkt = BuildTradeStateUpdateNotificationPacket(
				parsed->sessionId, StateToWire(sess->State()),
				senderOffer.copper, senderOffer.items, partnerSessionId);
			if (!pkt.empty()) m_server->Send(partnerConn, pkt);
		}

		LOG_INFO(Net, "[TradeHandler] SetOffer OK account={} sid={} items={} gold={}",
			accountId, parsed->sessionId, parsed->itemGuids.size(), parsed->copperGold);
	}

	// =========================================================================
	// HandleLock
	// =========================================================================

	void TradeHandler::HandleLock(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseTradeLockRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildTradeLockResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		auto* sess = m_reg->GetById(parsed->sessionId);
		if (!sess)
		{
			auto pkt = BuildTradeLockResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (sess->PlayerA() != accountId && sess->PlayerB() != accountId)
		{
			auto pkt = BuildTradeLockResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::NotPartOfSession), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		const bool ok = sess->Lock(accountId);
		if (!ok)
		{
			auto pkt = BuildTradeLockResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::WrongState),
				StateToWire(sess->State()),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Lock WrongState account={} sid={}",
				accountId, parsed->sessionId);
			return;
		}

		const uint8_t newState = StateToWire(sess->State());

		// Reponse au sender.
		{
			auto pkt = BuildTradeLockResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::Ok), newState,
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		}

		// Push StateUpdateNotification au partenaire pour qu'il voie le nouveau
		// state (ex : Open -> LockedA cote l'autre, ou LockedB -> BothLocked).
		// On envoie l'offer du sender -- meme inchangee, c'est ce que le
		// partenaire affiche dans son UI miroir.
		const uint64_t partnerAcc = (accountId == sess->PlayerA())
			? sess->PlayerB() : sess->PlayerA();
		const uint32_t partnerConn = FindConnIdForAccount(partnerAcc);
		const uint64_t partnerSessionId = FindSessionIdForAccount(partnerAcc);
		if (partnerConn != 0u && partnerSessionId != 0u)
		{
			const auto& senderOffer = OfferOf(*sess, accountId);
			auto pkt = BuildTradeStateUpdateNotificationPacket(
				parsed->sessionId, newState,
				senderOffer.copper, senderOffer.items, partnerSessionId);
			if (!pkt.empty()) m_server->Send(partnerConn, pkt);
		}

		LOG_INFO(Net, "[TradeHandler] Lock OK account={} sid={} newState={}",
			accountId, parsed->sessionId, static_cast<unsigned>(newState));
	}

	// =========================================================================
	// HandleCommit
	// =========================================================================

	void TradeHandler::HandleCommit(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseTradeCommitRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildTradeCommitResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		auto* sess = m_reg->GetById(parsed->sessionId);
		if (!sess)
		{
			auto pkt = BuildTradeCommitResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::InvalidSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (sess->PlayerA() != accountId && sess->PlayerB() != accountId)
		{
			auto pkt = BuildTradeCommitResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::NotPartOfSession),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		// V1 : on ne valide / n'applique PAS le delta inventory ici. On se
		// contente d'avancer la FSM. La transaction reelle (lock items,
		// transfert, application gold) viendra avec l'integration wallet.
		const bool ok = sess->Commit();
		if (!ok)
		{
			auto pkt = BuildTradeCommitResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::WrongState),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_WARN(Net, "[TradeHandler] Commit WrongState account={} sid={} state={}",
				accountId, parsed->sessionId, static_cast<unsigned>(sess->State()));
			return;
		}

		// Reponse au sender.
		{
			auto pkt = BuildTradeCommitResponsePacket(
				static_cast<uint8_t>(TradeErrorCode::Ok),
				requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		}

		// Push StateUpdateNotification(Committed) aux 2 participants. Pour
		// chacun, on envoie l'offer *de l'autre* avec state=Committed pour que
		// l'UI puisse confirmer le contenu finalise.
		const uint64_t accA = sess->PlayerA();
		const uint64_t accB = sess->PlayerB();
		const auto& offerA = sess->OfferA();
		const auto& offerB = sess->OfferB();

		const uint64_t sessA = FindSessionIdForAccount(accA);
		const uint64_t sessB = FindSessionIdForAccount(accB);
		const uint32_t connA = FindConnIdForAccount(accA);
		const uint32_t connB = FindConnIdForAccount(accB);

		// A voit l'offer de B avec state=Committed.
		if (connA != 0u && sessA != 0u)
		{
			auto pkt = BuildTradeStateUpdateNotificationPacket(
				parsed->sessionId, StateToWire(sess->State()),
				offerB.copper, offerB.items, sessA);
			if (!pkt.empty()) m_server->Send(connA, pkt);
		}
		// B voit l'offer de A avec state=Committed.
		if (connB != 0u && sessB != 0u)
		{
			auto pkt = BuildTradeStateUpdateNotificationPacket(
				parsed->sessionId, StateToWire(sess->State()),
				offerA.copper, offerA.items, sessB);
			if (!pkt.empty()) m_server->Send(connB, pkt);
		}

		LOG_INFO(Net, "[TradeHandler] Commit OK account={} sid={} (V1: no inventory delta applied)",
			accountId, parsed->sessionId);

		// Termine la session. Apres ce point, GetById(sid) retourne nullptr.
		m_reg->End(parsed->sessionId);
	}

	// =========================================================================
	// HandleCancel
	// =========================================================================

	void TradeHandler::HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		(void)connId;
		(void)requestId;
		(void)sessionIdHeader;

		auto parsed = ParseTradeCancelRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[TradeHandler] Cancel parse failed account={} size={}", accountId, payloadSize);
			return;
		}

		auto* sess = m_reg->GetById(parsed->sessionId);
		if (!sess)
		{
			// Pas de session : silencieux. Le client peut avoir ferme l'UI
			// apres un commit (session deja End()-ee) et envoye un Cancel
			// tardif ; ce n'est pas une erreur fatale.
			LOG_INFO(Net, "[TradeHandler] Cancel no-op (sid={} unknown) account={}",
				parsed->sessionId, accountId);
			return;
		}

		if (sess->PlayerA() != accountId && sess->PlayerB() != accountId)
		{
			LOG_WARN(Net, "[TradeHandler] Cancel NotPartOfSession account={} sid={}",
				accountId, parsed->sessionId);
			return;
		}

		const bool ok = sess->Cancel(accountId);
		if (!ok)
		{
			// Session deja Committed/Cancelled ; on End() quand meme par
			// idempotence (si c'etait un Cancel tardif apres Commit, la
			// session a deja ete End() au Commit, le GetById serait null).
			LOG_INFO(Net, "[TradeHandler] Cancel no-op (sid={} terminal) account={}",
				parsed->sessionId, accountId);
			return;
		}

		// Push CancelNotification aux 2 participants. La raison "partner cancelled"
		// permet au client de l'afficher dans cancelReason cote UI.
		const uint64_t accA = sess->PlayerA();
		const uint64_t accB = sess->PlayerB();
		const uint64_t sessA = FindSessionIdForAccount(accA);
		const uint64_t sessB = FindSessionIdForAccount(accB);
		const uint32_t connA = FindConnIdForAccount(accA);
		const uint32_t connB = FindConnIdForAccount(accB);

		if (connA != 0u && sessA != 0u)
		{
			const std::string_view reasonForA = (accountId == accA)
				? std::string_view("you cancelled the trade")
				: std::string_view("partner cancelled the trade");
			auto pkt = BuildTradeCancelNotificationPacket(parsed->sessionId, reasonForA, sessA);
			if (!pkt.empty()) m_server->Send(connA, pkt);
		}
		if (connB != 0u && sessB != 0u)
		{
			const std::string_view reasonForB = (accountId == accB)
				? std::string_view("you cancelled the trade")
				: std::string_view("partner cancelled the trade");
			auto pkt = BuildTradeCancelNotificationPacket(parsed->sessionId, reasonForB, sessB);
			if (!pkt.empty()) m_server->Send(connB, pkt);
		}

		LOG_INFO(Net, "[TradeHandler] Cancel OK account={} sid={}", accountId, parsed->sessionId);

		// Termine la session.
		m_reg->End(parsed->sessionId);
	}
}
