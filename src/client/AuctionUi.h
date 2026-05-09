#pragma once

#include "engine/client/InventoryUi.h"
#include "engine/client/UIModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	struct AuctionRowRegion
	{
		uint32_t rowIndex = 0;
		InventoryRect bounds{};
	};

	struct AuctionPanelState
	{
		std::string debugText;
		bool layoutValid = false;
		std::vector<AuctionRowRegion> rowRegions;
		InventoryRect postDropZone{};
	};

	/// M35.4 — Auction house debug HUD: browse rows, filters, post listing drop zone.
	class AuctionUiPresenter final
	{
	public:
		AuctionUiPresenter() = default;
		~AuctionUiPresenter();

		AuctionUiPresenter(const AuctionUiPresenter&) = delete;
		AuctionUiPresenter& operator=(const AuctionUiPresenter&) = delete;

		bool Init();
		void Shutdown();
		bool SetViewportSize(uint32_t width, uint32_t height);
		bool ApplyModel(const UIModel& model, uint32_t changeMask);
		bool Tick(float deltaSeconds);

		const AuctionPanelState& GetState() const { return m_state; }

		int HitTestRow(float mouseX, float mouseY) const;
		bool HitPostDropZone(float mouseX, float mouseY) const;

	private:
		void RebuildDebugText(const UIModel& model);
		void RebuildInteractionLayout(const UIModel& model);

		AuctionPanelState m_state{};
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		bool m_initialized = false;
	};
}
