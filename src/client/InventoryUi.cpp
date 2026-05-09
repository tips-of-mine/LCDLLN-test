#include "engine/client/InventoryUi.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace engine::client
{
	namespace
	{
		inline constexpr uint32_t kInventoryColumns = 4;
		inline constexpr uint32_t kInventoryRows = 4;
		inline constexpr size_t kPickupFeedbackMaxEntries = 3;
		inline constexpr float kPickupFeedbackDurationSeconds = 2.0f;

		/// Trim spaces and tabs from both ends of one text view.
		std::string_view Trim(std::string_view value)
		{
			size_t begin = 0;
			size_t end = value.size();
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

		/// Split one line into at most four `|` separated columns.
		std::array<std::string_view, 4> SplitMetadataColumns(std::string_view line, size_t& outCount)
		{
			std::array<std::string_view, 4> columns{};
			outCount = 0;
			size_t start = 0;
			while (start <= line.size() && outCount < columns.size())
			{
				const size_t separator = line.find('|', start);
				if (separator == std::string_view::npos)
				{
					columns[outCount++] = line.substr(start);
					break;
				}

				columns[outCount++] = line.substr(start, separator - start);
				start = separator + 1;
			}
			return columns;
		}

		/// Return true when the point lies within the rectangle bounds.
		bool ContainsPoint(const InventoryRect& rect, float x, float y)
		{
			return x >= rect.x
				&& y >= rect.y
				&& x <= (rect.x + rect.width)
				&& y <= (rect.y + rect.height);
		}
	}

	InventoryUiPresenter::~InventoryUiPresenter()
	{
		Shutdown();
	}

	bool InventoryUiPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] Init ignored: already initialized");
			return true;
		}

		m_relativeMetadataPath = config.GetString("ui.inventory_items_path", "ui/inventory_items.txt");
		if (!LoadMetadata(config))
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] Init FAILED: metadata load failed ({})", m_relativeMetadataPath);
			return false;
		}

		m_state.columns = kInventoryColumns;
		m_state.rows = kInventoryRows;
		m_state.slots.resize(static_cast<size_t>(kInventoryColumns * kInventoryRows));
		m_initialized = true;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[InventoryUiPresenter] Init OK (metadata={}, path={})", m_metadata.size(), m_relativeMetadataPath);
		return true;
	}

	void InventoryUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_state = {};
		m_metadata.clear();
		m_previousInventory.clear();
		m_hoveredSlotIndex = UINT32_MAX;
		m_viewportWidth = 0;
		m_viewportHeight = 0;
		m_relativeMetadataPath.clear();
		m_dragActive = false;
		m_dragSlotIndex = 0;
		m_dragItemId = 0;
		m_dragQuantity = 0;
		LOG_INFO(Core, "[InventoryUiPresenter] Destroyed");
	}

	bool InventoryUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] SetViewportSize FAILED: presenter not initialized");
			return false;
		}

		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] SetViewportSize FAILED: invalid viewport {}x{}", width, height);
			return false;
		}

		m_viewportWidth = width;
		m_viewportHeight = height;
		RebuildLayout();
		RefreshTooltip();
		RebuildDebugText();
		LOG_INFO(Core, "[InventoryUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	bool InventoryUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] ApplyModel FAILED: presenter not initialized");
			return false;
		}

		if (!m_state.layoutValid)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] ApplyModel using fallback layout: viewport not set");
			RebuildLayout();
		}

		if ((changeMask & UIModelChangeInventory) != 0u)
		{
			RefreshPickupFeedback(model);
		}
		else if (m_previousInventory.empty() && !model.inventory.empty())
		{
			m_previousInventory = model.inventory;
		}
		RebuildSlots(model);
		SyncDragWithModel(model);
		RefreshTooltip();
		RebuildDebugText();
		LOG_DEBUG(Core, "[InventoryUiPresenter] Model applied (change_mask={}, items={}, feedback={})",
			changeMask,
			model.inventory.size(),
			m_state.pickupFeedback.size());
		return true;
	}

	bool InventoryUiPresenter::UpdateHover(float mouseX, float mouseY)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] UpdateHover FAILED: presenter not initialized");
			return false;
		}

		uint32_t hoveredIndex = UINT32_MAX;
		for (const InventorySlotState& slot : m_state.slots)
		{
			if (ContainsPoint(slot.bounds, mouseX, mouseY))
			{
				hoveredIndex = slot.slotIndex;
				break;
			}
		}

		m_hoveredSlotIndex = hoveredIndex;
		for (InventorySlotState& slot : m_state.slots)
		{
			slot.hovered = (slot.slotIndex == m_hoveredSlotIndex);
		}

		RefreshTooltip();
		RebuildDebugText();
		LOG_DEBUG(Core, "[InventoryUiPresenter] Hover updated (mouse=({:.1f}, {:.1f}), slot={})", mouseX, mouseY, m_hoveredSlotIndex);
		return true;
	}

	bool InventoryUiPresenter::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] Tick FAILED: presenter not initialized");
			return false;
		}

		if (deltaSeconds < 0.0f)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] Tick FAILED: negative delta {}", deltaSeconds);
			return false;
		}

		bool changed = false;
		for (InventoryPickupFeedback& feedback : m_state.pickupFeedback)
		{
			if (!feedback.active)
			{
				continue;
			}

			feedback.remainingSeconds = std::max(0.0f, feedback.remainingSeconds - deltaSeconds);
			if (feedback.remainingSeconds <= 0.0f)
			{
				feedback.active = false;
				LOG_INFO(Core, "[InventoryUiPresenter] Pickup feedback expired");
			}

			changed = true;
		}

		if (changed)
		{
			RebuildDebugText();
		}

		return true;
	}

	bool InventoryUiPresenter::LoadMetadata(const engine::core::Config& config)
	{
		const std::string metadataText = engine::platform::FileSystem::ReadAllTextContent(config, m_relativeMetadataPath);
		if (metadataText.empty())
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] Metadata load FAILED: empty or missing file {}", m_relativeMetadataPath);
			return false;
		}

		m_metadata.clear();
		size_t lineStart = 0;
		while (lineStart <= metadataText.size())
		{
			const size_t lineEnd = metadataText.find('\n', lineStart);
			const std::string_view rawLine = (lineEnd == std::string::npos)
				? std::string_view(metadataText).substr(lineStart)
				: std::string_view(metadataText).substr(lineStart, lineEnd - lineStart);
			const std::string_view line = Trim(rawLine);
			if (!line.empty() && !line.starts_with('#'))
			{
				InventoryItemMetadata metadata{};
				if (ParseMetadataLine(line, metadata))
				{
					m_metadata.push_back(std::move(metadata));
				}
				else
				{
					LOG_WARN(Core, "[InventoryUiPresenter] Metadata line ignored: {}", std::string(line));
				}
			}

			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}

		if (m_metadata.empty())
		{
			LOG_ERROR(Core, "[InventoryUiPresenter] Metadata load FAILED: no valid entries ({})", m_relativeMetadataPath);
			return false;
		}

		LOG_INFO(Core, "[InventoryUiPresenter] Metadata loaded (entries={}, path={})", m_metadata.size(), m_relativeMetadataPath);
		return true;
	}

	bool InventoryUiPresenter::ParseMetadataLine(std::string_view line, InventoryItemMetadata& outMetadata) const
	{
		size_t columnCount = 0;
		const std::array<std::string_view, 4> columns = SplitMetadataColumns(line, columnCount);
		if (columnCount != 4)
		{
			return false;
		}

		const std::string_view idView = Trim(columns[0]);
		if (idView.empty())
		{
			return false;
		}

		uint32_t itemId = 0;
		for (const char character : idView)
		{
			if (character < '0' || character > '9')
			{
				return false;
			}
			itemId = (itemId * 10u) + static_cast<uint32_t>(character - '0');
		}

		outMetadata.itemId = itemId;
		outMetadata.iconPath = std::string(Trim(columns[1]));
		outMetadata.displayName = std::string(Trim(columns[2]));
		outMetadata.description = std::string(Trim(columns[3]));
		return !outMetadata.displayName.empty();
	}

	void InventoryUiPresenter::RebuildLayout()
	{
		const float viewportWidth = static_cast<float>(m_viewportWidth == 0 ? 1280u : m_viewportWidth);
		const float viewportHeight = static_cast<float>(m_viewportHeight == 0 ? 720u : m_viewportHeight);
		const float margin = std::max(20.0f, viewportWidth * 0.025f);
		const float panelWidth = std::clamp(viewportWidth * 0.22f, 260.0f, 360.0f);
		const float panelHeight = std::clamp(viewportHeight * 0.34f, 260.0f, 360.0f);
		const float innerPadding = 16.0f;
		const float gridWidth = panelWidth - (innerPadding * 2.0f);
		const float gridHeight = panelHeight - (innerPadding * 2.0f);
		const float slotGap = 8.0f;
		const float slotWidth = (gridWidth - (slotGap * static_cast<float>(kInventoryColumns - 1))) / static_cast<float>(kInventoryColumns);
		const float slotHeight = (gridHeight - (slotGap * static_cast<float>(kInventoryRows - 1))) / static_cast<float>(kInventoryRows);

		m_state.panelBounds = { viewportWidth - margin - panelWidth, viewportHeight - margin - panelHeight, panelWidth, panelHeight };
		m_state.gridBounds = { m_state.panelBounds.x + innerPadding, m_state.panelBounds.y + innerPadding, gridWidth, gridHeight };

		if (m_state.slots.size() != static_cast<size_t>(kInventoryColumns * kInventoryRows))
		{
			m_state.slots.resize(static_cast<size_t>(kInventoryColumns * kInventoryRows));
		}

		for (uint32_t row = 0; row < kInventoryRows; ++row)
		{
			for (uint32_t column = 0; column < kInventoryColumns; ++column)
			{
				const uint32_t slotIndex = (row * kInventoryColumns) + column;
				InventorySlotState& slot = m_state.slots[slotIndex];
				slot.slotIndex = slotIndex;
				slot.bounds = {
					m_state.gridBounds.x + (static_cast<float>(column) * (slotWidth + slotGap)),
					m_state.gridBounds.y + (static_cast<float>(row) * (slotHeight + slotGap)),
					slotWidth,
					slotHeight
				};
			}
		}

		m_state.layoutValid = true;
	}

	void InventoryUiPresenter::RebuildSlots(const UIModel& model)
	{
		for (InventorySlotState& slot : m_state.slots)
		{
			slot.itemId = 0;
			slot.quantity = 0;
			slot.iconPath.clear();
			slot.label.clear();
			slot.occupied = false;
			slot.hovered = (slot.slotIndex == m_hoveredSlotIndex);
		}

		const size_t itemCount = std::min(model.inventory.size(), m_state.slots.size());
		for (size_t index = 0; index < itemCount; ++index)
		{
			const engine::server::ItemStack& item = model.inventory[index];
			InventorySlotState& slot = m_state.slots[index];
			const InventoryItemMetadata* metadata = FindMetadata(item.itemId);

			slot.itemId = item.itemId;
			slot.quantity = item.quantity;
			slot.occupied = true;
			slot.iconPath = metadata ? metadata->iconPath : "";
			slot.label = metadata
				? (metadata->displayName + " x" + std::to_string(item.quantity))
				: ("Item " + std::to_string(item.itemId) + " x" + std::to_string(item.quantity));
		}
	}

	void InventoryUiPresenter::RefreshPickupFeedback(const UIModel& model)
	{
		for (const engine::server::ItemStack& item : model.inventory)
		{
			uint32_t previousQuantity = 0;
			for (const engine::server::ItemStack& previous : m_previousInventory)
			{
				if (previous.itemId == item.itemId)
				{
					previousQuantity = previous.quantity;
					break;
				}
			}

			if (item.quantity <= previousQuantity)
			{
				continue;
			}

			const uint32_t deltaQuantity = item.quantity - previousQuantity;
			const InventoryItemMetadata* metadata = FindMetadata(item.itemId);
			InventoryPickupFeedback feedback{};
			feedback.text = metadata
				? ("Picked up " + std::to_string(deltaQuantity) + "x " + metadata->displayName)
				: ("Picked up " + std::to_string(deltaQuantity) + "x Item " + std::to_string(item.itemId));
			feedback.remainingSeconds = kPickupFeedbackDurationSeconds;
			feedback.active = true;
			m_state.pickupFeedback.push_back(std::move(feedback));
			if (m_state.pickupFeedback.size() > kPickupFeedbackMaxEntries)
			{
				m_state.pickupFeedback.erase(
					m_state.pickupFeedback.begin(),
					m_state.pickupFeedback.begin() + static_cast<std::ptrdiff_t>(m_state.pickupFeedback.size() - kPickupFeedbackMaxEntries));
			}
			LOG_INFO(Core, "[InventoryUiPresenter] Pickup feedback added (item_id={}, delta={})", item.itemId, deltaQuantity);
		}

		m_previousInventory = model.inventory;
	}

	void InventoryUiPresenter::RefreshTooltip()
	{
		m_state.tooltip = {};
		if (m_hoveredSlotIndex == UINT32_MAX || m_hoveredSlotIndex >= m_state.slots.size())
		{
			return;
		}

		const InventorySlotState& slot = m_state.slots[m_hoveredSlotIndex];
		if (!slot.occupied)
		{
			return;
		}

		const InventoryItemMetadata* metadata = FindMetadata(slot.itemId);
		m_state.tooltip.bounds = {
			slot.bounds.x + slot.bounds.width + 12.0f,
			slot.bounds.y,
			220.0f,
			96.0f
		};
		m_state.tooltip.title = metadata ? metadata->displayName : ("Item " + std::to_string(slot.itemId));
		m_state.tooltip.description = metadata
			? metadata->description
			: ("itemId=" + std::to_string(slot.itemId) + ", qty=" + std::to_string(slot.quantity));
		m_state.tooltip.iconPath = metadata ? metadata->iconPath : "";
		m_state.tooltip.visible = true;
	}

	const InventoryItemMetadata* InventoryUiPresenter::FindMetadata(uint32_t itemId) const
	{
		for (const InventoryItemMetadata& metadata : m_metadata)
		{
			if (metadata.itemId == itemId)
			{
				return &metadata;
			}
		}

		return nullptr;
	}

	void InventoryUiPresenter::SyncDragWithModel(const UIModel& model)
	{
		(void)model;
		if (!m_dragActive)
		{
			return;
		}
		if (m_dragSlotIndex >= m_state.slots.size())
		{
			CancelDrag();
			return;
		}
		const InventorySlotState& slot = m_state.slots[m_dragSlotIndex];
		if (!slot.occupied || slot.itemId != m_dragItemId || slot.quantity < m_dragQuantity)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] Drag invalidated by inventory sync");
			CancelDrag();
		}
	}

	bool InventoryUiPresenter::TryBeginDrag(float mouseX, float mouseY)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[InventoryUiPresenter] TryBeginDrag FAILED: not initialized");
			return false;
		}

		for (const InventorySlotState& slot : m_state.slots)
		{
			if (!slot.occupied)
			{
				continue;
			}
			if (!ContainsPoint(slot.bounds, mouseX, mouseY))
			{
				continue;
			}

			m_dragActive = true;
			m_dragSlotIndex = slot.slotIndex;
			m_dragItemId = slot.itemId;
			m_dragQuantity = std::max(1u, slot.quantity);
			RebuildDebugText();
			LOG_INFO(Core,
				"[InventoryUiPresenter] Drag started (slot={}, item_id={}, qty={})",
				m_dragSlotIndex,
				m_dragItemId,
				m_dragQuantity);
			return true;
		}
		return false;
	}

	void InventoryUiPresenter::CancelDrag()
	{
		if (!m_dragActive)
		{
			return;
		}
		LOG_INFO(Core, "[InventoryUiPresenter] Drag ended without sale");
		m_dragActive = false;
		m_dragSlotIndex = 0;
		m_dragItemId = 0;
		m_dragQuantity = 0;
		RebuildDebugText();
	}

	bool InventoryUiPresenter::GetDragSource(uint32_t& outSlotIndex, uint32_t& outItemId, uint32_t& outQty) const
	{
		if (!m_dragActive)
		{
			return false;
		}
		outSlotIndex = m_dragSlotIndex;
		outItemId = m_dragItemId;
		outQty = m_dragQuantity;
		return true;
	}

	void InventoryUiPresenter::RebuildDebugText()
	{
		m_state.debugText.clear();
		m_state.debugText += "[InventoryUi]\n";
		m_state.debugText += "viewport=";
		m_state.debugText += std::to_string(m_viewportWidth);
		m_state.debugText += "x";
		m_state.debugText += std::to_string(m_viewportHeight);
		m_state.debugText += " layout=";
		m_state.debugText += m_state.layoutValid ? "true" : "false";
		m_state.debugText += "\n";
		m_state.debugText += "slots=";
		m_state.debugText += std::to_string(m_state.slots.size());
		m_state.debugText += " tooltip=";
		m_state.debugText += m_state.tooltip.visible ? "true" : "false";
		m_state.debugText += "\n";
		for (const InventorySlotState& slot : m_state.slots)
		{
			if (!slot.occupied)
			{
				continue;
			}
			m_state.debugText += "slot ";
			m_state.debugText += std::to_string(slot.slotIndex);
			m_state.debugText += " item=";
			m_state.debugText += std::to_string(slot.itemId);
			m_state.debugText += " qty=";
			m_state.debugText += std::to_string(slot.quantity);
			m_state.debugText += "\n";
		}
		for (const InventoryPickupFeedback& feedback : m_state.pickupFeedback)
		{
			if (!feedback.active)
			{
				continue;
			}
			m_state.debugText += "pickup ";
			m_state.debugText += feedback.text;
			m_state.debugText += " remaining=";
			m_state.debugText += std::to_string(feedback.remainingSeconds);
			m_state.debugText += "\n";
		}
		if (m_dragActive)
		{
			m_state.debugText += "drag item=";
			m_state.debugText += std::to_string(m_dragItemId);
			m_state.debugText += " qty=";
			m_state.debugText += std::to_string(m_dragQuantity);
			m_state.debugText += " from slot=";
			m_state.debugText += std::to_string(m_dragSlotIndex);
			m_state.debugText += "\n";
		}
	}
}
