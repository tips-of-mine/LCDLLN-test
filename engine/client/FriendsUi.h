#pragma once
// M32.1 — Client-side friends list UI panel presenter.
// Displays online status per friend, exposes whisper shortcut on double-click.

#include "engine/server/ServerProtocol.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// One friend entry displayed in the friends panel.
	struct FriendEntry
	{
		/// Display name of the friend.
		std::string                       name;
		/// Current presence status received from the server.
		engine::server::PresenceStatus    status         = engine::server::PresenceStatus::Offline;
		/// True when this entry is an inbound pending friend request awaiting local acceptance.
		bool                              isPendingInbound = false;
	};

	/// Fully resolved state of the friends panel for a UI rendering layer.
	struct FriendsPanelState
	{
		std::vector<FriendEntry> friends;
		/// Non-empty when the local player double-clicked a friend name (whisper shortcut).
		std::string              pendingWhisperTarget;
		bool                     hasWhisperRequest = false;
		/// Human-readable debug dump of the panel (for debug overlays).
		std::string              debugText;
		bool                     layoutValid = false;
	};

	/// Presents the M32.1 friends list panel: status indicators, double-click whisper shortcut.
	class FriendsUiPresenter final
	{
	public:
		FriendsUiPresenter() = default;

		/// Non-copyable, non-movable.
		FriendsUiPresenter(const FriendsUiPresenter&)            = delete;
		FriendsUiPresenter& operator=(const FriendsUiPresenter&) = delete;

		~FriendsUiPresenter();

		/// Initialize the presenter. Emits LOG_INFO on success.
		bool Init();

		/// Shut down the presenter and release state. Emits LOG_INFO on completion.
		void Shutdown();

		/// Update the viewport-dependent layout for the friends panel.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Replace the friends list with the data received from a FriendListSync server packet.
		void ApplyFriendList(const std::vector<FriendEntry>& friends);

		/// Apply a single friend presence status change (FriendStatusUpdate server packet).
		void ApplyStatusUpdate(std::string_view friendName, engine::server::PresenceStatus status);

		/// Register a click on a friend entry by display name.
		/// A second click within \ref kDoubleClickThresholdSec triggers the whisper shortcut.
		void RegisterClick(std::string_view friendName, float nowSeconds);

		/// Advance per-frame timers (double-click window, etc.).
		void Tick(float deltaSeconds);

		/// Consume a pending whisper request set by a double-click.
		/// Returns true and fills \p outTarget when a request is available; clears internal state.
		bool ConsumeWhisperRequest(std::string& outTarget);

		/// Return the immutable panel state.
		const FriendsPanelState& GetState() const { return m_state; }

		/// Build a multi-line text dump for a debug widget.
		std::string BuildPanelText() const;

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Seconds within which a second click is treated as a double-click.
		static constexpr float kDoubleClickThresholdSec = 0.5f;

		/// Rebuild the debug text after any state change.
		void RebuildDebugText();

		/// Return a short label for the given presence status.
		static const char* PresenceLabel(engine::server::PresenceStatus status);

		FriendsPanelState m_state{};
		uint32_t          m_viewportWidth  = 0;
		uint32_t          m_viewportHeight = 0;
		bool              m_initialized    = false;

		/// Display name of the last-clicked friend (for double-click detection).
		std::string m_lastClickedName;
		/// Timestamp (in seconds, from Tick accumulation) of the last click.
		float       m_lastClickTimeSec     = -1.0f;
		/// Accumulated game time used for double-click threshold comparison.
		float       m_elapsedSec           = 0.0f;
	};
}
