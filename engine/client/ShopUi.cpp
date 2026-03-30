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
