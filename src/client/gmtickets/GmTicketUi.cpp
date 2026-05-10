// CMANGOS.32 (Phase 5.32 step 3+4) — Implementation GmTicketUiPresenter.

#include "src/client/gmtickets/GmTicketUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	GmTicketUiPresenter::~GmTicketUiPresenter()
	{
		Shutdown();
	}

	bool GmTicketUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[GmTicketUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_state.layoutValid = true;
		LOG_INFO(Core, "[GmTicketUiPresenter] Init OK");
		return true;
	}

	void GmTicketUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		// Ne pas reset m_send : il est cable une fois au boot et on garde la
		// reference (Engine::Shutdown sera responsable du teardown ordonne).
		LOG_INFO(Core, "[GmTicketUiPresenter] Destroyed");
	}

	void GmTicketUiPresenter::RequestMyTickets()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GmTicketUiPresenter] RequestMyTickets: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGmTicketListMineRequestPayload();
		m_state.isListLoading = true;
		if (!m_send(engine::network::kOpcodeGmTicketListMineRequest, payload))
		{
			m_state.isListLoading = false;
			m_state.lastErrorText = "Echec envoi (liste tickets).";
			LOG_WARN(Net, "[GmTicketUiPresenter] RequestMyTickets: send failed");
			return;
		}
		LOG_DEBUG(Net, "[GmTicketUiPresenter] GmTicketListMineRequest queued");
	}

	void GmTicketUiPresenter::OpenTicket(std::string_view body)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GmTicketUiPresenter] OpenTicket: no send callback");
			return;
		}
		// Le renderer doit en principe avoir bloque le bouton si le body est
		// vide, mais on protege ici aussi : evite un round-trip pour un
		// BodyEmpty cote serveur.
		bool hasContent = false;
		for (char c : body)
		{
			const unsigned char uc = static_cast<unsigned char>(c);
			if (uc > 0x20u) { hasContent = true; break; }
		}
		if (!hasContent)
		{
			m_state.lastErrorText = "Le texte du ticket est vide.";
			LOG_WARN(Net, "[GmTicketUiPresenter] OpenTicket: empty body");
			return;
		}
		if (body.size() > engine::network::kMaxGmTicketBodyBytes)
		{
			m_state.lastErrorText = "Le texte du ticket est trop long (>4096 octets).";
			body = body.substr(0, engine::network::kMaxGmTicketBodyBytes);
		}
		const auto payload = engine::network::BuildGmTicketOpenRequestPayload(body);
		if (!m_send(engine::network::kOpcodeGmTicketOpenRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (ouverture ticket).";
			LOG_WARN(Net, "[GmTicketUiPresenter] OpenTicket: send failed");
			return;
		}
		// On ferme le compose au moment du submit : le serveur repondra plus
		// tard avec un OpenResponse qui declenchera le refresh de la liste.
		m_state.isComposeOpen = false;
		m_state.composeBody.clear();
		LOG_INFO(Net, "[GmTicketUiPresenter] GmTicketOpenRequest queued (size={})", body.size());
	}

	void GmTicketUiPresenter::CancelTicket(uint64_t ticketId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GmTicketUiPresenter] CancelTicket: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGmTicketCancelRequestPayload(ticketId);
		if (!m_send(engine::network::kOpcodeGmTicketCancelRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (annulation).";
			LOG_WARN(Net, "[GmTicketUiPresenter] CancelTicket: send failed (ticket={})", ticketId);
			return;
		}
		LOG_INFO(Net, "[GmTicketUiPresenter] GmTicketCancelRequest queued (ticket={})", ticketId);
	}

	void GmTicketUiPresenter::OpenCompose()
	{
		if (!m_state.isComposeOpen)
		{
			m_state.composeBody.clear();
		}
		m_state.isComposeOpen = true;
		m_state.lastErrorText.clear();
		LOG_DEBUG(Core, "[GmTicketUiPresenter] OpenCompose");
	}

	void GmTicketUiPresenter::CloseCompose()
	{
		m_state.isComposeOpen = false;
		// On garde le composeBody : si le joueur reouvre il retrouve sa saisie.
		LOG_DEBUG(Core, "[GmTicketUiPresenter] CloseCompose");
	}

	void GmTicketUiPresenter::SetComposeBody(std::string_view body)
	{
		m_state.composeBody.assign(body.data(), body.size());
	}

	// -------------------------------------------------------------------------

	void GmTicketUiPresenter::RebuildMineFromResponse(const engine::network::GmTicketListMineResponsePayload& resp)
	{
		m_state.mine.clear();
		m_state.mine.reserve(resp.tickets.size());
		for (const auto& t : resp.tickets)
		{
			GmTicketEntryView v;
			v.id           = t.id;
			v.createdTsMs  = t.createdTsMs;
			v.resolvedTsMs = t.resolvedTsMs;
			v.state        = t.state;
			m_state.mine.push_back(v);
		}
	}

	void GmTicketUiPresenter::OnOpenResponse(const engine::network::GmTicketOpenResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			using engine::network::GmTicketErrorCode;
			switch (static_cast<GmTicketErrorCode>(resp.error))
			{
			case GmTicketErrorCode::BodyEmpty:
				m_state.lastErrorText = "Le texte du ticket est vide.";
				break;
			case GmTicketErrorCode::BodyTooLong:
				m_state.lastErrorText = "Le texte du ticket est trop long (>4096 octets).";
				break;
			case GmTicketErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur a l'ouverture du ticket.";
				break;
			}
			LOG_WARN(Net, "[GmTicketUiPresenter] OnOpenResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		// OK : on rafraichit la liste pour faire apparaitre le nouveau ticket.
		m_state.lastErrorText.clear();
		LOG_INFO(Net, "[GmTicketUiPresenter] OnOpenResponse: ticketId={}", resp.ticketId);
		RequestMyTickets();
	}

	void GmTicketUiPresenter::OnListMineResponse(const engine::network::GmTicketListMineResponsePayload& resp)
	{
		m_state.isListLoading = false;
		if (resp.error != 0u)
		{
			using engine::network::GmTicketErrorCode;
			if (static_cast<GmTicketErrorCode>(resp.error) == GmTicketErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur lors du chargement de la liste.";
			LOG_WARN(Net, "[GmTicketUiPresenter] OnListMineResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		RebuildMineFromResponse(resp);
		LOG_INFO(Net, "[GmTicketUiPresenter] OnListMineResponse: {} tickets",
			m_state.mine.size());
	}

	void GmTicketUiPresenter::OnCancelResponse(const engine::network::GmTicketCancelResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			using engine::network::GmTicketErrorCode;
			switch (static_cast<GmTicketErrorCode>(resp.error))
			{
			case GmTicketErrorCode::NotFound:
				m_state.lastErrorText = "Ticket introuvable.";
				break;
			case GmTicketErrorCode::NotOwner:
				m_state.lastErrorText = "Ce ticket ne vous appartient pas.";
				break;
			case GmTicketErrorCode::AlreadyResolved:
				m_state.lastErrorText = "Ticket deja resolu, annulation impossible.";
				break;
			case GmTicketErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur a l'annulation.";
				break;
			}
			LOG_WARN(Net, "[GmTicketUiPresenter] OnCancelResponse: server error code={} ticket={}",
				static_cast<unsigned>(resp.error), resp.ticketId);
			return;
		}
		m_state.lastErrorText.clear();
		LOG_INFO(Net, "[GmTicketUiPresenter] OnCancelResponse: ticketId={} cancelled", resp.ticketId);
		// Refresh la liste pour refleter le state Cancelled (ou disparition de
		// la liste, selon la politique cote serveur).
		RequestMyTickets();
	}

	void GmTicketUiPresenter::OnResolvedNotification(const engine::network::GmTicketResolvedNotificationPayload& note)
	{
		m_state.lastResolvedNotificationTicketId = note.ticketId;
		LOG_INFO(Net, "[GmTicketUiPresenter] OnResolvedNotification: ticketId={} resolvedTsMs={}",
			note.ticketId, note.resolvedTsMs);
		// Refresh la liste pour mettre a jour l'etat affiche.
		RequestMyTickets();
	}
}
