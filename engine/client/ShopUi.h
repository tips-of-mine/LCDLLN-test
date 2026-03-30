#pragma once

#include "engine/client/UIModel.h"

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Resolved shop panel state for debug overlay / future HUD renderer (M35.2).
	struct ShopPanelState
	{
		std::string debugText;
		bool layoutValid = false;
	};

	/// Builds vendor shop debug text from the shared UI model (M35.2).
	class ShopUiPresenter final
	{
	public:
		ShopUiPresenter() = default;

		~ShopUiPresenter();

		/// Initialize presenter state.
		bool Init();

		/// Release presenter allocations.
		void Shutdown();

		/// Update layout when the viewport size changes.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot; rebuilds when \p changeMask includes \ref UIModelChangeShop.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Reserved for future hover / buy-button animation timing.
		bool Tick(float deltaSeconds);

		const ShopPanelState& GetState() const { return m_state; }

	private:
		void RebuildDebugText(const UIModel& model);

		ShopPanelState m_state{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		bool m_initialized = false;
	};
}
