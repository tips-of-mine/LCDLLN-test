#pragma once
// CMANGOS.30 (Phase 5.30 step 3+4) — Presenter client de la lecture de
// cinematiques (cutscene player).
//
// Le presenter gere une seule cinematique active a la fois. Quand un push
// PlayNotification (108) arrive, le presenter charge la sequence locale
// (game/data/cinematics/seq<id>.json) et demarre la lecture. Pendant le
// playback, Tick(nowMs) interpole la camera via SampleAt et pousse les
// sound cues. A la fin (last keyframe), le presenter envoie un
// CinematicAckRequest avec completionState=EndedNormally. Si l'utilisateur
// appuie sur Esc, RequestSkip envoie un CinematicSkipRequest, et a la
// reponse Ok+allowed, le presenter termine la lecture avec
// completionState=SkippedByUser.
//
// V1 simplification : LoadSequence retourne une sequence hardcodee si pas
// de fichier data trouve (2 keyframes : start + end de 5 secondes). Le
// ticket data sera fait plus tard.
//
// Send : fire-and-forget via un callback (cf. m_send dans QuestUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 108/110/112
// vers les OnXxx du presenter.

#include "src/shardd/cinematics/CinematicSequence.h"
#include "src/shared/network/CinematicPayloads.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Etat snapshot expose au renderer ImGui. Le renderer lit ces champs en
	/// lecture seule.
	struct CinematicUiState
	{
		/// True si une cinematique est en cours de lecture.
		bool     isPlaying          = false;
		/// Id de la sequence active (significatif si isPlaying).
		uint32_t activeSequenceId   = 0;
		/// Timestamp absolu (ms) du debut de la lecture cote client.
		uint64_t startTimeMs        = 0;
		/// Duree totale de la sequence (derniere keyframe.tsMs).
		uint64_t totalDurationMs    = 0;
		/// Timestamp courant relatif au debut (mis a jour par Tick).
		uint64_t currentTimeMs      = 0;

		// Etat camera interpole a la frame courante.
		float    camPosX  = 0.0f;
		float    camPosY  = 0.0f;
		float    camPosZ  = 0.0f;
		float    camLookX = 0.0f;
		float    camLookY = 0.0f;
		float    camLookZ = 0.0f;

		/// Dernier sound cue declenche (vide si aucun encore). Le renderer
		/// peut l'afficher discretement en debug.
		std::string lastSoundCue;

		/// True quand l'input gameplay doit etre desactive (overlay actif).
		/// Le Engine lit ce flag pour gater les inputs.
		bool        inputDisabled  = false;

		/// Texte d'erreur transitoire (e.g. "Sequence introuvable"). Vide
		/// par defaut.
		std::string lastErrorText;
	};

	/// Presenter pour la lecture de cinematiques. Doit etre Init() avant tout
	/// usage du callback. Thread : main (comme les autres presenters UI).
	class CinematicUiPresenter final
	{
	public:
		CinematicUiPresenter() = default;

		CinematicUiPresenter(const CinematicUiPresenter&)            = delete;
		CinematicUiPresenter& operator=(const CinematicUiPresenter&) = delete;

		~CinematicUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.30 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestSkip.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit un push CINEMATIC_PLAY_NOTIFICATION (108). Charge la sequence
		/// localement et demarre la lecture. Si une lecture est deja en cours,
		/// elle est interrompue (completionState=Interrupted) avant de demarrer
		/// la nouvelle.
		void OnPlayNotification(const engine::network::CinematicPlayNotificationPayload& note);

		/// Recoit CINEMATIC_ACK_RESPONSE (110). V1 : log only, pas de mutation
		/// d'etat (le presenter a deja clear son etat avant l'envoi de l'ack).
		void OnAckResponse(const engine::network::CinematicAckResponsePayload& resp);

		/// Recoit CINEMATIC_SKIP_RESPONSE (112). Si allowed=true, termine la
		/// lecture en cours et envoie un Ack avec completionState=SkippedByUser.
		/// Sinon, ecrit lastErrorText et continue la lecture.
		void OnSkipResponse(const engine::network::CinematicSkipResponsePayload& resp);

		// ---------------------------------------------------------------------
		// User input
		// ---------------------------------------------------------------------

		/// L'utilisateur a appuye sur Esc pendant une lecture. Envoie un
		/// CINEMATIC_SKIP_REQUEST au master. La reponse asynchrone via
		/// OnSkipResponse termine effectivement la lecture si allowed=true.
		/// No-op si pas de cinematique active.
		void RequestSkip();

		// ---------------------------------------------------------------------
		// Per-frame tick
		// ---------------------------------------------------------------------

		/// A appeler chaque frame quand IsInitialized(). Met a jour
		/// currentTimeMs, interpole la camera via SampleAt, pousse les sound
		/// cues franchis, et detecte la fin de sequence (envoie l'Ack avec
		/// EndedNormally). No-op si pas de cinematique active.
		///
		/// \param nowMs Timestamp absolu courant (ms ; meme reference que
		///              startTimeMs).
		void Tick(uint64_t nowMs);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const CinematicUiState& GetState() const { return m_state; }

	private:
		/// Charge la sequence depuis game/data/cinematics/seq<id>.json. V1 :
		/// fallback hardcode (2 keyframes : (0,0,0)->(10,0,0) sur 5 secondes)
		/// si le fichier n'existe pas. Future PR fera le vrai parser JSON.
		///
		/// \param sequenceId id de la sequence a charger.
		/// \return true si la sequence est valide (au moins 2 keyframes).
		///
		/// Effet de bord : ecrit dans m_currentSeq.
		bool LoadSequence(uint32_t sequenceId);

		/// Notifie le master que la lecture est terminee. Envoie un
		/// CINEMATIC_ACK_REQUEST avec le completionState donne, puis clear
		/// l'etat de lecture local.
		///
		/// \param completionState cf. CinematicCompletionState (0=EndedNormally,
		///                        1=SkippedByUser, 2=Interrupted).
		///
		/// Effet de bord : envoie un paquet sur la connexion master ; mute
		/// m_state.isPlaying et m_state.inputDisabled a false.
		void NotifyCompletion(uint8_t completionState);

		/// Pousse les sound cues franchis entre le tsMs precedent et le tsMs
		/// courant. V1 : log + ecrit lastSoundCue ; le ticket audio sera fait
		/// dans une sub-PR future pour brancher au moteur de son.
		///
		/// \param prevMs tsMs relatif a la derniere frame.
		/// \param nowMs  tsMs relatif a la frame courante.
		void FireSoundCuesInRange(uint64_t prevMs, uint64_t nowMs);

		bool                                            m_initialized   = false;
		engine::server::cinematics::CinematicSequence   m_currentSeq    {};
		CinematicUiState                                m_state         {};
		SendCallback                                    m_send;

		/// tsMs de la derniere frame (pour detecter la traversee de keyframes
		/// avec sound cue). Reset a 0 au debut de chaque sequence.
		uint64_t m_lastTickRelMs = 0u;
	};
}
