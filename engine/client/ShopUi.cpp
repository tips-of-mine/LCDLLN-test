#include "engine/client/ShopUi.h"



#include "engine/core/Log.h"

#include "engine/server/ServerProtocol.h"



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



		/// Mirror \ref engine::server::VendorCatalog::ComputeSellPrice for client-side display only.

		uint32_t LocalSellPrice(uint32_t buyPrice)

		{

			if (buyPrice == 0u)

			{

				return 0u;

			}

			const uint64_t sp = (static_cast<uint64_t>(buyPrice) * 25ull) / 100ull;

			const uint32_t out = static_cast<uint32_t>(std::min<uint64_t>(sp, static_cast<uint64_t>(0xFFFFFFFFu)));

			return out > 0u ? out : 1u;

		}

	}



	ShopUiPresenter::~ShopUiPresenter()

	{

		Shutdown();

	}



	bool ShopUiPresenter::Init()

	{

		if (m_initialized)

		{

			LOG_WARN(Core, "[ShopUiPresenter] Init ignored: already initialized");

			return true;

		}



		m_initialized = true;

		LOG_INFO(Core, "[ShopUiPresenter] Init OK");

		return true;

	}



	void ShopUiPresenter::Shutdown()

	{

		if (!m_initialized)

		{

			return;

		}



		m_initialized = false;

		m_state = {};

		m_viewportWidth = 0;

		m_viewportHeight = 0;

		LOG_INFO(Core, "[ShopUiPresenter] Destroyed");

	}



	bool ShopUiPresenter::SetViewportSize(uint32_t width, uint32_t height)

	{

		if (!m_initialized)

		{

			LOG_ERROR(Core, "[ShopUiPresenter] SetViewportSize FAILED: presenter not initialized");

			return false;

		}



		if (width == 0u || height == 0u)

		{

			LOG_WARN(Core, "[ShopUiPresenter] SetViewportSize FAILED: invalid viewport {}x{}", width, height);

			return false;

		}



		m_viewportWidth = width;

		m_viewportHeight = height;

		m_state.layoutValid = true;

		LOG_INFO(Core, "[ShopUiPresenter] Viewport updated ({}x{})", width, height);

		return true;

	}



	bool ShopUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)

	{

		if (!m_initialized)

		{

			LOG_ERROR(Core, "[ShopUiPresenter] ApplyModel FAILED: presenter not initialized");

			return false;

		}



		if (!m_state.layoutValid)

		{

			LOG_WARN(Core, "[ShopUiPresenter] ApplyModel using fallback layout: viewport not set");

		}



		if ((changeMask & UIModelChangeShop) == 0u)

		{

			return true;

		}



		RebuildInteractionLayout(model);

		RebuildDebugText(model);

		LOG_DEBUG(Core, "[ShopUiPresenter] Model applied (offers={})", model.shop.offers.size());

		return true;

	}



	bool ShopUiPresenter::Tick(float deltaSeconds)

	{

		if (!m_initialized)

		{

			LOG_ERROR(Core, "[ShopUiPresenter] Tick FAILED: presenter not initialized");

			return false;

		}



		if (deltaSeconds < 0.0f)

		{

			LOG_WARN(Core, "[ShopUiPresenter] Tick FAILED: negative delta {}", deltaSeconds);

			return false;

		}



		(void)deltaSeconds;

		return true;

	}



	int ShopUiPresenter::HitTestOfferLine(float mouseX, float mouseY) const

	{

		for (const ShopOfferRegion& region : m_state.offerRegions)

		{

			if (PointInRect(mouseX, mouseY, region.bounds))

			{

				return static_cast<int>(region.offerIndex);

			}

		}

		return -1;

	}



	bool ShopUiPresenter::HitSellDropZone(float mouseX, float mouseY) const

	{

		return PointInRect(mouseX, mouseY, m_state.sellDropZone);

	}



	void ShopUiPresenter::RebuildInteractionLayout(const UIModel& model)

	{

		m_state.offerRegions.clear();

		m_state.sellDropZone = {};



		if (!model.shop.isOpen || model.shop.offers.empty())

		{

			return;

		}



		const float vw = static_cast<float>(m_viewportWidth == 0u ? 1280u : m_viewportWidth);

		const float vh = static_cast<float>(m_viewportHeight == 0u ? 720u : m_viewportHeight);

		const float margin = std::max(20.0f, vw * 0.025f);

		const float panelWidth = std::clamp(vw * 0.28f, 280.0f, 420.0f);

		const float innerPad = 12.0f;

		const float headerH = 32.0f;

		const float rowH = 26.0f;

		const float sellH = 56.0f;



		const size_t offerCount = model.shop.offers.size();

		const float bodyH = headerH + (static_cast<float>(offerCount) * rowH) + sellH + innerPad;

		const float panelHeight = std::min(bodyH, std::max(160.0f, vh * 0.55f));

		const float panelX = margin;

		const float panelY = vh - margin - panelHeight;



		const float innerX = panelX + innerPad;

		const float innerW = panelWidth - (innerPad * 2.0f);



		for (size_t i = 0; i < offerCount; ++i)

		{

			const float yRow = panelY + innerPad + headerH + (static_cast<float>(i) * rowH);

			if (yRow + rowH > panelY + panelHeight - sellH - innerPad)

			{

				LOG_WARN(Core, "[ShopUiPresenter] Offer row {} clipped by panel height", i);

				break;

			}

			ShopOfferRegion region{};

			region.offerIndex = static_cast<uint32_t>(i);

			region.bounds = { innerX, yRow, innerW, rowH };

			m_state.offerRegions.push_back(region);

		}



		const float sellY = panelY + panelHeight - sellH - innerPad;

		m_state.sellDropZone = { innerX, sellY, innerW, sellH };

		LOG_DEBUG(Core,

			"[ShopUiPresenter] Interaction layout (offers_regions={}, sell_zone=({:.0f},{:.0f},{:.0f}x{:.0f}))",

			m_state.offerRegions.size(),

			m_state.sellDropZone.x,

			m_state.sellDropZone.y,

			m_state.sellDropZone.width,

			m_state.sellDropZone.height);

	}



	void ShopUiPresenter::RebuildDebugText(const UIModel& model)

	{

		m_state.debugText.clear();

		m_state.debugText += "[ShopUi]\n";

		m_state.debugText += "viewport=";

		m_state.debugText += std::to_string(m_viewportWidth);

		m_state.debugText += "x";

		m_state.debugText += std::to_string(m_viewportHeight);

		m_state.debugText += " layout=";

		m_state.debugText += m_state.layoutValid ? "true" : "false";

		m_state.debugText += "\n";



		if (!model.shop.isOpen)

		{

			m_state.debugText += "shop closed\n";

			return;

		}



		m_state.debugText += "vendor_id=";

		m_state.debugText += std::to_string(model.shop.vendorId);

		m_state.debugText += " ";

		m_state.debugText += model.shop.displayName;

		m_state.debugText += "\n";

		m_state.debugText += "Click row or keys 1-9 to buy qty=1. Right-drag inv stack to SELL zone (server 25% buy). Y/N confirm.\n";



		for (const UIShopOfferLine& line : model.shop.offers)

		{

			m_state.debugText += "item ";

			m_state.debugText += std::to_string(line.itemId);

			m_state.debugText += " buy ";

			m_state.debugText += std::to_string(line.buyPrice);

			m_state.debugText += " stock ";

			if (line.stock == engine::server::kShopInfiniteStockWire)

			{

				m_state.debugText += "inf";

			}

			else

			{

				m_state.debugText += std::to_string(line.stock);

			}

			m_state.debugText += " sell ";

			m_state.debugText += std::to_string(LocalSellPrice(line.buyPrice));

			m_state.debugText += "\n";

		}

	}

}

