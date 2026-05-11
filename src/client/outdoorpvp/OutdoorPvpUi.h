#pragma once
// CMANGOS.36 (Phase 5.36 step 3+4) — Presenter client de la fenetre OutdoorPvp.
// Maintient un cache local des zones contestees + objectifs + scores +
// subscriptions actives + etat de capture en cours + indicateur transitoire
// du dernier resultat de capture.
//
// Pas de rendu ImGui : le panneau est drawe par OutdoorPvpImGuiRenderer
// qui lit l'etat via GetState() et propage les inputs UI (RequestList /
// Subscribe / Unsubscribe / StartCapture) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans BattleGroundUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 141/143/145/147/148/149 vers les OnXxx du presenter.

#include "src/shared/network/OutdoorPvpPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'un objectif capturable exposable au layer UI. Mirror direct
	/// de engine::network::OutdoorPvpObjectiveSummary.
	struct OutdoorPvpObjectiveSummary
	{
		uint32_t objectiveId  = 0;
		uint8_t  owner        = 0xFFu;
		uint32_t capturePct   = 0;
		uint8_t  capturingBy  = 0xFFu;
	};

	/// Resume d'une zone contestee exposable au layer UI.
	struct OutdoorPvpZoneSummary
	{
		uint32_t                                  zoneId         = 0;
		std::string                               name;
		uint32_t                                  allianceScore  = 0;
		uint32_t                                  hordeScore     = 0;
		std::vector<OutdoorPvpObjectiveSummary>   objectives;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct OutdoorPvpUiState
	{
		std::vector<OutdoorPvpZoneSummary>  zones;
		bool                                zonesLoaded = false;

		/// Subscriptions actives cote client (pour synchroniser les boutons
		/// Subscribe/Unsubscribe). Mis a jour aux OnSubscribeResponse Ok / OnUnsubscribeResponse Ok.
		std::unordered_set<uint32_t>        subscribedZones;

		/// Capture en cours : si set, le panneau affiche une bar de progression
		/// (capturingZoneId + capturingObjectiveId + capturingPct). Reinitialise
		/// au push Completed correspondant.
		std::optional<uint32_t> capturingZoneId;
		std::optional<uint32_t> capturingObjectiveId;
		uint32_t                capturingPct        = 0;
		uint8_t                 capturingByFaction  = 0xFFu;

		/// Resultat du dernier capture complet (push 149). Affiche par le
		/// renderer pendant une duree (5 sec env., gere cote renderer V1 :
		/// le presenter ne timeout pas activement).
		std::optional<uint64_t> lastCaptureCompletedTimeMs;
		uint32_t                lastCaptureZoneId       = 0;
		uint32_t                lastCaptureObjectiveId  = 0;
		uint8_t                 lastCaptureNewOwner     = 0xFFu;
		uint32_t                lastCaptureAllianceScore = 0;
		uint32_t                lastCaptureHordeScore   = 0;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Abonne a la zone 1."). Vide par defaut.
		std::string lastInfoText;
	};

	/// Presenter pour la fenetre OutdoorPvp cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main (comme les autres presenters UI).
	class OutdoorPvpUiPresenter final
	{
	public:
		OutdoorPvpUiPresenter() = default;

		OutdoorPvpUiPresenter(const OutdoorPvpUiPresenter&)            = delete;
		OutdoorPvpUiPresenter& operator=(const OutdoorPvpUiPresenter&) = delete;

		~OutdoorPvpUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.36 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / Subscribe / Unsubscribe / StartCapture.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie OUTDOOR_PVP_ZONE_LIST_REQUEST. Reponse via OnListResponse.
		void RequestList();

		/// Envoie OUTDOOR_PVP_SUBSCRIBE_REQUEST avec \p zoneId. Reponse via
		/// OnSubscribeResponse. Le state est mis a jour cote client a
		/// reception de la reponse Ok.
		void Subscribe(uint32_t zoneId);

		/// Envoie OUTDOOR_PVP_UNSUBSCRIBE_REQUEST avec \p zoneId. Reponse via
		/// OnUnsubscribeResponse.
		void Unsubscribe(uint32_t zoneId);

		/// Envoie OUTDOOR_PVP_CAPTURE_START_REQUEST avec \p zoneId, \p objectiveId
		/// et \p faction (0=Alliance, 1=Horde). Reponse via OnCaptureStartResponse.
		void StartCapture(uint32_t zoneId, uint32_t objectiveId, uint8_t faction);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit OUTDOOR_PVP_ZONE_LIST_RESPONSE. Remplace le cache local.
		void OnListResponse(const engine::network::OutdoorPvpZoneListResponsePayload& resp);

		/// Recoit OUTDOOR_PVP_SUBSCRIBE_RESPONSE. Si Ok, ajoute zoneId a la
		/// liste des subscriptions locale.
		void OnSubscribeResponse(const engine::network::OutdoorPvpSubscribeResponsePayload& resp);

		/// Recoit OUTDOOR_PVP_UNSUBSCRIBE_RESPONSE. Si Ok, retire zoneId.
		void OnUnsubscribeResponse(const engine::network::OutdoorPvpUnsubscribeResponsePayload& resp);

		/// Recoit OUTDOOR_PVP_CAPTURE_START_RESPONSE. Si Ok, le presenter
		/// attend les push 148/149 pour mettre a jour la progression.
		void OnCaptureStartResponse(const engine::network::OutdoorPvpCaptureStartResponsePayload& resp);

		/// Recoit un push OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION : update
		/// capturingPct + capturingZoneId + capturingObjectiveId + capturingByFaction.
		void OnCaptureProgressNotification(const engine::network::OutdoorPvpCaptureProgressNotificationPayload& note);

		/// Recoit un push OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION : arme
		/// le toast result, met a jour les scores de la zone dans le cache
		/// local, clear la capture en cours.
		void OnCaptureCompletedNotification(const engine::network::OutdoorPvpCaptureCompletedNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const OutdoorPvpUiState& GetState() const { return m_state; }

	private:
		/// Memorise les parametres de la request en cours (pour update
		/// du state apres reponse Ok). On stocke le zoneId du Subscribe/
		/// Unsubscribe pendant et le triplet (zoneId, objectiveId, faction)
		/// du CaptureStart pendant.
		uint32_t m_pendingSubscribeZoneId   = 0;
		uint32_t m_pendingUnsubscribeZoneId = 0;
		uint32_t m_pendingCaptureZoneId     = 0;
		uint32_t m_pendingCaptureObjectiveId = 0;
		uint8_t  m_pendingCaptureFaction    = 0;

		bool                m_initialized = false;
		OutdoorPvpUiState   m_state{};
		SendCallback        m_send;
	};
}
