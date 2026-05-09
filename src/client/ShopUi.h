#pragma once

#include "engine/client/InventoryUi.h"
#include "engine/client/UIModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Hit-test region for one vendor offer row (M35.2).
	struct ShopOfferRegion
	{
		uint32_t offerIndex = 0;
		InventoryRect bounds{};
	};

	/// Resolved shop panel state for HUD / debug overlay (M35.2).
	struct ShopPanelState
	{
		std::string debugText;
		bool layoutValid = false;
		/// Offer rows aligned with \ref UIModel::shop.offers indices.
		std::vector<ShopOfferRegion> offerRegions;
		/// Area where a dragged inventory stack completes a sell-back (server confirms price).
		InventoryRect sellDropZone{};
	};

	/// Vendor shop presenter: grid of offers, buy targets, sell drop zone (M35.2).
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

		bool Tick(float deltaSeconds);

		const ShopPanelState& GetState() const { return m_state; }

		/// Hit test for offer row index (0-based). Returns -1 when no hit.
		int HitTestOfferLine(float mouseX, float mouseY) const;

		/// True when cursor is over the sell-back drop strip.
		bool HitSellDropZone(float mouseX, float mouseY) const;

	private:
		void RebuildDebugText(const UIModel& model);
		void RebuildInteractionLayout(const UIModel& model);

		ShopPanelState m_state{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		bool m_initialized = false;
	};
}
