#include "engine/client/AuctionUi.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace engine::client
{
	namespace
	{
		bool PointInRect(float x, float y, const InventoryRect& r)
		{
			return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
		}
	}

	AuctionUiPresenter::~AuctionUiPresenter()
	{
		Shutdown();
	}

	bool AuctionUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuctionUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		LOG_INFO(Core, "[AuctionUiPresenter] Init OK");
		return true;
	}

	void AuctionUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized = false;
		m_state = {};
		m_viewportWidth = 0;
		m_viewportHeight = 0;
		LOG_INFO(Core, "[AuctionUiPresenter] Destroyed");
	}

	bool AuctionUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionUiPresenter] SetViewportSize FAILED: not initialized");
			return false;
		}
		if (width == 0u || height == 0u)
		{
			LOG_WARN(Core, "[AuctionUiPresenter] SetViewportSize FAILED: invalid size {}x{}", width, height);
			return false;
		}
		m_viewportWidth = width;
		m_viewportHeight = height;
		LOG_INFO(Core, "[AuctionUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	bool AuctionUiPresenter::Tick(float deltaSeconds)
	{
		(void)deltaSeconds;
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionUiPresenter] Tick FAILED: not initialized");
			return false;
		}
		return true;
	}

	bool AuctionUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AuctionUiPresenter] ApplyModel FAILED: not initialized");
			return false;
		}
		if ((changeMask & UIModelChangeAuction) == 0u)
		{
			return true;
		}
		RebuildInteractionLayout(model);
		RebuildDebugText(model);
		m_state.layoutValid = m_viewportWidth > 0u && m_viewportHeight > 0u;
		LOG_DEBUG(Core, "[AuctionUiPresenter] Model applied (rows={})", model.auction.listings.size());
		return true;
	}

	int AuctionUiPresenter::HitTestRow(float mouseX, float mouseY) const
	{
		for (const AuctionRowRegion& region : m_state.rowRegions)
		{
			if (PointInRect(mouseX, mouseY, region.bounds))
			{
				return static_cast<int>(region.rowIndex);
			}
		}
		return -1;
	}

	bool AuctionUiPresenter::HitPostDropZone(float mouseX, float mouseY) const
	{
		return PointInRect(mouseX, mouseY, m_state.postDropZone);
	}

	void AuctionUiPresenter::RebuildInteractionLayout(const UIModel& model)
	{
		m_state.rowRegions.clear();
		m_state.postDropZone = {};

		if (!model.auction.isOpen || model.auction.listings.empty())
		{
			return;
		}

		const float vw = static_cast<float>(m_viewportWidth == 0u ? 1280u : m_viewportWidth);
		const float vh = static_cast<float>(m_viewportHeight == 0u ? 720u : m_viewportHeight);
		const float margin = std::max(20.0f, vw * 0.025f);
		const float panelWidth = std::clamp(vw * 0.36f, 320.0f, 520.0f);
		const float innerPad = 12.0f;
		const float headerH = 56.0f;
		const float rowH = 24.0f;
		const float postH = 48.0f;
		const size_t rowCount = model.auction.listings.size();
		const float bodyH = headerH + (static_cast<float>(rowCount) * rowH) + postH + innerPad;
		const float panelHeight = std::min(bodyH, std::max(200.0f, vh * 0.62f));
		const float panelX = vw - margin - panelWidth;
		const float panelY = vh - margin - panelHeight;
		const float innerX = panelX + innerPad;
		const float innerW = panelWidth - (innerPad * 2.0f);

		for (size_t i = 0; i < rowCount; ++i)
		{
			const float yRow = panelY + innerPad + headerH + (static_cast<float>(i) * rowH);
			if (yRow + rowH > panelY + panelHeight - postH - innerPad)
			{
				LOG_WARN(Core, "[AuctionUiPresenter] Row {} clipped by panel height", i);
				break;
			}
			AuctionRowRegion region{};
			region.rowIndex = static_cast<uint32_t>(i);
			region.bounds = { innerX, yRow, innerW, rowH };
			m_state.rowRegions.push_back(region);
		}

		const float postY = panelY + panelHeight - postH - innerPad;
		m_state.postDropZone = { innerX, postY, innerW, postH };
		LOG_DEBUG(Core,
			"[AuctionUiPresenter] Layout (rows={}, post_zone=({:.0f},{:.0f}))",
			m_state.rowRegions.size(),
			m_state.postDropZone.x,
			m_state.postDropZone.y);
	}

	void AuctionUiPresenter::RebuildDebugText(const UIModel& model)
	{
		m_state.debugText.clear();
		m_state.debugText += "[AuctionHouse M35.4]\n";
		if (!model.auction.isOpen)
		{
			m_state.debugText += " Closed. H=open  F5=refresh filters\n";
			return;
		}
		m_state.debugText += " sort=";
		m_state.debugText += std::to_string(model.auction.sortMode);
		m_state.debugText += " minP=";
		m_state.debugText += std::to_string(model.auction.filterMinPrice);
		m_state.debugText += " maxP=";
		m_state.debugText += std::to_string(model.auction.filterMaxPrice);
		m_state.debugText += " itemF=";
		m_state.debugText += std::to_string(model.auction.filterItemId);
		m_state.debugText += " sel=";
		m_state.debugText += std::to_string(model.auction.selectedRow);
		m_state.debugText += "\n F=sort  Q/E=min  PgUp/PgDn=max(0=off)  M=toggle item#1 filter  G=refresh\n";
		m_state.debugText += " Click row / 1-9 select  B=bid  O=buyout  P=list hovered stack\n";
		for (size_t i = 0; i < model.auction.listings.size(); ++i)
		{
			const UIAuctionListingLine& L = model.auction.listings[i];
			m_state.debugText += " #";
			m_state.debugText += std::to_string(i + 1);
			m_state.debugText += " id=";
			m_state.debugText += std::to_string(L.listingId);
			m_state.debugText += " itm=";
			m_state.debugText += std::to_string(L.itemId);
			m_state.debugText += " x";
			m_state.debugText += std::to_string(L.quantity);
			m_state.debugText += " sb=";
			m_state.debugText += std::to_string(L.startBid);
			m_state.debugText += " cur=";
			m_state.debugText += std::to_string(L.currentBid);
			m_state.debugText += " bo=";
			m_state.debugText += std::to_string(L.buyoutPrice);
			m_state.debugText += " expT=";
			m_state.debugText += std::to_string(L.expiresAtTick);
			m_state.debugText += "\n";
		}
	}
}
