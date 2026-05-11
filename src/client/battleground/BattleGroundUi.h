#pragma once
// CMANGOS.10 (Phase 5 step 3+4) — Presenter client de la fenetre BattleGround.
// Maintient un cache local des battlegrounds disponibles + etat de queue +
// scoreboard du match actif + indicateur transitoire du dernier resultat.
//
// Pas de rendu ImGui : le panneau est drawe par BattleGroundImGuiRenderer
// qui lit l'etat via GetState() et propage les inputs UI (RequestList /
// Queue / LeaveQueue / LeaveMatch) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans LfgUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 131/133/135/136/137/138 vers les OnXxx du presenter.

#include "src/shared/network/BattleGroundPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'un battleground exposable au layer UI. Mirror direct de
	/// engine::network::BgInfo.
	struct BattleGroundInfo
	{
		uint16_t    bgType   = 0;
		std::string name;
		uint8_t     teamSize = 0;
		std::string mapName;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct BattleGroundUiState
	{
		std::vector<BattleGroundInfo> battlegrounds;
		bool                          listLoaded = false;

		/// True si l'account est inscrit dans une queue BG (ou en match).
		bool     inQueue           = false;
		uint16_t queuedBgType      = 0;
		uint8_t  queuedFaction     = 0; ///< 0=Alliance, 1=Horde
		uint32_t estimatedWaitSec  = 0;
		uint32_t queuePosition     = 0;

		/// Match actif (push opcode 136 a la reception). Reinitialise au
		/// MatchEndNotification.
		std::optional<uint64_t> activeMatchId;
		uint16_t                activeMatchBgType = 0;
		std::string             activeMatchMap;
		uint8_t                 activeAllianceCount = 0;
		uint8_t                 activeHordeCount    = 0;
		uint32_t                allianceScore       = 0;
		uint32_t                hordeScore          = 0;
		uint32_t                matchElapsedSec     = 0;

		/// Resultat du dernier match (push opcode 138). Affiche transitoirement
		/// par le renderer.
		std::optional<uint8_t>  lastMatchWinner; ///< 0=Alliance, 1=Horde, 2=Draw
		uint32_t                lastMatchAllianceScore = 0;
		uint32_t                lastMatchHordeScore    = 0;
		uint32_t                lastMatchDurationSec   = 0;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Inscrit en queue."). Vide par defaut.
		std::string lastInfoText;
	};

	/// Presenter pour la fenetre BattleGround cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main (comme les autres presenters UI).
	class BattleGroundUiPresenter final
	{
	public:
		BattleGroundUiPresenter() = default;

		BattleGroundUiPresenter(const BattleGroundUiPresenter&)            = delete;
		BattleGroundUiPresenter& operator=(const BattleGroundUiPresenter&) = delete;

		~BattleGroundUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.10 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / Queue / LeaveQueue / LeaveMatch.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie BG_LIST_REQUEST. Reponse via OnListResponse.
		void RequestList();

		/// Envoie BG_QUEUE_REQUEST avec \p bgType (1/2/3 V1) et \p faction
		/// (0=Alliance, 1=Horde). Reponse via OnQueueResponse.
		void Queue(uint16_t bgType, uint8_t faction);

		/// Envoie BG_LEAVE_QUEUE_REQUEST. Reponse via OnLeaveQueueResponse.
		void LeaveQueue();

		/// Envoie BG_LEAVE_MATCH_REQUEST (forfait V1, fire-and-forget).
		/// Pas de Response paire ; le master pousse a la place un
		/// MatchEndNotification avec winnerFaction = opposite.
		void LeaveMatch();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit BG_LIST_RESPONSE. Remplace la cache locale.
		void OnListResponse(const engine::network::BgListResponsePayload& resp);

		/// Recoit BG_QUEUE_RESPONSE. Si OK, marque inQueue = true et stocke
		/// estimatedWaitSec + queuePosition. Sinon, ecrit dans lastErrorText.
		void OnQueueResponse(const engine::network::BgQueueResponsePayload& resp);

		/// Recoit BG_LEAVE_QUEUE_RESPONSE. Si OK, clear l'etat de queue local.
		void OnLeaveQueueResponse(const engine::network::BgLeaveQueueResponsePayload& resp);

		/// Recoit un push BG_MATCH_START_NOTIFICATION : arme l'etat match
		/// actif (matchId + bgType + map + counts).
		void OnMatchStartNotification(const engine::network::BgMatchStartNotificationPayload& note);

		/// Recoit un push BG_SCORE_UPDATE_NOTIFICATION : update les scores
		/// + elapsed du match actif.
		void OnScoreUpdateNotification(const engine::network::BgScoreUpdateNotificationPayload& note);

		/// Recoit un push BG_MATCH_END_NOTIFICATION : arme le toast result
		/// (winner + scores + duration) puis clear l'etat match actif.
		void OnMatchEndNotification(const engine::network::BgMatchEndNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const BattleGroundUiState& GetState() const { return m_state; }

	private:
		/// Clear l'etat de queue local (appele a OnLeaveQueueResponse Ok ou a
		/// la reception d'un MatchStartNotification).
		void ClearQueueState();

		/// Clear l'etat match actif local (appele a OnMatchEndNotification).
		void ClearActiveMatch();

		/// Memorise le bgType et faction choisis en attente de la reponse Ok.
		uint16_t m_pendingBgType  = 0;
		uint8_t  m_pendingFaction = 0;

		bool                m_initialized = false;
		BattleGroundUiState m_state{};
		SendCallback        m_send;
	};
}
