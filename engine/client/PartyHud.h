#pragma once
// M32.2 — Party frame HUD presenter: up to 5 member HP/mana bars + class icons.

#include "engine/client/UIModel.h"

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Maximum number of party member frames displayed by the party HUD.
	inline constexpr size_t kMaxPartyFrames = 5;

	/// Pixel-space data for one rendered party member frame (M32.2).
	struct PartyMemberFrame
	{
		/// Screen-space bounds of the entire frame widget.
		HudRect frameBounds{};
		/// HP bar widget.
		HudBarWidget hpBar{};
		/// Mana bar widget.
		HudBarWidget manaBar{};
		/// Display name label (e.g. "P12", or localised character name).
		std::string  displayName;
		/// True when this member is the current party leader.
		bool         isLeader = false;
		/// True when this slot is occupied by a real member.
		bool         visible  = false;
	};

	/// Resolved party HUD state ready for a UI renderer (M32.2).
	struct PartyHudState
	{
		/// Up to kMaxPartyFrames member frames.  Slot 0 is always the local player.
		PartyMemberFrame frames[kMaxPartyFrames]{};
		/// Human-readable loot mode label (e.g. "FreeForAll").
		std::string      lootModeLabel;
		/// Number of currently visible frames.
		uint8_t          visibleCount  = 0;
		/// True when the local player is in a party.
		bool             inParty       = false;
		/// True when SetViewportSize has been called at least once.
		bool             layoutValid   = false;
	};

	/// Builds a responsive party HUD state from the shared UI model without owning
	/// rendering code.  Follows the same pattern as CombatHudPresenter (M16.2).
	class PartyHudPresenter final
	{
	public:
		/// Construct an uninitialized party HUD presenter.
		PartyHudPresenter() = default;

		/// Release party HUD resources.
		~PartyHudPresenter();

		/// Initialize the presenter.
		/// Emits LOG_INFO on success or LOG_WARN on repeated initialisation.
		bool Init();

		/// Shut down the presenter and release cached state.
		/// Emits LOG_INFO on completion.
		void Shutdown();

		/// Update the viewport-dependent layout in pixels.
		/// Must be called whenever the window resizes.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild the visible party frames.
		/// \p changeMask should include UIModelChangeParty to trigger a rebuild;
		/// other change bits are ignored.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Return the current immutable party HUD state snapshot.
		const PartyHudState& GetState() const { return m_state; }

	private:
		/// Pixel height of one party member frame.
		static constexpr float kFrameHeight  = 48.0f;
		/// Pixel width of one party member frame.
		static constexpr float kFrameWidth   = 180.0f;
		/// Horizontal padding from the left edge of the viewport.
		static constexpr float kFramePadding = 8.0f;
		/// Vertical gap between successive member frames.
		static constexpr float kFrameGap     = 4.0f;
		/// Vertical offset from the top of the viewport for the first frame.
		static constexpr float kFrameTopOffset = 64.0f;

		/// Recompute widget rectangles after a viewport change.
		void RebuildLayout();

		/// Populate one member frame from UI model party data.
		void BuildFrame(size_t slot, const UIPartyMemberEntry& entry, bool isLeader);

		PartyHudState m_state{};
		uint32_t m_viewportWidth  = 0;
		uint32_t m_viewportHeight = 0;
		bool     m_initialized    = false;
	};
}
