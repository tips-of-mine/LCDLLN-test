#pragma once
// CMANGOS.31 (Phase 5.31 step 3+4) — Presenter client de la fenetre
// GameEvents. Maintient un cache local des events saisonniers + flag
// d'abonnement global + dernier changement reçu (pour toast UI).
//
// Pas de rendu ImGui : le panneau est drawe par GameEventImGuiRenderer qui
// lit l'etat via GetState() et propage les inputs UI (RequestList /
// Subscribe / Unsubscribe) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans WeatherUi).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 158/160/162/163 vers les OnXxx du presenter.

#include "src/shared/network/GameEventPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'un event expose au layer UI. Mirror direct de
	/// engine::network::GameEventSummary + un champ `untilTsMs` qui
	/// reflete le dernier StateChange reçu (ou 0 si jamais reçu).
	struct GameEventSummary
	{
		uint32_t    eventId    = 0;
		std::string name;
		uint8_t     state      = 0;     ///< 0=Inactive, 1=Active.
		uint64_t    startTsMs  = 0;
		uint64_t    durationMs = 0;
		uint64_t    recurMs    = 0;
		uint64_t    untilTsMs  = 0;     ///< Active: when ends ; Inactive: when starts. 0 = inconnu.
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct GameEventState
	{
		std::vector<GameEventSummary>     events;
		bool                              eventsLoaded = false;

		/// Abonnement global : true si l'utilisateur est abonne aux push.
		bool                              subscribed   = false;

		/// Dernier StateChange reçu : eventId + nouvel etat + instant
		/// d'arrivee (ms steady_clock cote client). Utilise par le renderer
		/// pour afficher un toast 5s apres reception. 0 = jamais reçu.
		std::optional<uint64_t>           lastChangeTimeMs;
		uint32_t                          lastChangeEventId  = 0;
		uint8_t                           lastChangeNewState = 0;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Abonne aux events."). Vide par defaut.
		std::string lastInfoText;
	};

	/// Static helper : retourne le libelle ASCII en anglais pour un state.
	/// Inputs invalides (state > 1) renvoient "?".
	const char* GameEventStateName(uint8_t state);

	/// Static helper : formate un delta en ms en chaine "in Xd Yh" ou
	/// "Xh Ym" ou "Xm Ys". \p deltaMs peut etre negatif (renvoie "-").
	/// \param deltaMs delta a formater.
	std::string FormatRelativeTime(int64_t deltaMs);

	/// Presenter pour la fenetre GameEvents cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main (comme les autres
	/// presenters UI).
	class GameEventUiPresenter final
	{
	public:
		GameEventUiPresenter() = default;

		GameEventUiPresenter(const GameEventUiPresenter&)            = delete;
		GameEventUiPresenter& operator=(const GameEventUiPresenter&) = delete;

		~GameEventUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.31 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / Subscribe / Unsubscribe.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie GAME_EVENT_LIST_REQUEST. Reponse via OnListResponse.
		void RequestList();

		/// Envoie GAME_EVENT_SUBSCRIBE_REQUEST. Reponse via OnSubscribeResponse.
		void Subscribe();

		/// Envoie GAME_EVENT_UNSUBSCRIBE_REQUEST. Reponse via OnUnsubscribeResponse.
		void Unsubscribe();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit GAME_EVENT_LIST_RESPONSE. Remplace le cache local.
		void OnListResponse(const engine::network::GameEventListResponsePayload& resp);

		/// Recoit GAME_EVENT_SUBSCRIBE_RESPONSE. Si Ok, met subscribed=true.
		void OnSubscribeResponse(const engine::network::GameEventSubscribeResponsePayload& resp);

		/// Recoit GAME_EVENT_UNSUBSCRIBE_RESPONSE. Si Ok, met subscribed=false.
		void OnUnsubscribeResponse(const engine::network::GameEventUnsubscribeResponsePayload& resp);

		/// Recoit un push GAME_EVENT_STATE_CHANGE_NOTIFICATION : update
		/// state + untilTsMs de l'event dans le cache local. Met aussi a jour
		/// lastChangeEventId/lastChangeNewState/lastChangeTimeMs pour le toast UI.
		void OnStateChangeNotification(const engine::network::GameEventStateChangeNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const GameEventState& GetState() const { return m_state; }

	private:
		bool             m_initialized = false;
		GameEventState   m_state{};
		SendCallback     m_send;
	};
}
