// CMANGOS.39 (Phase 4.39 step 3+4) — Implementation SkillBookUiPresenter.

#include "src/client/skills/SkillBookUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdio>

namespace engine::client
{
	namespace
	{
		/// Duree d'affichage de l'indicateur Use (Success/Fail/Crit) en secondes.
		constexpr float kIndicatorDurationSeconds = 2.0f;
	}

	std::string GetSkillName(uint16_t skillId)
	{
		// V1 : table hardcode des skills du starter set. La table de skills
		// reelle viendra avec CMANGOS.41 (Trainers) ou CMANGOS.40 (Crafting
		// catalog) qui exposera un map id -> name localise.
		switch (skillId)
		{
		case 1u: return "Cuisine";
		case 2u: return "Herboristerie";
		case 3u: return "Mineur";
		case 4u: return "Premiers soins";
		case 5u: return "Crochetage";
		default: break;
		}
		char buf[32]{};
		std::snprintf(buf, sizeof(buf), "Skill #%u", static_cast<unsigned>(skillId));
		return buf;
	}

	SkillBookUiPresenter::~SkillBookUiPresenter()
	{
		Shutdown();
	}

	bool SkillBookUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[SkillBookUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_state.layoutValid = true;
		m_clockSeconds = 0.0f;
		LOG_INFO(Core, "[SkillBookUiPresenter] Init OK");
		return true;
	}

