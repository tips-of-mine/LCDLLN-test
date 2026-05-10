// CMANGOS.24 (Phase 3.24 step 3+4) — Implementation ReputationUiPresenter.

#include "src/client/reputation/ReputationUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	namespace
	{
		/// Duree d'affichage du toast push en secondes.
		constexpr float kToastDurationSeconds = 3.0f;
	}

	ReputationUiPresenter::~ReputationUiPresenter()
	{
		Shutdown();
	}

	bool ReputationUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ReputationUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_state.layoutValid = true;
		m_clockSeconds = 0.0f;
		LOG_INFO(Core, "[ReputationUiPresenter] Init OK");
		return true;
	}

	void ReputationUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		// Ne pas reset m_send : il est cable une fois au boot et on garde la
		// reference (Engine::Shutdown sera responsable du teardown ordonne).
		LOG_INFO(Core, "[ReputationUiPresenter] Destroyed");
	}

	void ReputationUiPresenter::RequestReputationList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[ReputationUiPresenter] RequestReputationList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildReputationListRequestPayload();
		m_state.isLoading = true;
		if (!m_send(engine::network::kOpcodeReputationListRequest, payload))
		{
			m_state.isLoading = false;
			m_state.lastErrorText = "Echec envoi (liste reputations).";
			LOG_WARN(Net, "[ReputationUiPresenter] RequestReputationList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[ReputationUiPresenter] ReputationListRequest queued");
	}

	// -------------------------------------------------------------------------

	void ReputationUiPresenter::RebuildEntriesFromResponse(const engine::network::ReputationListResponsePayload& resp)
	{
		m_state.entries.clear();
		m_state.entries.reserve(resp.entries.size());
		for (const auto& e : resp.entries)
		{
			ReputationEntryView v;
			v.factionId = e.factionId;
			v.value     = e.value;
			v.standing  = e.standing;
			m_state.entries.push_back(v);
		}
	}

	void ReputationUiPresenter::OnListResponse(const engine::network::ReputationListResponsePayload& resp)
	{
		m_state.isLoading = false;
		if (resp.error != 0u)
		{
			using engine::network::ReputationErrorCode;
			if (static_cast<ReputationErrorCode>(resp.error) == ReputationErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur lors du chargement des reputations.";
			LOG_WARN(Net, "[ReputationUiPresenter] OnListResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		RebuildEntriesFromResponse(resp);
		LOG_INFO(Net, "[ReputationUiPresenter] OnListResponse: {} entries",
			m_state.entries.size());
	}

	void ReputationUiPresenter::UpdateOrInsertEntry(uint32_t factionId, int32_t newValue, int8_t newStanding)
	{
		for (auto& e : m_state.entries)
		{
			if (e.factionId == factionId)
			{
				e.value    = newValue;
				e.standing = newStanding;
				return;
			}
		}
		// Insert : la faction n'etait pas dans la liste (rep gagnee pour la
		// premiere fois). Ajout en queue ; le renderer triera.
		ReputationEntryView v;
		v.factionId = factionId;
		v.value     = newValue;
		v.standing  = newStanding;
		m_state.entries.push_back(v);
	}

	void ReputationUiPresenter::OnUpdateNotification(const engine::network::ReputationUpdateNotificationPayload& note)
	{
		UpdateOrInsertEntry(note.factionId, note.newValue, note.newStanding);

		// Arme le toast pour ~3s.
		ReputationEntryView v;
		v.factionId = note.factionId;
		v.value     = note.newValue;
		v.standing  = note.newStanding;
		m_state.lastUpdateToast        = v;
		m_state.lastUpdateToastDelta   = note.delta;
		m_state.lastUpdateToastExpireAt = m_clockSeconds + kToastDurationSeconds;

		LOG_INFO(Net, "[ReputationUiPresenter] OnUpdateNotification: faction={} newValue={} standing={} delta={}",
			note.factionId, note.newValue, static_cast<int>(note.newStanding), note.delta);
	}

	void ReputationUiPresenter::TickToast(float deltaSeconds)
	{
		if (deltaSeconds <= 0.0f) return;
		m_clockSeconds += deltaSeconds;
		if (m_state.lastUpdateToast.has_value()
			&& m_clockSeconds >= m_state.lastUpdateToastExpireAt)
		{
			m_state.lastUpdateToast.reset();
			m_state.lastUpdateToastDelta    = 0;
			m_state.lastUpdateToastExpireAt = 0.0f;
		}
	}
}
