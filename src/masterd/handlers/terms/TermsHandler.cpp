#include "src/masterd/handlers/terms/TermsHandler.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/session/SessionManager.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/account/AccountStore.h"
#include "src/masterd/handlers/terms/TermsRepository.h"
#include "src/masterd/email/LocalizedEmail.h"
#include "src/shared/network/TermsPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

#include "src/masterd/email/SmtpMailer.h"

#include <algorithm>
#include <string>

namespace engine::server
{
	void TermsHandler::SetServer(NetServer* server) { m_server = server; }
	void TermsHandler::SetSessionManager(SessionManager* sm) { m_sessions = sm; }
	void TermsHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void TermsHandler::SetAccountStore(AccountStore* store) { m_accounts = store; }
	void TermsHandler::SetTermsRepository(TermsRepository* repo) { m_repo = repo; }
	void TermsHandler::SetSmtpConfig(const SmtpConfig* cfg) { m_smtp = cfg; }

	void TermsHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
	                                const uint8_t* payload, std::size_t payloadSize)
	{
		using namespace engine::network;
		if (!m_server || !m_sessions || !m_connMap)
			return;

		if (opcode == kOpcodeTermsStatusRequest)
		{
			auto parsed = ParseTermsStatusRequestPayload(payload, payloadSize);
			if (!parsed)
				return;
			TermsStatusResponsePayload resp{};
			if (!m_repo || !m_repo->IsEnforced())
			{
				resp.pending_count = 0;
				auto pkt = BuildTermsStatusResponsePacket(resp, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			auto sess = m_connMap->GetSessionId(connId);
			if (!sess)
				return;
			auto accOpt = m_sessions->GetAccountId(*sess);
			if (!accOpt)
				return;
			resp.pending_count = m_repo->CountPendingEditions(*accOpt);
			TermsRepository::PendingHead head;
			if (!m_repo->GetFirstPending(*accOpt, parsed->locale_pref, head))
			{
				auto pkt = BuildTermsStatusResponsePacket(resp, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			if (head.edition_id != 0)
			{
				resp.next_edition_id = head.edition_id;
				resp.version_label = head.version_label;
				resp.title           = head.title;
				resp.resolved_locale = head.resolved_locale;
			}
			auto pkt = BuildTermsStatusResponsePacket(resp, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (opcode == kOpcodeTermsContentRequest)
		{
			auto parsed = ParseTermsContentRequestPayload(payload, payloadSize);
			if (!parsed)
				return;
			if (!m_repo || !m_repo->IsEnforced())
			{
				TermsContentResponsePayload r{};
				auto pkt = BuildTermsContentResponsePacket(r, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			TermsRepository::ContentChunk chunk;
			if (!m_repo->LoadEditionContent(parsed->edition_id, parsed->locale_pref, chunk))
				return;
			const size_t total = chunk.full_text.size();
			const uint32_t off = parsed->byte_offset;
			const uint32_t maxc = parsed->max_chunk;
			TermsContentResponsePayload r{};
			r.edition_id     = parsed->edition_id;
			r.byte_offset    = off;
			r.total_length   = static_cast<uint32_t>(total);
			if (off < total)
			{
				const size_t take = std::min<size_t>(maxc, total - static_cast<size_t>(off));
				r.chunk.assign(chunk.full_text.data() + off, chunk.full_text.data() + off + take);
			}
			auto pkt = BuildTermsContentResponsePacket(r, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}

		if (opcode == kOpcodeTermsAcceptRequest)
		{
			auto parsed = ParseTermsAcceptRequestPayload(payload, payloadSize);
			if (!parsed)
				return;
			const uint8_t fail = 0;
			const uint8_t ok   = 1;
			if (!m_repo || !m_repo->IsEnforced())
			{
				auto pkt = BuildTermsAcceptResponsePacket(ok, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			if (parsed->acknowledged != 1)
			{
				auto pkt = BuildTermsAcceptResponsePacket(fail, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			auto sess = m_connMap->GetSessionId(connId);
			if (!sess)
			{
				LOG_WARN(Auth, "[TermsHandler] accept: pas de session pour connId={}", connId);
				return;
			}
			auto accOpt = m_sessions->GetAccountId(*sess);
			if (!accOpt)
			{
				LOG_WARN(Auth, "[TermsHandler] accept: session sans account_id (connId={})", connId);
				return;
			}
			if (!m_repo->IsEditionPendingForAccount(*accOpt, parsed->edition_id))
			{
				auto pkt = BuildTermsAcceptResponsePacket(fail, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			if (!m_repo->RecordAcceptance(*accOpt, parsed->edition_id))
			{
				auto pkt = BuildTermsAcceptResponsePacket(fail, requestId, sessionIdHeader);
				if (!pkt.empty()) m_server->Send(connId, pkt);
				return;
			}
			LOG_INFO(Auth, "[TermsHandler] account_id={} accepted terms edition_id={}", *accOpt, parsed->edition_id);

			if (m_smtp && !m_smtp->host.empty() && m_accounts)
			{
				auto ar = m_accounts->FindByAccountId(*accOpt);
				if (!ar || ar->email.empty())
				{
					// Diagnostic Task 7 : l'acceptation a reussi mais le compte n'a pas
					// d'e-mail reel relisible (placeholder @lcdlln.no-email.local, compte
					// inscrit sans e-mail, ou lecture FindByAccountId infructueuse).
					// Sans ce log le mail CGU etait saute SILENCIEUSEMENT — on le rend observable.
					LOG_WARN(Auth, "[TermsHandler] mail CGU saute : email absent pour account_id={} (record_trouve={})",
						*accOpt, (ar ? 1 : 0));
				}
				else
				{
					std::string ver;
					if (!m_repo->GetEditionVersionLabel(parsed->edition_id, ver))
						ver = "?";
					std::string subj, body;
					bool isHtml = false;
					BuildTermsAcceptanceEmail(ar->email_locale, ver, subj, body, isHtml);
					LOG_INFO(Auth, "[TermsHandler] envoi email acceptation CGU (account_id={} edition_id={})", *accOpt, parsed->edition_id);
					const bool sent = SmtpMailer::Send(*m_smtp, ar->email, subj, body, isHtml);
					if (!sent)
						LOG_WARN(Auth,
							"[TermsHandler] échec envoi email CGU (account_id={}) — lignes [SMTP] / sous-système Smtp (log.level Info conseillé)",
							*accOpt);
					else
						LOG_INFO(Auth, "[TermsHandler] email CGU envoyé (account_id={})", *accOpt);
				}
			}
			else
			{
				// Diagnostic Task 7 : cablage SMTP/AccountStore absent ou smtp.host vide.
				// D'apres main_linux.cpp le cablage est complet, donc ce log pointe plutot
				// vers une config SMTP manquante (smtp.host vide) qu'un bug de wiring.
				LOG_WARN(Auth, "[TermsHandler] mail CGU saute : smtp/accounts indisponibles (host_vide={} accounts_null={})",
					(m_smtp ? (m_smtp->host.empty() ? 1 : 0) : 1), (m_accounts == nullptr ? 1 : 0));
			}

			auto pkt = BuildTermsAcceptResponsePacket(ok, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		}
	}
}
