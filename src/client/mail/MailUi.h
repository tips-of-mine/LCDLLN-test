#pragma once
// CMANGOS.18 (Phase 3.18 step 4) — MailUi : presenter cote client de
// la boite mail. Recoit les MailListInboxResponse / MailReadResponse
// du master, expose un model immuable au renderer, et fire-and-forget
// les requetes (Send / List / Read / TakeAttachments / Delete).

#include "src/shared/network/MailPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Une entree affichee dans la liste inbox. Miroir cote client de
	/// \ref engine::network::MailInboxEntry, augmente d'un cache local pour
	/// le body (rempli a la volee apres MailReadResponse) afin d'eviter de
	/// re-interroger le serveur quand l'utilisateur reclique sur le mail.
	struct MailUiInboxEntry
	{
		uint64_t    mailId         = 0;
		uint64_t    senderAccountId = 0;
		std::string subject;
		uint64_t    sentTsMs       = 0;
		uint64_t    expiresTsMs    = 0;
		uint8_t     state          = 0; ///< 0=Unread, 1=Read, 2=Returned, 3=Deleted
		uint64_t    copperGold     = 0;
		uint64_t    copperCod      = 0;
		bool        bodyLoaded     = false;
		std::string body;             ///< rempli apres MailReadResponse
	};

	/// Etat global du panel Mail. Toutes les mutations passent par
	/// \ref MailUiPresenter ; le renderer ne fait que lire \ref GetState.
	struct MailUiState
	{
		std::vector<MailUiInboxEntry> inbox;
		std::optional<uint64_t>       selectedMailId; ///< vide si rien selectionne
		bool                          isInboxLoading = false;
		bool                          isComposeOpen  = false;
		std::string                   composeRecipient;  ///< saisie utilisateur (account_id sous forme decimale)
		std::string                   composeSubject;
		std::string                   composeBody;
		uint64_t                      composeGold     = 0;
		uint64_t                      composeCod      = 0;
		std::string                   lastErrorText;     ///< affichage non bloquant (pourrait devenir une queue plus tard)
	};

	/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
	/// Cable via \ref MailUiPresenter::SetSendCallback. Doit retourner
	/// \c true si le paquet a ete queue, \c false sinon (pas de session, build
	/// echoue). Dans ce dernier cas le presenter remplit \c lastErrorText et
	/// reset \c isInboxLoading le cas echeant.
	using MailSendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

	/// Presenter de la boite mail cote client (Phase 3 CMANGOS.18 step 4).
	/// Branche les opcodes Mail (49-58) sur une UI ImGui ; partage la connexion
	/// master du \ref AuthUiPresenter via un callback fire-and-forget.
	class MailUiPresenter final
	{
	public:
		MailUiPresenter() = default;
		MailUiPresenter(const MailUiPresenter&)            = delete;
		MailUiPresenter& operator=(const MailUiPresenter&) = delete;

		/// Initialise l'etat (vide). LOG_INFO sur succes. Idempotent : une seconde
		/// init renvoie \c true sans toucher l'etat.
		bool Init();
		/// Libere l'etat. LOG_INFO. Apres Shutdown il faut re-Init avant d'envoyer.
		void Shutdown();

		/// Cable le callback pour fire-and-forget des requetes au master. Doit
		/// etre appele avant tout RequestInbox / SubmitCompose.
		void SetSendCallback(MailSendCallback cb) { m_send = std::move(cb); }

		/// Demande l'inbox au master (envoie MailListInboxRequest). Met
		/// \c isInboxLoading=true. La reponse est consommee via \ref OnInboxResponse.
		void RequestInbox();

		/// Selectionne un mail dans la liste. Si pas encore charge, envoie
		/// MailReadRequest. Sinon affiche directement le body cache.
		/// \param mailId identifiant du mail (cf. \ref MailUiInboxEntry::mailId).
		void SelectMail(uint64_t mailId);

		/// Envoie un mail (compose dialog confirmed). Tente de parser
		/// \c composeRecipient comme un uint64 (account_id). En cas d'echec,
		/// remplit \c lastErrorText et n'envoie pas. Sur succes reseau, reset
		/// les champs compose et ferme la dialog ; \ref OnSendResponse fera le
		/// refresh inbox automatique si le serveur accepte.
		void SubmitCompose();

		/// Take attachments (gold + COD). \param mailId identifiant cible.
		/// Pour V1 on envoie \c paidCopperCod=0 (le wallet client n'est pas
		/// encore branche) : le serveur retournera \c CodNotPaid si COD > 0,
		/// affiche dans \c lastErrorText.
		void TakeAttachments(uint64_t mailId);

		/// Supprime un mail (admin / janitor user-side). Si le mail a encore
		/// des attachments, le serveur retournera \c HasAttachments.
		void DeleteMail(uint64_t mailId);

		/// Open / close le compose dialog. Ouvrir reset les champs compose
		/// (recipient, subject, body, gold, cod). Fermer ne touche pas aux
		/// champs (l'utilisateur peut reouvrir et continuer).
		void OpenCompose();
		void CloseCompose();

		/// Setters compose (relayes par le renderer ImGui).
		void SetComposeRecipient(std::string_view text) { m_state.composeRecipient.assign(text); }
		void SetComposeSubject(std::string_view text)   { m_state.composeSubject.assign(text); }
		void SetComposeBody(std::string_view text)      { m_state.composeBody.assign(text); }
		void SetComposeGold(uint64_t copperGold)        { m_state.composeGold = copperGold; }
		void SetComposeCod(uint64_t copperCod)          { m_state.composeCod = copperCod; }

		/// Recoit une reponse master : appel par le push handler du Engine.
		/// Chaque OnXxxResponse mute l'etat (et eventuellement \c lastErrorText)
		/// puis logue le succes ou la cause de l'echec.
		void OnInboxResponse(const engine::network::MailListInboxResponsePayload& resp);
		void OnReadResponse(const engine::network::MailReadResponsePayload& resp);
		void OnSendResponse(const engine::network::MailSendResponsePayload& resp);
		void OnTakeAttachmentsResponse(const engine::network::MailTakeAttachmentsResponsePayload& resp);
		void OnDeleteResponse(const engine::network::MailDeleteResponsePayload& resp);

		/// Etat immuable pour le renderer.
		const MailUiState& GetState() const { return m_state; }

		/// Utile pour le renderer ImGui (init paresseuse une fois la session active).
		bool IsInitialized() const { return m_initialized; }

	private:
		MailUiState      m_state;
		MailSendCallback m_send;
		bool             m_initialized = false;
	};
}
