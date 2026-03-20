#pragma once

#include "engine/client/UIModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle used by the HUD presenter after responsive layout resolution.
	struct HudRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	/// One HUD bar widget showing a labeled current/max value pair.
	struct HudBarWidget
	{
		HudRect bounds{};
		std::string label;
		uint32_t currentValue = 0;
		uint32_t maxValue = 0;
		bool visible = false;
	};

	/// One HUD combat log line built from the latest retained combat events.
	struct HudCombatLogLine
	{
		std::string text;
		uint32_t damage = 0;
		bool incoming = false;
	};

	/// One HUD cooldown widget updated every frame by the presenter.
	struct HudCooldownWidget
	{
		HudRect bounds{};
		std::string actionId;
		float durationSeconds = 0.0f;
		float remainingSeconds = 0.0f;
		bool active = false;
	};

	/// Fully resolved MVP combat HUD state ready for a UI layer to render.
	struct CombatHudState
	{
		HudRect panelBounds{};
		HudBarWidget playerHealthBar{};
		HudBarWidget playerManaBar{};
		uint32_t playerComboPoints = 0;
		uint32_t playerMaxComboPoints = 0;
		bool playerHasCombo = false;
		HudRect targetFrameBounds{};
		HudBarWidget targetHealthBar{};
		bool targetVisible = false;
		std::string targetLabel;
		HudRect combatLogBounds{};
		std::vector<HudCombatLogLine> combatLogLines;
		std::vector<HudCooldownWidget> cooldowns;
		std::string debugText;
		bool layoutValid = false;
	};

	/// Builds a responsive combat HUD state from the UI model without owning rendering code.
	class CombatHudPresenter final
	{
	public:
		/// Construct an uninitialized HUD presenter.
		CombatHudPresenter() = default;

		/// Release HUD presenter resources.
		~CombatHudPresenter();

		/// Initialize the presenter and allocate the MVP cooldown widgets.
		bool Init();

		/// Shutdown the presenter and release cached HUD state.
		void Shutdown();

		/// Update the viewport-dependent widget layout in pixels.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild the visible HUD state.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Advance transient HUD timers such as cooldowns.
		bool Tick(float deltaSeconds);

		/// Return the current immutable HUD state snapshot.
		const CombatHudState& GetState() const { return m_state; }

	private:
		/// Recompute widget rectangles after a viewport change.
		void RebuildLayout();

		/// Copy player bar values from the shared UI model.
		void UpdatePlayerBars(const UIModel& model);

		/// Copy target frame values from the shared UI model.
		void UpdateTargetFrame(const UIModel& model);

		/// Copy the recent combat log lines from the shared UI model.
		void UpdateCombatLog(const UIModel& model);

		/// Start or refresh the MVP auto-attack cooldown from new combat entries.
		void RefreshCooldowns(const UIModel& model);

		/// Rebuild the human-readable HUD dump.
		void RebuildDebugText();

		CombatHudState m_state{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		uint64_t m_lastCombatSequence = 0;
		bool m_initialized = false;
	};
}
