#pragma once

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
    /// Pixel-space rectangle used by the trade panel layout.
    struct TradeRect
    {
        float x      = 0.0f;
        float y      = 0.0f;
        float width  = 0.0f;
        float height = 0.0f;
    };

    /// One item row displayed in a trade panel side.
    struct TradeItemView
    {
        uint32_t    itemId      = 0;
        uint32_t    quantity    = 0;
        std::string displayName;
        std::string iconPath;
    };

    /// One side (self or other) of the split trade window.
    struct TradeSideView
    {
        std::string                 playerLabel;
        std::vector<TradeItemView>  items;
        uint32_t                    gold   = 0;
        bool                        locked = false;
    };

    /// Fully resolved trade window state for the rendering layer (M35.3).
    struct TradePanelState
    {
        TradeRect    panelBounds{};
        TradeSideView mySide{};
        TradeSideView theirSide{};
        /// Remaining seconds in the review phase (0 when not reviewing).
        float        reviewSecondsRemaining = 0.0f;
        std::string  debugText;
        bool         visible      = false;
        bool         layoutValid  = false;
        bool         canConfirm   = false; ///< True when both sides are locked.
        /// True when local player just completed a trade.
        bool         lastSuccess  = false;
        std::string  lastError;
    };

    /// Builds a split trade window panel state from the shared UI model (M35.3).
    class TradeUiPresenter final
    {
    public:
        /// Construct an uninitialized presenter.
        TradeUiPresenter() = default;

        /// Release presenter resources.
        ~TradeUiPresenter();

        /// Initialize the presenter and load item display metadata from content.
        bool Init(const engine::core::Config& config);

        /// Shutdown the presenter and release cached state.
        void Shutdown();

        /// Update the viewport-dependent layout for the trade panel.
        bool SetViewportSize(uint32_t width, uint32_t height);

        /// Apply one UI model snapshot and rebuild the trade panel state.
        bool ApplyModel(const UIModel& model, uint32_t changeMask);

        /// Return the immutable resolved trade panel state.
        const TradePanelState& GetState() const { return m_state; }

    private:
        /// Load item display metadata from the inventory items metadata file.
        bool LoadMetadata(const engine::core::Config& config);

        /// Parse one metadata line using the `item|icon|name|description` format.
        bool ParseMetadataLine(std::string_view line, uint32_t& outItemId,
                               std::string& outIcon, std::string& outName) const;

        /// Recompute panel rectangle after a viewport change.
        void RebuildLayout();

        /// Rebuild both trade sides from the current model state.
        void RebuildSides(const UIModel& model);

        /// Rebuild textual debug dump of the trade panel state.
        void RebuildDebugText();

        /// Return the display name for one item id, or empty when metadata is absent.
        std::string FindDisplayName(uint32_t itemId) const;

        /// Return the icon path for one item id, or empty when metadata is absent.
        std::string FindIconPath(uint32_t itemId) const;

        struct ItemMetaEntry
        {
            uint32_t    itemId = 0;
            std::string iconPath;
            std::string displayName;
        };

        TradePanelState          m_state{};
        std::vector<ItemMetaEntry> m_metadata;
        uint32_t                 m_viewportWidth  = 0;
        uint32_t                 m_viewportHeight = 0;
        std::string              m_metadataRelPath;
        bool                     m_initialized    = false;
    };
}