	void SkillBookUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[SkillBookUiPresenter] Destroyed");
	}

	void SkillBookUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildSkillsListRequestPayload();
		m_state.isLoading = true;
		if (!m_send(engine::network::kOpcodeSkillsListRequest, payload))
		{
			m_state.isLoading = false;
			m_state.lastErrorText = "Echec envoi (liste skills).";
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[SkillBookUiPresenter] SkillsListRequest queued");
	}

	void SkillBookUiPresenter::RequestLearn(uint16_t skillId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestLearn: no send callback");
			return;
		}
		const auto payload = engine::network::BuildSkillLearnRequestPayload(skillId);
		if (!m_send(engine::network::kOpcodeSkillLearnRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (apprendre skill).";
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestLearn: send failed (skillId={})", skillId);
			return;
		}
		LOG_DEBUG(Net, "[SkillBookUiPresenter] SkillLearnRequest queued skillId={}", skillId);
	}

	void SkillBookUiPresenter::RequestUse(uint16_t skillId, uint64_t targetEntityId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestUse: no send callback");
			return;
		}
		const auto payload = engine::network::BuildSkillUseRequestPayload(skillId, targetEntityId);
		if (!m_send(engine::network::kOpcodeSkillUseRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (utiliser skill).";
			LOG_WARN(Net, "[SkillBookUiPresenter] RequestUse: send failed (skillId={})", skillId);
			return;
		}
		LOG_DEBUG(Net, "[SkillBookUiPresenter] SkillUseRequest queued skillId={} target={}",
			skillId, targetEntityId);
	}

	// -------------------------------------------------------------------------

	void SkillBookUiPresenter::RebuildSkillsFromResponse(const engine::network::SkillsListResponsePayload& resp)
	{
		m_state.skills.clear();
		m_state.skills.reserve(resp.skills.size());
		for (const auto& e : resp.skills)
		{
			SkillBookEntryView v;
			v.skillId = e.skillId;
			v.value   = e.value;
			v.cap     = e.cap;
			v.bonus   = e.bonus;
			v.name    = GetSkillName(e.skillId);
			m_state.skills.push_back(std::move(v));
		}
	}

	void SkillBookUiPresenter::OnListResponse(const engine::network::SkillsListResponsePayload& resp)
	{
		m_state.isLoading = false;
		if (resp.error != 0u)
		{
			using engine::network::SkillErrorCode;
			if (static_cast<SkillErrorCode>(resp.error) == SkillErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur lors du chargement des skills.";
			LOG_WARN(Net, "[SkillBookUiPresenter] OnListResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		RebuildSkillsFromResponse(resp);
		LOG_INFO(Net, "[SkillBookUiPresenter] OnListResponse: {} skills",
			m_state.skills.size());
	}

	void SkillBookUiPresenter::UpdateOrInsertEntry(uint16_t skillId, uint16_t newValue, uint16_t newCap, uint16_t bonus)
	{
		for (auto& e : m_state.skills)
		{
			if (e.skillId == skillId)
			{
				e.value = newValue;
				e.cap   = newCap;
				if (bonus != 0u)
					e.bonus = bonus;
				return;
			}
		}
		// Insert : le skill n'etait pas dans la liste (Learn d'un nouveau).
		SkillBookEntryView v;
		v.skillId = skillId;
		v.value   = newValue;
		v.cap     = newCap;
		v.bonus   = bonus;
		v.name    = GetSkillName(skillId);
		m_state.skills.push_back(std::move(v));
	}

	void SkillBookUiPresenter::OnLearnResponse(const engine::network::SkillLearnResponsePayload& resp)
	{
		using engine::network::SkillErrorCode;
		if (resp.error != 0u)
		{
			switch (static_cast<SkillErrorCode>(resp.error))
			{
			case SkillErrorCode::AlreadyLearned:
				m_state.lastErrorText = "Vous connaissez deja cette competence.";
				break;
			case SkillErrorCode::UnknownSkill:
				m_state.lastErrorText = "Competence inconnue.";
				break;
			case SkillErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur lors de l'apprentissage.";
				break;
			}
			LOG_WARN(Net, "[SkillBookUiPresenter] OnLearnResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		LOG_INFO(Net, "[SkillBookUiPresenter] OnLearnResponse OK initialCap={}", resp.initialCap);
		// L'insertion / update viendra avec la push UpgradeNotification (delta=0)
		// emise par le serveur juste apres la response. On laisse donc OnUpgradeNotification
		// mettre a jour la liste.
	}

	void SkillBookUiPresenter::ArmUseIndicator(uint16_t skillId, uint8_t result, uint16_t delta)
	{
		m_state.lastUseResult     = result;
		m_state.lastUseSkillId    = skillId;
		m_state.lastUseDelta      = delta;
		m_state.lastUseExpireAt   = m_clockSeconds + kIndicatorDurationSeconds;
	}

	void SkillBookUiPresenter::OnUseResponse(const engine::network::SkillUseResponsePayload& resp)
	{
		using engine::network::SkillErrorCode;
		if (resp.error != 0u)
		{
			switch (static_cast<SkillErrorCode>(resp.error))
			{
			case SkillErrorCode::SkillNotLearned:
				m_state.lastErrorText = "Vous ne connaissez pas cette competence.";
				break;
			case SkillErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur lors de l'utilisation.";
				break;
			}
			LOG_WARN(Net, "[SkillBookUiPresenter] OnUseResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		LOG_INFO(Net, "[SkillBookUiPresenter] OnUseResponse: result={} delta={}",
			static_cast<unsigned>(resp.result), resp.deltaValue);
		// L'indicateur prend le skillId du dernier Use ; on ne le sait pas
		// directement depuis la response (V1 : pas d'echo). On met skillId=0
		// qui signifie "indicateur sans highlight specifique" cote renderer.
		// Si gain effectif, la push UpgradeNotification fera le highlight propre.
		ArmUseIndicator(0u, resp.result, resp.deltaValue);
	}

	void SkillBookUiPresenter::OnUpgradeNotification(const engine::network::SkillUpgradeNotificationPayload& note)
	{
		UpdateOrInsertEntry(note.skillId, note.newValue, note.newCap);

		// Si delta > 0, on rearme l'indicateur avec le skillId precis (highlight).
		// Si delta == 0 (cas du Learn), pas d'indicateur visuel additionnel.
		if (note.delta > 0)
		{
			ArmUseIndicator(note.skillId, 0u /*Success*/, static_cast<uint16_t>(note.delta));
		}

		LOG_INFO(Net, "[SkillBookUiPresenter] OnUpgradeNotification: skill={} newValue={} newCap={} delta={}",
			note.skillId, note.newValue, note.newCap, static_cast<int>(note.delta));
	}

	void SkillBookUiPresenter::TickIndicator(float deltaSeconds)
	{
		if (deltaSeconds <= 0.0f) return;
		m_clockSeconds += deltaSeconds;
		if (m_state.lastUseResult.has_value()
			&& m_clockSeconds >= m_state.lastUseExpireAt)
		{
			m_state.lastUseResult.reset();
			m_state.lastUseDelta    = 0;
			m_state.lastUseSkillId  = 0;
			m_state.lastUseExpireAt = 0.0f;
		}
	}
}
