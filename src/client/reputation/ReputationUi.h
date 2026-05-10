#pragma once
// CMANGOS.24 (Phase 3.24 step 3+4) — Presenter client de la liste de
// reputations. Maintient un cache local des reputations par faction +
// affiche un toast non-bloquant sur la derniere mise a jour push.
//
// Pas de rendu ImGui : le panneau est drawe par ReputationImGuiRenderer qui
// lit l'etat via GetState() et propage les inputs UI via les methodes du
// presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans QuestUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 96/97 vers
// les OnXxx du presenter.

#include "src/shared/network/ReputationPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace engine::client
{
	/// Une entree de la liste de reputations exposable au layer UI.
	/// Mirror direct de ReputationEntry sur le wire mais avec des champs
	/// nommes pour le renderer.
	struct ReputationEntryView
	{
		uint32_t factionId = 0;
		int32_t  value     = 0; ///< [-42000 ; +41999] cmangos.
		int8_t   standing  = 0; ///< -6 Hated ... +1 Exalted (cf. ReputationStanding).
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct ReputationUiState
	{
		std::vector<ReputationEntryView> entries;
		bool                             isLoading = false;
		std::string                      lastErrorText;       ///< Vide si pas d'erreur transitoire.
		/// Toast non-bloquant : derniere mise a jour push (factionId, newValue, standing, delta).
		/// Le renderer affiche un toast en haut du panneau pendant ~3s.
		std::optional<ReputationEntryView> lastUpdateToast;
		int32_t                          lastUpdateToastDelta = 0; ///< delta de la derniere notification (signed).
		float                            lastUpdateToastExpireAt = 0.0f; ///< game seconds (cf. TickToast).
		bool                             layoutValid = false;
	};

	/// Presenter pour le panneau Reputation cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main.
	class ReputationUiPresenter final
	{
	public:
		ReputationUiPresenter() = default;

		ReputationUiPresenter(const ReputationUiPresenter&)            = delete;
		ReputationUiPresenter& operator=(const ReputationUiPresenter&) = delete;

		~ReputationUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.24 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestReputationList.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie REPUTATION_LIST_REQUEST. Reponse via OnListResponse.
		/// Met aussi m_state.isLoading = true le temps de la reponse.
		void RequestReputationList();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit REPUTATION_LIST_RESPONSE. Remplace la cache locale.
		void OnListResponse(const engine::network::ReputationListResponsePayload& resp);

		/// Recoit un push REPUTATION_UPDATE_NOTIFICATION : update entry locale +
		/// arme le toast pour ~3s.
		void OnUpdateNotification(const engine::network::ReputationUpdateNotificationPayload& note);

		// ---------------------------------------------------------------------
		// Tick / state access
		// ---------------------------------------------------------------------

		/// Avance le compteur de toast et clear l'overlay si expire. \p deltaSeconds
		/// est le dt frame (en secondes). A appeler chaque frame depuis Engine.
		void TickToast(float deltaSeconds);

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const ReputationUiState& GetState() const { return m_state; }

	private:
		/// Met a jour m_state.entries depuis une reponse server.
		void RebuildEntriesFromResponse(const engine::network::ReputationListResponsePayload& resp);

		/// Met a jour ou insere une entree apres push notification.
		void UpdateOrInsertEntry(uint32_t factionId, int32_t newValue, int8_t newStanding);

		bool                  m_initialized       = false;
		ReputationUiState     m_state{};
		SendCallback          m_send;
		float                 m_clockSeconds      = 0.0f; ///< Cumul depuis Init() (non monotonic, juste utilise pour comparer expiry).
	};
}
