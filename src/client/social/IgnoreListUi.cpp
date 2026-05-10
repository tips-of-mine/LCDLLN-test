// CMANGOS.25 (Phase 3.25 step 3+4) — Implementation IgnoreListUiPresenter.

#include "src/client/social/IgnoreListUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	IgnoreListUiPresenter::~IgnoreListUiPresenter()
	{
		Shutdown();
	}

	bool IgnoreListUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[IgnoreListUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_state.layoutValid = true;
		m_ignoredAccountIds.clear();
		LOG_INFO(Core, "[IgnoreListUiPresenter] Init OK");
		return true;
	}

	void IgnoreListUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		m_ignoredAccountIds.clear();
		// Ne pas reset m_send : il est cable une fois au boot et on garde la
		// reference (Engine::Shutdown sera responsable du teardown ordonne).
		LOG_INFO(Core, "[IgnoreListUiPresenter] Destroyed");
	}

	void IgnoreListUiPresenter::IgnoreAccount(uint64_t targetAccountId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] IgnoreAccount: no send callback");
			return;
		}
		const auto payload = engine::network::BuildIgnoreAddRequestPayload(targetAccountId);
		if (!m_send(engine::network::kOpcodeIgnoreAddRequest, payload))
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] IgnoreAccount: send failed (target={})", targetAccountId);
			return;
		}
		LOG_DEBUG(Net, "[IgnoreListUiPresenter] IgnoreAddRequest queued (target={})", targetAccountId);
	}

	void IgnoreListUiPresenter::UnignoreAccount(uint64_t targetAccountId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] UnignoreAccount: no send callback");
			return;
		}
		const auto payload = engine::network::BuildIgnoreRemoveRequestPayload(targetAccountId);
		if (!m_send(engine::network::kOpcodeIgnoreRemoveRequest, payload))
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] UnignoreAccount: send failed (target={})", targetAccountId);
			return;
		}
		LOG_DEBUG(Net, "[IgnoreListUiPresenter] IgnoreRemoveRequest queued (target={})", targetAccountId);
	}

	void IgnoreListUiPresenter::RequestIgnoreList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] RequestIgnoreList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildIgnoreListRequestPayload();
		if (!m_send(engine::network::kOpcodeIgnoreListRequest, payload))
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] RequestIgnoreList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[IgnoreListUiPresenter] IgnoreListRequest queued");
	}

	void IgnoreListUiPresenter::OnIgnoreAddResponse(const engine::network::IgnoreAddResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] OnIgnoreAddResponse: server error code={} target={}",
				static_cast<unsigned>(resp.error), resp.targetAccountId);
			return;
		}
		m_ignoredAccountIds.insert(resp.targetAccountId);
		RebuildState();
		LOG_INFO(Net, "[IgnoreListUiPresenter] OnIgnoreAddResponse: target={} cached size={}",
			resp.targetAccountId, m_ignoredAccountIds.size());
	}

	void IgnoreListUiPresenter::OnIgnoreRemoveResponse(const engine::network::IgnoreRemoveResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] OnIgnoreRemoveResponse: server error code={} target={}",
				static_cast<unsigned>(resp.error), resp.targetAccountId);
			return;
		}
		m_ignoredAccountIds.erase(resp.targetAccountId);
		RebuildState();
		LOG_INFO(Net, "[IgnoreListUiPresenter] OnIgnoreRemoveResponse: target={} cached size={}",
			resp.targetAccountId, m_ignoredAccountIds.size());
	}

	void IgnoreListUiPresenter::OnIgnoreListResponse(const engine::network::IgnoreListResponsePayload& resp)
	{
		if (resp.error != 0u)
		{
			LOG_WARN(Net, "[IgnoreListUiPresenter] OnIgnoreListResponse: server error code={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		// Le serveur est l'autorite : on remplace la cache complete.
		m_ignoredAccountIds.clear();
		m_ignoredAccountIds.reserve(resp.ignoredAccountIds.size());
		for (auto id : resp.ignoredAccountIds)
			m_ignoredAccountIds.insert(id);
		RebuildState();
		LOG_INFO(Net, "[IgnoreListUiPresenter] OnIgnoreListResponse: {} accounts cached",
			m_ignoredAccountIds.size());
	}

	bool IgnoreListUiPresenter::IsIgnoredLocal(uint64_t accountId) const
	{
		return m_ignoredAccountIds.count(accountId) != 0u;
	}

	void IgnoreListUiPresenter::RebuildState()
	{
		m_state.ignoredAccountIds.clear();
		m_state.ignoredAccountIds.reserve(m_ignoredAccountIds.size());
		for (auto id : m_ignoredAccountIds)
			m_state.ignoredAccountIds.push_back(id);
		m_state.layoutValid = true;
	}
}
