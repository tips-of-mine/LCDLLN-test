// CMANGOS.30 (Phase 5.30 step 3+4) — Implementation CinematicUiPresenter.

#include "src/client/cinematics/CinematicUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	CinematicUiPresenter::~CinematicUiPresenter()
	{
		Shutdown();
	}

	bool CinematicUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[CinematicUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_currentSeq = {};
		m_lastTickRelMs = 0u;
		LOG_INFO(Core, "[CinematicUiPresenter] Init OK");
		return true;
	}

	void CinematicUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		m_currentSeq = {};
		m_lastTickRelMs = 0u;
		// Ne pas reset m_send : il est cable une fois au boot (Engine::Shutdown
		// est responsable du teardown ordonne).
		LOG_INFO(Core, "[CinematicUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------

	bool CinematicUiPresenter::LoadSequence(uint32_t sequenceId)
	{
		using engine::server::cinematics::CameraKeyframe;
		using engine::server::cinematics::CinematicSequence;

		// V1 simplification : pas de parser JSON pour le moment. On retourne
		// une sequence hardcodee 5 secondes 2 keyframes pour permettre au
		// flow E2E (push -> playback -> ack) d'etre testable. Le ticket data
		// sera fait dans une sub-PR future qui ajoutera un vrai parser JSON
		// + le contenu game/data/cinematics/seq<id>.json.
		CinematicSequence seq;
		seq.id = sequenceId;
		CameraKeyframe k0;
		k0.tsMs = 0u;
		k0.posX = 0.0f;  k0.posY = 0.0f;  k0.posZ = 0.0f;
		k0.lookX = 1.0f; k0.lookY = 0.0f; k0.lookZ = 0.0f;
		k0.soundCue.clear();
		CameraKeyframe k1;
		k1.tsMs = 5000u; // 5 secondes
		k1.posX = 10.0f; k1.posY = 2.0f;  k1.posZ = 0.0f;
		k1.lookX = 1.0f; k1.lookY = 0.0f; k1.lookZ = 0.0f;
		k1.soundCue.clear();
		seq.keyframes.push_back(k0);
		seq.keyframes.push_back(k1);

		m_currentSeq = std::move(seq);
		LOG_INFO(Core, "[CinematicUiPresenter] LoadSequence id={} (V1 hardcode 5s, 2 keyframes)",
			sequenceId);
		return true;
	}

	void CinematicUiPresenter::NotifyCompletion(uint8_t completionState)
	{
		using namespace engine::network;

		const uint32_t seqId = m_state.activeSequenceId;
		if (m_send && seqId != 0u)
		{
			const auto payload = BuildCinematicAckRequestPayload(seqId, completionState);
			if (!m_send(kOpcodeCinematicAckRequest, payload))
			{
				LOG_WARN(Net, "[CinematicUiPresenter] NotifyCompletion: send failed (seq={})", seqId);
			}
			else
			{
				LOG_INFO(Net, "[CinematicUiPresenter] Ack sent seq={} state={}",
					seqId, static_cast<unsigned>(completionState));
			}
		}

		// Clear le state local quoi qu'il arrive (le master ne tracke pas en V1).
		m_state.isPlaying        = false;
		m_state.activeSequenceId = 0u;
		m_state.startTimeMs      = 0u;
		m_state.totalDurationMs  = 0u;
		m_state.currentTimeMs    = 0u;
		m_state.inputDisabled    = false;
		m_state.lastSoundCue.clear();
		m_currentSeq             = {};
		m_lastTickRelMs          = 0u;
	}

	void CinematicUiPresenter::FireSoundCuesInRange(uint64_t prevMs, uint64_t nowMs)
	{
		// Parcourt les keyframes de la sequence et declenche celles dont tsMs
		// est dans (prevMs, nowMs]. V1 : on log + on stocke le dernier cue dans
		// lastSoundCue. Le ticket audio sera fait dans une sub-PR future pour
		// brancher reellement le moteur de son (e.g. miniaudio).
		for (const auto& kf : m_currentSeq.keyframes)
		{
			if (kf.soundCue.empty()) continue;
			if (kf.tsMs > prevMs && kf.tsMs <= nowMs)
			{
				m_state.lastSoundCue = kf.soundCue;
				LOG_INFO(Core, "[CinematicUiPresenter] SoundCue fired t={}ms cue='{}'",
					kf.tsMs, kf.soundCue);
			}
		}
	}

	// -------------------------------------------------------------------------

	void CinematicUiPresenter::OnPlayNotification(const engine::network::CinematicPlayNotificationPayload& note)
	{
		LOG_INFO(Net, "[CinematicUiPresenter] OnPlayNotification seq={} reason={}",
			note.sequenceId, static_cast<unsigned>(note.reason));

		// Si une lecture est en cours, on l'interrompt (notify Interrupted) avant
		// de demarrer la nouvelle. Cas rare mais defensif.
		if (m_state.isPlaying)
		{
			LOG_WARN(Net, "[CinematicUiPresenter] OnPlayNotification: previous cinematic seq={} interrupted",
				m_state.activeSequenceId);
			NotifyCompletion(static_cast<uint8_t>(engine::network::CinematicCompletionState::Interrupted));
		}

		if (!LoadSequence(note.sequenceId))
		{
			m_state.lastErrorText = "Sequence cinematique introuvable.";
			LOG_WARN(Core, "[CinematicUiPresenter] LoadSequence failed seq={}", note.sequenceId);
			return;
		}
		if (m_currentSeq.keyframes.size() < 2u)
		{
			m_state.lastErrorText = "Sequence cinematique invalide (moins de 2 keyframes).";
			LOG_WARN(Core, "[CinematicUiPresenter] sequence has < 2 keyframes seq={}", note.sequenceId);
			m_currentSeq = {};
			return;
		}

		m_state.lastErrorText.clear();
		m_state.isPlaying        = true;
		m_state.activeSequenceId = note.sequenceId;
		// startTimeMs est rempli a la premiere frame de Tick (on n'a pas le now
		// ici dans le path push). totalDurationMs est calcule depuis la sequence.
		m_state.startTimeMs      = 0u;
		m_state.totalDurationMs  = m_currentSeq.keyframes.back().tsMs;
		m_state.currentTimeMs    = 0u;
		m_state.inputDisabled    = true;
		m_state.lastSoundCue.clear();
		m_lastTickRelMs          = 0u;

		// Position initiale : premiere keyframe.
		const auto& k0 = m_currentSeq.keyframes.front();
		m_state.camPosX  = k0.posX;
		m_state.camPosY  = k0.posY;
		m_state.camPosZ  = k0.posZ;
		m_state.camLookX = k0.lookX;
		m_state.camLookY = k0.lookY;
		m_state.camLookZ = k0.lookZ;
	}

	void CinematicUiPresenter::OnAckResponse(const engine::network::CinematicAckResponsePayload& resp)
	{
		// V1 : log only. Le presenter a deja clear son state avant l'envoi de
		// l'ack (NotifyCompletion). On note juste si le master a renvoye une
		// erreur (ne devrait pas en V1, mais robustesse).
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[CinematicUiPresenter] OnAckResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		LOG_DEBUG(Net, "[CinematicUiPresenter] OnAckResponse Ok");
	}

	void CinematicUiPresenter::OnSkipResponse(const engine::network::CinematicSkipResponsePayload& resp)
	{
		using engine::network::CinematicErrorCode;

		if (resp.error != 0u)
		{
			const auto err = static_cast<CinematicErrorCode>(resp.error);
			if (err == CinematicErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide.";
			else if (err == CinematicErrorCode::SkipNotAllowed)
				m_state.lastErrorText = "Cette cinematique ne peut pas etre passee.";
			else
				m_state.lastErrorText = "Erreur cinematique.";
			LOG_WARN(Net, "[CinematicUiPresenter] OnSkipResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		if (!resp.allowed)
		{
			m_state.lastErrorText = "Cette cinematique ne peut pas etre passee.";
			LOG_INFO(Net, "[CinematicUiPresenter] OnSkipResponse: skip refused (continue playback)");
			return;
		}

		// Skip autorise : termine la lecture en cours.
		if (m_state.isPlaying)
		{
			LOG_INFO(Net, "[CinematicUiPresenter] OnSkipResponse: skip allowed -> end playback");
			NotifyCompletion(static_cast<uint8_t>(engine::network::CinematicCompletionState::SkippedByUser));
		}
		else
		{
			LOG_DEBUG(Net, "[CinematicUiPresenter] OnSkipResponse: allowed but no active cinematic");
		}
	}

	// -------------------------------------------------------------------------

	void CinematicUiPresenter::RequestSkip()
	{
		if (!m_state.isPlaying)
		{
			LOG_DEBUG(Net, "[CinematicUiPresenter] RequestSkip: no active cinematic, no-op");
			return;
		}
		if (!m_send)
		{
			LOG_WARN(Net, "[CinematicUiPresenter] RequestSkip: no send callback");
			return;
		}
		const auto payload = engine::network::BuildCinematicSkipRequestPayload(m_state.activeSequenceId);
		if (!m_send(engine::network::kOpcodeCinematicSkipRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (skip cinematique).";
			LOG_WARN(Net, "[CinematicUiPresenter] RequestSkip: send failed");
			return;
		}
		LOG_INFO(Net, "[CinematicUiPresenter] CinematicSkipRequest queued (seq={})",
			m_state.activeSequenceId);
	}

	// -------------------------------------------------------------------------

	void CinematicUiPresenter::Tick(uint64_t nowMs)
	{
		if (!m_state.isPlaying) return;
		if (m_currentSeq.keyframes.size() < 2u) return;

		// Premiere frame : on initialise startTimeMs.
		if (m_state.startTimeMs == 0u)
		{
			m_state.startTimeMs = nowMs;
			m_lastTickRelMs     = 0u;
		}

		const uint64_t relMs = (nowMs > m_state.startTimeMs) ? (nowMs - m_state.startTimeMs) : 0u;
		m_state.currentTimeMs = relMs;

		// Sound cues franchis entre la derniere frame et celle-ci.
		FireSoundCuesInRange(m_lastTickRelMs, relMs);
		m_lastTickRelMs = relMs;

		// Detection de fin : depasse la derniere keyframe.
		if (relMs >= m_state.totalDurationMs)
		{
			LOG_INFO(Core, "[CinematicUiPresenter] Sequence ended seq={} (rel={}ms >= total={}ms)",
				m_state.activeSequenceId, relMs, m_state.totalDurationMs);
			NotifyCompletion(static_cast<uint8_t>(engine::network::CinematicCompletionState::EndedNormally));
			return;
		}

		// Interpolation lineaire de la camera via SampleAt.
		engine::server::cinematics::InterpolatedFrame frame{};
		if (engine::server::cinematics::SampleAt(m_currentSeq, relMs, frame))
		{
			m_state.camPosX  = frame.posX;
			m_state.camPosY  = frame.posY;
			m_state.camPosZ  = frame.posZ;
			m_state.camLookX = frame.lookX;
			m_state.camLookY = frame.lookY;
			m_state.camLookZ = frame.lookZ;
		}
	}
}
