// M32.1 — Client-side friends list UI panel presenter implementation.

#include "engine/client/FriendsUi.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <string>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	FriendsUiPresenter::~FriendsUiPresenter()
	{
		if (m_initialized)
			Shutdown();
	}

	bool FriendsUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[FriendsUi] Init called more than once, ignoring");
			return true;
		}

		m_state      = {};
		m_elapsedSec = 0.0f;
		m_lastClickTimeSec = -1.0f;
		m_lastClickedName.clear();
		m_initialized = true;

		LOG_INFO(Core, "[FriendsUi] Init OK");
		return true;
	}

	void FriendsUiPresenter::Shutdown()
	{
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Core, "[FriendsUi] Destroyed");
	}

	bool FriendsUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendsUi] SetViewportSize called before Init");
			return false;
		}

		m_viewportWidth  = width;
		m_viewportHeight = height;
		m_state.layoutValid = (width > 0 && height > 0);

		LOG_DEBUG(Core, "[FriendsUi] Viewport set to {}x{}", width, height);
		return m_state.layoutValid;
	}

	// -------------------------------------------------------------------------
	// Data updates from network
	// -------------------------------------------------------------------------

	void FriendsUiPresenter::ApplyFriendList(const std::vector<FriendEntry>& friends)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendsUi] ApplyFriendList called before Init");
			return;
		}

		m_state.friends = friends;
		RebuildDebugText();

		LOG_INFO(Core, "[FriendsUi] Friend list updated ({} entries)", friends.size());
	}

	void FriendsUiPresenter::ApplyStatusUpdate(std::string_view friendName,
	                                            engine::server::PresenceStatus status)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendsUi] ApplyStatusUpdate called before Init");
			return;
		}

		for (auto& entry : m_state.friends)
		{
			if (entry.name == friendName)
			{
				entry.status = status;
				LOG_DEBUG(Core, "[FriendsUi] Status update: '{}' → {}", friendName, static_cast<int>(status));
				RebuildDebugText();
				return;
			}
		}

		// Friend not yet in the list (e.g. logged in before FriendListSync arrived); add a stub.
		FriendEntry stub;
		stub.name   = std::string(friendName);
		stub.status = status;
		m_state.friends.push_back(std::move(stub));
		RebuildDebugText();
		LOG_DEBUG(Core, "[FriendsUi] Status update for unknown friend '{}' → added stub", friendName);
	}

	// -------------------------------------------------------------------------
	// Interaction
	// -------------------------------------------------------------------------

	void FriendsUiPresenter::RegisterClick(std::string_view friendName, float nowSeconds)
	{
		if (!m_initialized)
			return;

		if (m_lastClickedName == friendName
			&& m_lastClickTimeSec >= 0.0f
			&& (nowSeconds - m_lastClickTimeSec) <= kDoubleClickThresholdSec)
		{
			// Double-click detected: trigger whisper shortcut.
			m_state.pendingWhisperTarget = std::string(friendName);
			m_state.hasWhisperRequest    = true;
			m_lastClickedName.clear();
			m_lastClickTimeSec = -1.0f;
			LOG_INFO(Core, "[FriendsUi] Whisper shortcut triggered for '{}'", friendName);
		}
		else
		{
			m_lastClickedName  = std::string(friendName);
			m_lastClickTimeSec = nowSeconds;
		}
	}

	void FriendsUiPresenter::Tick(float deltaSeconds)
	{
		if (!m_initialized)
			return;
		m_elapsedSec += deltaSeconds;
	}

	bool FriendsUiPresenter::ConsumeWhisperRequest(std::string& outTarget)
	{
		if (!m_state.hasWhisperRequest)
			return false;

		outTarget                    = std::move(m_state.pendingWhisperTarget);
		m_state.pendingWhisperTarget = {};
		m_state.hasWhisperRequest    = false;
		return true;
	}

	// -------------------------------------------------------------------------
	// Debug text
	// -------------------------------------------------------------------------

	std::string FriendsUiPresenter::BuildPanelText() const
	{
		if (!m_initialized)
			return "[FriendsUi] Not initialized\n";

		std::string out;
		out.reserve(256);
		out += "=== Friends ===\n";

		for (const auto& e : m_state.friends)
		{
			if (e.isPendingInbound)
				out += "[?] ";
			else
				out += "[" + std::string(PresenceLabel(e.status)) + "] ";
			out += e.name;
			out += '\n';
		}

		if (m_state.friends.empty())
			out += "(no friends)\n";

		return out;
	}

	const char* FriendsUiPresenter::PresenceLabel(engine::server::PresenceStatus status)
	{
		switch (status)
		{
			case engine::server::PresenceStatus::Online:  return "Online";
			case engine::server::PresenceStatus::Away:    return "Away";
			case engine::server::PresenceStatus::Busy:    return "Busy";
			case engine::server::PresenceStatus::Offline: return "Offline";
			default:                                       return "?";
		}
	}

	void FriendsUiPresenter::RebuildDebugText()
	{
		m_state.debugText = BuildPanelText();
	}
}
