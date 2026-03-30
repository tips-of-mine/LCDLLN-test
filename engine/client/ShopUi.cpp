#include "engine/client/ShopUi.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace engine::client
{
	namespace
	{
		/// Trim leading/trailing spaces and tabs from a text view.
		std::string_view Trim(std::string_view value)
		{
			size_t begin = 0;
			size_t end   = value.size();
			while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
			{
				++begin;
			}
			while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}

		/// Split a line into at most four `|`-separated columns.
		std::array<std::string_view, 4> SplitColumns(std::string_view line, size_t& outCount)
		{
			std::array<std::string_view, 4> cols{};
			outCount = 0;
			size_t start = 0;
			while (start <= line.size() && outCount < cols.size())
			{
				const size_t sep = line.find('|', start);
				if (sep == std::string_view::npos)
				{
					cols[outCount++] = line.substr(start);
					break;
				}
				cols[outCount++] = line.substr(start, sep - start);
				start = sep + 1;
			}
			return cols;
		}
	}

	ShopUiPresenter::~ShopUiPresenter()
	{
		Shutdown();
	}

	bool ShopUiPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ShopUiPresenter] Init ignored: already initialized");
			return true;
		}

		// Reuse the same inventory metadata file so item names/icons are shared.
		m_metadataRelPath = config.GetString("ui.inventory_items_path", "ui/inventory_items.txt");
		if (!LoadMetadata(config))
		{
			LOG_ERROR(Core, "[ShopUiPresenter] Init FAILED: metadata load failed ({})", m_metadataRelPath);
			return false;
		}

		m_initialized = true;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[ShopUiPresenter] Init OK (meta_entries={}, path={})", m_metadata.size(), m_metadataRelPath);
		return true;
	}

	void ShopUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized    = false;
		m_state          = {};
		m_metadata.clear();
		m_viewportWidth  = 0;
		m_viewportHeight = 0;
		m_metadataRelPath.clear();
		LOG_INFO(Core, "[ShopUiPresenter] Destroyed");
	}

	bool ShopUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ShopUiPresenter] SetViewportSize FAILED: not initialized");
			return false;
		}
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[ShopUiPresenter] SetViewportSize FAILED: invalid size {}x{}", width, height);
			return false;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[ShopUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	bool ShopUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ShopUiPresenter] ApplyModel FAILED: not initialized");
			return false;
		}

		if ((changeMask & UIModelChangeShop) == 0u && (changeMask & UIModelChangeWallet) == 0u)
		{
			return true;
		}

		if (!m_state.layoutValid)
		{
			RebuildLayout();
		}

		RebuildItems(model);
		RebuildDebugText();
		LOG_DEBUG(Core, "[ShopUiPresenter] Model applied (vendor={}, items={}, gold={}, open={})",
			model.shop.vendorId,
			model.shop.items.size(),
			model.wallet.gold,
			model.shop.isOpen ? "true" : "false");
		return true;
	}

	bool ShopUiPresenter::SelectItem(uint32_t itemId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ShopUiPresenter] SelectItem FAILED: not initialized");
			return false;
		}
		m_state.selectedItemId = itemId;
		for (ShopItemView& view : m_state.items)
		{
			view.selected = (view.itemId == itemId);
		}
		RebuildDebugText();
		LOG_DEBUG(Core, "[ShopUiPresenter] SelectItem (item_id={})", itemId);
		return true;
	}

	bool ShopUiPresenter::LoadMetadata(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, m_metadataRelPath);
		if (text.empty())
		{
			LOG_WARN(Core, "[ShopUiPresenter] Metadata missing or empty ({}); shop will show item ids only", m_metadataRelPath);
			// Not a hard failure — shop is still functional without display names.
			return true;
		}

		m_metadata.clear();
		size_t lineStart = 0;
		while (lineStart <= text.size())
		{
			const size_t lineEnd    = text.find('\n', lineStart);
			const std::string_view rawLine = (lineEnd == std::string::npos)
				? std::string_view(text).substr(lineStart)
				: std::string_view(text).substr(lineStart, lineEnd - lineStart);
			const std::string_view line = Trim(rawLine);
			if (!line.empty() && !line.starts_with('#'))
			{
				uint32_t    itemId = 0;
				std::string icon;
				std::string name;
				if (ParseMetadataLine(line, itemId, icon, name))
				{
					ItemMetaEntry entry{};
					entry.itemId      = itemId;
					entry.iconPath    = std::move(icon);
					entry.displayName = std::move(name);
					m_metadata.push_back(std::move(entry));
				}
			}
			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}

		LOG_INFO(Core, "[ShopUiPresenter] Metadata loaded (entries={}, path={})", m_metadata.size(), m_metadataRelPath);
		return true;
	}

	bool ShopUiPresenter::ParseMetadataLine(std::string_view line, uint32_t& outItemId, std::string& outIcon, std::string& outName) const
	{
		size_t colCount = 0;
		const auto cols = SplitColumns(line, colCount);
		if (colCount < 3)
		{
			return false;
		}
		const std::string_view idView = Trim(cols[0]);
		if (idView.empty())
		{
			return false;
		}
		uint32_t id = 0;
		for (const char ch : idView)
		{
			if (ch < '0' || ch > '9')
			{
				return false;
			}
			id = (id * 10u) + static_cast<uint32_t>(ch - '0');
		}
		outItemId = id;
		outIcon   = std::string(Trim(cols[1]));
		outName   = std::string(Trim(cols[2]));
		return !outName.empty();
	}

	void ShopUiPresenter::RebuildLayout()
	{
		const float vw = static_cast<float>(m_viewportWidth  == 0 ? 1280u : m_viewportWidth);
		const float vh = static_cast<float>(m_viewportHeight == 0 ? 720u  : m_viewportHeight);

		// Centre of screen, slightly left of centre.
		const float panelWidth  = std::clamp(vw * 0.40f, 380.0f, 560.0f);
		const float panelHeight = std::clamp(vh * 0.60f, 360.0f, 520.0f);
		const float panelX      = (vw - panelWidth) * 0.5f;
		const float panelY      = (vh - panelHeight) * 0.5f;

		m_state.panelBounds = { panelX, panelY, panelWidth, panelHeight };
		m_state.layoutValid = true;
	}

	void ShopUiPresenter::RebuildItems(const UIModel& model)
	{
		m_state.visible    = model.shop.isOpen;
		m_state.vendorId   = model.shop.vendorId;
		m_state.playerGold = model.wallet.gold;

		m_state.items.clear();
		m_state.items.reserve(model.shop.items.size());

		for (const UIShopItemEntry& entry : model.shop.items)
		{
			ShopItemView view{};
			view.itemId      = entry.itemId;
			view.buyPrice    = entry.buyPrice;
			view.sellPrice   = entry.sellPrice;
			view.stock       = entry.stock;
			view.displayName = FindDisplayName(entry.itemId);
			view.iconPath    = FindIconPath(entry.itemId);
			view.selected    = (entry.itemId == m_state.selectedItemId);
			view.canAfford   = (model.wallet.gold >= entry.buyPrice);
			m_state.items.push_back(std::move(view));
		}
	}

	void ShopUiPresenter::RebuildDebugText()
	{
		m_state.debugText.clear();
		m_state.debugText += "[ShopUi]\n";
		m_state.debugText += "vendor=";
		m_state.debugText += m_state.vendorId.empty() ? "(none)" : m_state.vendorId;
		m_state.debugText += " open=";
		m_state.debugText += m_state.visible ? "true" : "false";
		m_state.debugText += " gold=";
		m_state.debugText += std::to_string(m_state.playerGold);
		m_state.debugText += " items=";
		m_state.debugText += std::to_string(m_state.items.size());
		m_state.debugText += "\n";
		for (const ShopItemView& item : m_state.items)
		{
			m_state.debugText += " item_id=";
			m_state.debugText += std::to_string(item.itemId);
			m_state.debugText += " buy=";
			m_state.debugText += std::to_string(item.buyPrice);
			m_state.debugText += " sell=";
			m_state.debugText += std::to_string(item.sellPrice);
			m_state.debugText += " stock=";
			m_state.debugText += (item.stock == -1) ? "inf" : std::to_string(item.stock);
			m_state.debugText += " name=";
			m_state.debugText += item.displayName.empty() ? "?" : item.displayName;
			m_state.debugText += "\n";
		}
	}

	std::string ShopUiPresenter::FindDisplayName(uint32_t itemId) const
	{
		for (const ItemMetaEntry& entry : m_metadata)
		{
			if (entry.itemId == itemId)
			{
				return entry.displayName;
			}
		}
		return "Item " + std::to_string(itemId);
	}

	std::string ShopUiPresenter::FindIconPath(uint32_t itemId) const
	{
		for (const ItemMetaEntry& entry : m_metadata)
		{
			if (entry.itemId == itemId)
			{
				return entry.iconPath;
			}
		}
		return {};
	}
}
