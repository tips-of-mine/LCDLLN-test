#include "src/client/mail/MailUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	namespace
	{
		/// Resout le code MailSendErrorCode en chaine localizable (FR pour V1).
		/// Utilise par \ref MailUiPresenter::OnSendResponse.
		std::string_view DescribeSendError(uint8_t code)
		{
			using engine::network::MailSendErrorCode;
			switch (static_cast<MailSendErrorCode>(code))
			{
			case MailSendErrorCode::Ok:                 return "";
			case MailSendErrorCode::RecipientNotFound:  return "Destinataire introuvable.";
			case MailSendErrorCode::SubjectTooLong:     return "Sujet trop long.";
			case MailSendErrorCode::BodyTooLong:        return "Corps trop long.";
			case MailSendErrorCode::InsufficientGold:   return "Or insuffisant.";
			case MailSendErrorCode::AttachmentsTooMany: return "Trop de pieces jointes.";
			case MailSendErrorCode::Unauthorized:       return "Session expiree, reconnexion requise.";
			}
			return "Erreur inconnue.";
		}

		/// Localise les codes MailReadErrorCode en chaine FR pour le bandeau erreur.
		std::string_view DescribeReadError(uint8_t code)
		{
			using engine::network::MailReadErrorCode;
			switch (static_cast<MailReadErrorCode>(code))
			{
			case MailReadErrorCode::Ok:            return "";
			case MailReadErrorCode::NotFound:      return "Mail introuvable.";
			case MailReadErrorCode::WrongReceiver: return "Vous n'etes pas le destinataire.";
			case MailReadErrorCode::Unauthorized:  return "Session expiree, reconnexion requise.";
			}
			return "Erreur inconnue.";
		}

		/// Localise les codes MailTakeErrorCode en chaine FR.
		std::string_view DescribeTakeError(uint8_t code)
		{
			using engine::network::MailTakeErrorCode;
			switch (static_cast<MailTakeErrorCode>(code))
			{
			case MailTakeErrorCode::Ok:            return "";
			case MailTakeErrorCode::NotFound:      return "Mail introuvable.";
			case MailTakeErrorCode::WrongReceiver: return "Vous n'etes pas le destinataire.";
			case MailTakeErrorCode::AlreadyTaken:  return "Or deja retire.";
			case MailTakeErrorCode::CodNotPaid:    return "Frais COD non regles : payez d'abord les frais.";
			case MailTakeErrorCode::Unauthorized:  return "Session expiree, reconnexion requise.";
			}
			return "Erreur inconnue.";
		}

		/// Localise les codes MailDeleteErrorCode en chaine FR.
		std::string_view DescribeDeleteError(uint8_t code)
		{
			using engine::network::MailDeleteErrorCode;
			switch (static_cast<MailDeleteErrorCode>(code))
			{
			case MailDeleteErrorCode::Ok:             return "";
			case MailDeleteErrorCode::NotFound:       return "Mail introuvable.";
			case MailDeleteErrorCode::WrongReceiver:  return "Vous n'etes pas le destinataire.";
			case MailDeleteErrorCode::HasAttachments: return "Retirez d'abord les pieces jointes.";
			case MailDeleteErrorCode::Unauthorized:   return "Session expiree, reconnexion requise.";
			}
			return "Erreur inconnue.";
		}

		/// Tente de parser un \c string_view comme un uint64 decimal entier sans
		/// signe. Retourne \c std::nullopt si la chaine est vide, contient un
		/// caractere non numerique ou deborde.
		std::optional<uint64_t> ParseUint64Decimal(std::string_view text)
		{
			if (text.empty())
				return std::nullopt;
			uint64_t value = 0;
			const char* first = text.data();
			const char* last  = text.data() + text.size();
			auto [ptr, ec] = std::from_chars(first, last, value, 10);
			if (ec != std::errc{} || ptr != last)
				return std::nullopt;
			return value;
		}
	}

	bool MailUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[MailUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_state = {};
		m_initialized = true;
		LOG_INFO(Core, "[MailUiPresenter] Init OK");
		return true;
	}

	void MailUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		m_send  = {};
		LOG_INFO(Core, "[MailUiPresenter] Shutdown");
	}

	void MailUiPresenter::RequestInbox()
	{
		if (!m_initialized)
			return;
		if (!m_send)
		{
			m_state.lastErrorText = "Pas de connexion au serveur.";
			LOG_WARN(Net, "[MailUiPresenter] RequestInbox: no send callback");
			return;
		}
		m_state.isInboxLoading = true;
		m_state.lastErrorText.clear();
		const auto payload = engine::network::BuildMailListInboxRequestPayload();
		if (!m_send(engine::network::kOpcodeMailListInboxRequest, payload))
		{
			m_state.isInboxLoading = false;
			m_state.lastErrorText  = "Envoi de la requete inbox echoue.";
			LOG_WARN(Net, "[MailUiPresenter] RequestInbox: send failed");
			return;
		}
		LOG_DEBUG(Net, "[MailUiPresenter] RequestInbox queued");
	}

	void MailUiPresenter::SelectMail(uint64_t mailId)
	{
		if (!m_initialized)
			return;
		m_state.selectedMailId = mailId;
		// Si le body est deja en cache, rien a faire : le renderer le lira directement.
		auto it = std::find_if(m_state.inbox.begin(), m_state.inbox.end(),
			[mailId](const MailUiInboxEntry& e) { return e.mailId == mailId; });
		if (it == m_state.inbox.end())
		{
			LOG_WARN(Net, "[MailUiPresenter] SelectMail: unknown mail_id={}", mailId);
			return;
		}
		if (it->bodyLoaded)
		{
			LOG_DEBUG(Net, "[MailUiPresenter] SelectMail: body cache hit (mail_id={})", mailId);
			return;
		}
		if (!m_send)
		{
			m_state.lastErrorText = "Pas de connexion au serveur.";
			return;
		}
		const auto payload = engine::network::BuildMailReadRequestPayload(mailId);
		if (!m_send(engine::network::kOpcodeMailReadRequest, payload))
		{
			m_state.lastErrorText = "Envoi de la requete lecture echoue.";
			LOG_WARN(Net, "[MailUiPresenter] SelectMail: send failed (mail_id={})", mailId);
			return;
		}
		LOG_DEBUG(Net, "[MailUiPresenter] MailReadRequest queued (mail_id={})", mailId);
	}

	void MailUiPresenter::SubmitCompose()
	{
		if (!m_initialized)
			return;
		const auto recipientOpt = ParseUint64Decimal(m_state.composeRecipient);
		if (!recipientOpt)
		{
			m_state.lastErrorText = "Destinataire invalide : entrez un account_id (entier).";
			LOG_WARN(Net, "[MailUiPresenter] SubmitCompose: invalid recipient '{}'", m_state.composeRecipient);
			return;
		}
		if (!m_send)
		{
			m_state.lastErrorText = "Pas de connexion au serveur.";
			return;
		}
		if (m_state.composeSubject.size() > engine::network::kMaxMailSubjectBytes)
		{
			m_state.lastErrorText = "Sujet trop long (max 255 octets).";
			return;
		}
		if (m_state.composeBody.size() > engine::network::kMaxMailBodyBytes)
		{
			m_state.lastErrorText = "Corps trop long.";
			return;
		}
		const auto payload = engine::network::BuildMailSendRequestPayload(
			*recipientOpt,
			m_state.composeSubject,
			m_state.composeBody,
			m_state.composeGold,
			m_state.composeCod);
		if (payload.empty() || !m_send(engine::network::kOpcodeMailSendRequest, payload))
		{
			m_state.lastErrorText = "Envoi du mail echoue.";
			LOG_WARN(Net, "[MailUiPresenter] SubmitCompose: send failed (recipient={})", *recipientOpt);
			return;
		}
		LOG_INFO(Net, "[MailUiPresenter] MailSendRequest queued (recipient={}, subj_len={}, body_len={}, gold={}, cod={})",
			*recipientOpt, m_state.composeSubject.size(), m_state.composeBody.size(),
			m_state.composeGold, m_state.composeCod);
		// Reset des champs + ferme la dialog. On reouvrira automatiquement au prochain
		// OpenCompose (qui re-clear de toute facon, mais c'est plus propre).
		m_state.composeRecipient.clear();
		m_state.composeSubject.clear();
		m_state.composeBody.clear();
		m_state.composeGold = 0;
		m_state.composeCod  = 0;
		m_state.isComposeOpen = false;
	}

	void MailUiPresenter::TakeAttachments(uint64_t mailId)
	{
		if (!m_initialized || !m_send)
			return;
		// V1 : on n'envoie pas de paidCopperCod, le wallet client n'existe pas encore.
		// Le serveur retournera CodNotPaid si copperCod > 0.
		const auto payload = engine::network::BuildMailTakeAttachmentsRequestPayload(mailId, /*paidCopperCod=*/0u);
		if (!m_send(engine::network::kOpcodeMailTakeAttachmentsRequest, payload))
		{
			m_state.lastErrorText = "Envoi prendre-piece-jointe echoue.";
			LOG_WARN(Net, "[MailUiPresenter] TakeAttachments: send failed (mail_id={})", mailId);
			return;
		}
		LOG_DEBUG(Net, "[MailUiPresenter] MailTakeAttachmentsRequest queued (mail_id={})", mailId);
	}

	void MailUiPresenter::DeleteMail(uint64_t mailId)
	{
		if (!m_initialized || !m_send)
			return;
		const auto payload = engine::network::BuildMailDeleteRequestPayload(mailId);
		if (!m_send(engine::network::kOpcodeMailDeleteRequest, payload))
		{
			m_state.lastErrorText = "Envoi suppression echoue.";
			LOG_WARN(Net, "[MailUiPresenter] DeleteMail: send failed (mail_id={})", mailId);
			return;
		}
		LOG_DEBUG(Net, "[MailUiPresenter] MailDeleteRequest queued (mail_id={})", mailId);
	}

	void MailUiPresenter::OpenCompose()
	{
		if (!m_initialized)
			return;
		m_state.composeRecipient.clear();
		m_state.composeSubject.clear();
		m_state.composeBody.clear();
		m_state.composeGold = 0;
		m_state.composeCod  = 0;
		m_state.isComposeOpen = true;
	}

	void MailUiPresenter::CloseCompose()
	{
		if (!m_initialized)
			return;
		m_state.isComposeOpen = false;
	}

	void MailUiPresenter::OnInboxResponse(const engine::network::MailListInboxResponsePayload& resp)
	{
		m_state.isInboxLoading = false;
		if (resp.error != 0)
		{
			// Seul code documente : Unauthorized (6).
			m_state.lastErrorText = "Inbox indisponible (session ?).";
			LOG_WARN(Net, "[MailUiPresenter] OnInboxResponse: server error code={}", static_cast<unsigned>(resp.error));
			return;
		}
		// Conversion network::MailInboxEntry -> MailUiInboxEntry. On preserve les bodies
		// deja telecharges pour les mailId presents dans les deux listes (rare mais utile
		// quand le client refresh apres un Send qui a vide l'inbox affichee).
		std::vector<MailUiInboxEntry> next;
		next.reserve(resp.mails.size());
		for (const auto& src : resp.mails)
		{
			MailUiInboxEntry dst{};
			dst.mailId          = src.mailId;
			dst.senderAccountId = src.senderAccountId;
			dst.subject         = src.subject;
			dst.sentTsMs        = src.sentTsMs;
			dst.expiresTsMs     = src.expiresTsMs;
			dst.state           = src.state;
			dst.copperGold      = src.copperGold;
			dst.copperCod       = src.copperCod;
			// Cherche une eventuelle entree precedente pour reutiliser le body.
			auto prev = std::find_if(m_state.inbox.begin(), m_state.inbox.end(),
				[id = src.mailId](const MailUiInboxEntry& e) { return e.mailId == id; });
			if (prev != m_state.inbox.end() && prev->bodyLoaded)
			{
				dst.body       = std::move(prev->body);
				dst.bodyLoaded = true;
			}
			next.push_back(std::move(dst));
		}
		m_state.inbox = std::move(next);
		// Si le mail selectionne n'existe plus, on de-selectionne.
		if (m_state.selectedMailId)
		{
			const uint64_t selId = *m_state.selectedMailId;
			auto it = std::find_if(m_state.inbox.begin(), m_state.inbox.end(),
				[selId](const MailUiInboxEntry& e) { return e.mailId == selId; });
			if (it == m_state.inbox.end())
				m_state.selectedMailId.reset();
		}
		LOG_INFO(Net, "[MailUiPresenter] OnInboxResponse: {} entries", m_state.inbox.size());
	}

	void MailUiPresenter::OnReadResponse(const engine::network::MailReadResponsePayload& resp)
	{
		if (resp.error != 0)
		{
			m_state.lastErrorText.assign(DescribeReadError(resp.error));
			LOG_WARN(Net, "[MailUiPresenter] OnReadResponse: error code={} mail_id={}",
				static_cast<unsigned>(resp.error), resp.mailId);
			return;
		}
		auto it = std::find_if(m_state.inbox.begin(), m_state.inbox.end(),
			[id = resp.mailId](const MailUiInboxEntry& e) { return e.mailId == id; });
		if (it == m_state.inbox.end())
		{
			LOG_WARN(Net, "[MailUiPresenter] OnReadResponse: unknown mail_id={}", resp.mailId);
			return;
		}
		it->body       = resp.body;
		it->bodyLoaded = true;
		// Le serveur a marque le mail comme lu en DB ; on reflete cote client (state=1).
		if (it->state == 0)
			it->state = 1;
		LOG_DEBUG(Net, "[MailUiPresenter] OnReadResponse: body cached (mail_id={}, body_len={})",
			resp.mailId, resp.body.size());
	}

	void MailUiPresenter::OnSendResponse(const engine::network::MailSendResponsePayload& resp)
	{
		if (resp.error != 0)
		{
			m_state.lastErrorText.assign(DescribeSendError(resp.error));
			LOG_WARN(Net, "[MailUiPresenter] OnSendResponse: error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		LOG_INFO(Net, "[MailUiPresenter] OnSendResponse: OK mail_id={}", resp.mailId);
		// Refresh inbox automatique : si le mail s'est envoye a nous-memes
		// (cas tests / dev), on le verra apparaitre. Sinon c'est un no-op visible.
		RequestInbox();
	}

	void MailUiPresenter::OnTakeAttachmentsResponse(const engine::network::MailTakeAttachmentsResponsePayload& resp)
	{
		if (resp.error != 0)
		{
			m_state.lastErrorText.assign(DescribeTakeError(resp.error));
			LOG_WARN(Net, "[MailUiPresenter] OnTakeAttachmentsResponse: error code={} mail_id={}",
				static_cast<unsigned>(resp.error), resp.mailId);
			return;
		}
		auto it = std::find_if(m_state.inbox.begin(), m_state.inbox.end(),
			[id = resp.mailId](const MailUiInboxEntry& e) { return e.mailId == id; });
		if (it != m_state.inbox.end())
		{
			it->copperGold = 0; // Le serveur a viree l'or attache.
		}
		LOG_INFO(Net, "[MailUiPresenter] OnTakeAttachmentsResponse: OK mail_id={} gold_taken={}",
			resp.mailId, resp.copperGoldTaken);
	}

	void MailUiPresenter::OnDeleteResponse(const engine::network::MailDeleteResponsePayload& resp)
	{
		if (resp.error != 0)
		{
			m_state.lastErrorText.assign(DescribeDeleteError(resp.error));
			LOG_WARN(Net, "[MailUiPresenter] OnDeleteResponse: error code={} mail_id={}",
				static_cast<unsigned>(resp.error), resp.mailId);
			return;
		}
		const auto before = m_state.inbox.size();
		m_state.inbox.erase(std::remove_if(m_state.inbox.begin(), m_state.inbox.end(),
			[id = resp.mailId](const MailUiInboxEntry& e) { return e.mailId == id; }),
			m_state.inbox.end());
		if (m_state.selectedMailId && *m_state.selectedMailId == resp.mailId)
			m_state.selectedMailId.reset();
		LOG_INFO(Net, "[MailUiPresenter] OnDeleteResponse: OK mail_id={} (inbox {} -> {})",
			resp.mailId, before, m_state.inbox.size());
	}
}
