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

	/// One entity blip on the HUD minimap (design system — HudOverlay).
	struct HudMinimapBlip
	{
		float   xPct = 0.5f; ///< Normalised horizontal position [0, 1]; 0.5 = centre.
		float   yPct = 0.5f; ///< Normalised vertical position [0, 1].
		uint8_t kind = 0;    ///< 0=hostile, 1=friendly/NPC, 2=quest marker.
	};

	/// One keyed action slot in the bottom-centre action bar (10 slots, keys 1-0).
	struct HudActionSlot
	{
		HudRect     bounds{};
		uint8_t     slotIndex   = 0;   ///< 0-9 → keys 1,2,…,9,0.
		std::string actionId;          ///< Bound action id; empty = unbound.
		float       cooldownPct = 0.f; ///< [0, 1]; 0 = ready.
		bool        hasAbility  = false;
		bool        active      = false; ///< Slot is currently active (pressed / triggered).
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
		/// M35.1 — top-right wallet strip (gold icon label + amount).
		HudRect walletBounds{};
		std::string walletGoldLine;
		bool walletVisible = false;

		// ---- Design system — HudOverlay: player identity (driven by SetPlayerIdentity/SetPlayerXp) ----
		std::string playerName;        ///< Displayed as "Name · Niv. N".
		uint32_t    playerLevel   = 0;
		float       playerXpPct   = 0.f; ///< XP bar fill [0, 1].
		HudRect     portraitBounds{};    ///< 52×52 portrait thumbnail area inside panelBounds.
		HudRect     playerXpBarBounds{}; ///< Thin XP bar row below the mana bar.

		// ---- Design system — HudOverlay: minimap (top-right) ----
		std::string                  zoneName;
		std::string                  zoneDirection;
		HudRect                      minimapBounds{};
		std::vector<HudMinimapBlip>  minimapBlips;
		bool                         minimapVisible = false;

		// ---- Design system — HudOverlay: action bar (bottom-centre, 10 slots) ----
		HudRect                     actionBarBounds{};
		std::vector<HudActionSlot>  actionSlots;

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

		bool IsInitialized() const { return m_initialized; }

		// ---- Design system setters (driven externally, not from UIModel) ----

		/// Set the displayed player name and level (call after character creation succeeds).
		void SetPlayerIdentity(const std::string& name, uint32_t level);

		/// Set the XP fill fraction [0, 1] (call when the server sends an XP update).
		void SetPlayerXp(float xpPct);

		/// Set the minimap zone label and compass direction (call on zone change).
		void SetZoneInfo(const std::string& zoneName, const std::string& direction);

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

		/// Copy wallet label from the UI model (M35.1).
		void UpdateWalletFromModel(const UIModel& model);

		/// Populate minimapBlips from player and target positions.
		void UpdateMinimap(const UIModel& model);

		/// Compute action bar slot bounds (called from RebuildLayout).
		void RebuildActionBar();

		CombatHudState m_state{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		uint64_t m_lastCombatSequence = 0;
		bool m_initialized = false;
	};
}
