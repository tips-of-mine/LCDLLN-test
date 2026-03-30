#include "engine/client/TradeUi.h"

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
                ++begin;
            while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
                --end;
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

    TradeUiPresenter::~TradeUiPresenter()
    {
        Shutdown();
    }

    bool TradeUiPresenter::Init(const engine::core::Config& config)
    {
        if (m_initialized)
        {
            LOG_WARN(Core, "[TradeUiPresenter] Init ignored: already initialized");
            return true;
        }

        // Reuse the same inventory metadata file so item names/icons are shared.
        m_metadataRelPath = config.GetString("ui.inventory_items_path", "ui/inventory_items.txt");
        if (!LoadMetadata(config))
        {
            LOG_ERROR(Core, "[TradeUiPresenter] Init FAILED: metadata load failed ({})", m_metadataRelPath);
            return false;
        }

        m_initialized = true;
        RebuildLayout();
        RebuildDebugText();
        LOG_INFO(Core, "[TradeUiPresenter] Init OK (meta_entries={}, path={})", m_metadata.size(), m_metadataRelPath);
        return true;
    }

    void TradeUiPresenter::Shutdown()
    {
        if (!m_initialized)
            return;
        m_initialized    = false;
        m_state          = {};
        m_metadata.clear();
        m_viewportWidth  = 0;
        m_viewportHeight = 0;
        m_metadataRelPath.clear();
        LOG_INFO(Core, "[TradeUiPresenter] Destroyed");
    }

    bool TradeUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
    {
        if (!m_initialized)
        {
            LOG_ERROR(Core, "[TradeUiPresenter] SetViewportSize FAILED: not initialized");
            return false;
        }
        if (width == 0 || height == 0)
        {
            LOG_WARN(Core, "[TradeUiPresenter] SetViewportSize FAILED: invalid size {}x{}", width, height);
            return false;
        }
        m_viewportWidth  = width;
        m_viewportHeight = height;
        RebuildLayout();
        RebuildDebugText();
        LOG_INFO(Core, "[TradeUiPresenter] Viewport updated ({}x{})", width, height);
        return true;
    }

    bool TradeUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
    {
        if (!m_initialized)
        {
            LOG_ERROR(Core, "[TradeUiPresenter] ApplyModel FAILED: not initialized");
            return false;
        }

        if ((changeMask & UIModelChangeTrade) == 0u)
            return true;

        if (!m_state.layoutValid)
            RebuildLayout();

        RebuildSides(model);
        RebuildDebugText();
        LOG_DEBUG(Core, "[TradeUiPresenter] Model applied (open={}, state={}, other={})",
            model.trade.isOpen ? "true" : "false",
            model.trade.tradeState,
            model.trade.theirPlayerName);
        return true;
    }

    bool TradeUiPresenter::LoadMetadata(const engine::core::Config& config)
    {
        const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, m_metadataRelPath);
        if (text.empty())
        {
            LOG_WARN(Core, "[TradeUiPresenter] Metadata missing or empty ({}); trade UI will show item ids only",
                m_metadataRelPath);
            return true;
        }

        m_metadata.clear();
        size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const size_t lineEnd = text.find('\n', lineStart);
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
                break;
            lineStart = lineEnd + 1;
        }

        LOG_INFO(Core, "[TradeUiPresenter] Metadata loaded (entries={}, path={})", m_metadata.size(), m_metadataRelPath);
        return true;
    }

    bool TradeUiPresenter::ParseMetadataLine(
        std::string_view line,
        uint32_t& outItemId,
        std::string& outIcon,
        std::string& outName) const
    {
        size_t colCount = 0;
        const auto cols = SplitColumns(line, colCount);
        if (colCount < 3)
            return false;
        const std::string_view idView = Trim(cols[0]);
        if (idView.empty())
            return false;
        uint32_t id = 0;
        for (const char ch : idView)
        {
            if (ch < '0' || ch > '9')
                return false;
            id = (id * 10u) + static_cast<uint32_t>(ch - '0');
        }
        outItemId = id;
        outIcon   = std::string(Trim(cols[1]));
        outName   = std::string(Trim(cols[2]));
        return !outName.empty();
    }

    void TradeUiPresenter::RebuildLayout()
    {
        const float vw = static_cast<float>(m_viewportWidth  == 0 ? 1280u : m_viewportWidth);
        const float vh = static_cast<float>(m_viewportHeight == 0 ? 720u  : m_viewportHeight);

        // Centred wide panel; split left (self) / right (other).
        const float panelWidth  = std::clamp(vw * 0.60f, 480.0f, 720.0f);
        const float panelHeight = std::clamp(vh * 0.55f, 320.0f, 480.0f);
        const float panelX      = (vw - panelWidth) * 0.5f;
        const float panelY      = (vh - panelHeight) * 0.5f;

        m_state.panelBounds = { panelX, panelY, panelWidth, panelHeight };
        m_state.layoutValid = true;
    }

    void TradeUiPresenter::RebuildSides(const UIModel& model)
    {
        const UITradeState& trade = model.trade;

        m_state.visible  = trade.isOpen;
        m_state.lastSuccess = trade.lastResultSuccess;
        m_state.lastError   = trade.lastResultError;

        // Review phase timer in seconds (approximate: 1 tick ≈ 1/30s by default).
        m_state.reviewSecondsRemaining =
            (trade.tradeState == 2u) // Reviewing
            ? static_cast<float>(trade.reviewTicksRemaining) / 30.0f
            : 0.0f;

        m_state.canConfirm = trade.myOffer.locked && trade.theirOffer.locked;

        // My side.
        m_state.mySide.playerLabel = "You";
        m_state.mySide.gold        = trade.myOffer.gold;
        m_state.mySide.locked      = trade.myOffer.locked;
        m_state.mySide.items.clear();
        for (const UITradeItemEntry& entry : trade.myOffer.items)
        {
            TradeItemView view{};
            view.itemId      = entry.itemId;
            view.quantity    = entry.quantity;
            view.displayName = FindDisplayName(entry.itemId);
            view.iconPath    = FindIconPath(entry.itemId);
            m_state.mySide.items.push_back(std::move(view));
        }

        // Their side.
        m_state.theirSide.playerLabel = trade.theirPlayerName.empty() ? "Other" : trade.theirPlayerName;
        m_state.theirSide.gold        = trade.theirOffer.gold;
        m_state.theirSide.locked      = trade.theirOffer.locked;
        m_state.theirSide.items.clear();
        for (const UITradeItemEntry& entry : trade.theirOffer.items)
        {
            TradeItemView view{};
            view.itemId      = entry.itemId;
            view.quantity    = entry.quantity;
            view.displayName = FindDisplayName(entry.itemId);
            view.iconPath    = FindIconPath(entry.itemId);
            m_state.theirSide.items.push_back(std::move(view));
        }
    }

    void TradeUiPresenter::RebuildDebugText()
    {
        m_state.debugText.clear();
        m_state.debugText += "[TradeUi]\n";
        m_state.debugText += "open=";
        m_state.debugText += m_state.visible ? "true" : "false";
        m_state.debugText += " can_confirm=";
        m_state.debugText += m_state.canConfirm ? "true" : "false";
        m_state.debugText += " review_sec=";
        m_state.debugText += std::to_string(m_state.reviewSecondsRemaining);
        m_state.debugText += "\n";

        // My side.
        m_state.debugText += "my(";
        m_state.debugText += m_state.mySide.playerLabel;
        m_state.debugText += ") gold=";
        m_state.debugText += std::to_string(m_state.mySide.gold);
        m_state.debugText += " locked=";
        m_state.debugText += m_state.mySide.locked ? "true" : "false";
        m_state.debugText += " items=";
        m_state.debugText += std::to_string(m_state.mySide.items.size());
        m_state.debugText += "\n";
        for (const TradeItemView& item : m_state.mySide.items)
        {
            m_state.debugText += " item_id=";
            m_state.debugText += std::to_string(item.itemId);
            m_state.debugText += " qty=";
            m_state.debugText += std::to_string(item.quantity);
            m_state.debugText += " name=";
            m_state.debugText += item.displayName.empty() ? "?" : item.displayName;
            m_state.debugText += "\n";
        }

        // Their side.
        m_state.debugText += "their(";
        m_state.debugText += m_state.theirSide.playerLabel;
        m_state.debugText += ") gold=";
        m_state.debugText += std::to_string(m_state.theirSide.gold);
        m_state.debugText += " locked=";
        m_state.debugText += m_state.theirSide.locked ? "true" : "false";
        m_state.debugText += " items=";
        m_state.debugText += std::to_string(m_state.theirSide.items.size());
        m_state.debugText += "\n";
        for (const TradeItemView& item : m_state.theirSide.items)
        {
            m_state.debugText += " item_id=";
            m_state.debugText += std::to_string(item.itemId);
            m_state.debugText += " qty=";
            m_state.debugText += std::to_string(item.quantity);
            m_state.debugText += " name=";
            m_state.debugText += item.displayName.empty() ? "?" : item.displayName;
            m_state.debugText += "\n";
        }
    }

    std::string TradeUiPresenter::FindDisplayName(uint32_t itemId) const
    {
        for (const ItemMetaEntry& entry : m_metadata)
        {
            if (entry.itemId == itemId)
                return entry.displayName;
        }
        return "Item " + std::to_string(itemId);
    }

    std::string TradeUiPresenter::FindIconPath(uint32_t itemId) const
    {
        for (const ItemMetaEntry& entry : m_metadata)
        {
            if (entry.itemId == itemId)
                return entry.iconPath;
        }
        return {};
    }
}
