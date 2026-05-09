#pragma once

#include "src/client/UIModel.h"

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Resolved layout for the harvest progress cast bar (M36.1).
	struct HarvestCastBarState
	{
		/// Pixel-space position and dimensions of the bar background.
		float barX      = 0.0f;
		float barY      = 0.0f;
		float barWidth  = 0.0f;
		float barHeight = 0.0f;
		/// Fill fraction in [0, 1]: 0 = just started, 1 = complete.
		float fillFraction = 0.0f;
		/// Human-readable label displayed above the bar (e.g. "Harvesting…").
		std::string label;
		/// True while a harvest is in progress.
		bool visible = false;
	};

	/// Builds the harvest cast bar layout from the UIModel harvest state (M36.1).
	///
	/// Driven by UIModelChangeHarvest in the change mask.
	class HarvestCastBarPresenter final
	{
	public:
		HarvestCastBarPresenter() = default;
		~HarvestCastBarPresenter();

		HarvestCastBarPresenter(const HarvestCastBarPresenter&) = delete;
		HarvestCastBarPresenter& operator=(const HarvestCastBarPresenter&) = delete;

		/// Initialize the presenter.
		bool Init();

		/// Release resources.
		void Shutdown();

		/// Update the viewport-dependent bar position.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Rebuild bar state from the latest UI model.
		/// @param changeMask  Must include UIModelChangeHarvest for a rebuild to occur.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Advance the bar fill fraction by elapsed game time.
		/// @param deltaSeconds  Seconds since the last frame.
		bool Tick(float deltaSeconds);

		/// Return the immutable bar state for rendering.
		const HarvestCastBarState& GetState() const { return m_state; }

	private:
		/// Recompute pixel-space bounds after a viewport change.
		void RebuildLayout();

		HarvestCastBarState m_state{};
		uint32_t            m_viewportWidth  = 0;
		uint32_t            m_viewportHeight = 0;
		bool                m_initialized    = false;
	};
}
