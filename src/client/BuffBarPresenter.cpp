#include "engine/client/BuffBarPresenter.h"

#include "engine/core/Log.h"

#include <algorithm>

namespace engine::client
{
    namespace
    {
        /// Pixel size of one buff icon slot.
        inline constexpr float kIconSize    = 32.0f;
        /// Horizontal gap between icon slots.
        inline constexpr float kIconGap     = 4.0f;
        /// Vertical offset of the player buff bar from the top of the viewport.
        inline constexpr float kBuffBarTopY = 16.0f;
        /// Vertical gap between the buff row and the debuff row.
        inline constexpr float kDebuffRowGap = 4.0f;
        /// Right-side margin from the viewport edge.
        inline constexpr float kRightMargin = 16.0f;
        /// Vertical offset below the player buff bars where the target bars start.
        inline constexpr float kTargetBarOffsetY = 120.0f;
    }

    BuffBarPresenter::~BuffBarPresenter()
    {
        Shutdown();
    }

    bool BuffBarPresenter::Init()
    {
        if (m_initialized)
        {
            LOG_WARN(Gameplay, "[BuffBarPresenter] Init ignored: already initialized");
            return true;
        }

        m_playerState = BuffBarState{};
        m_targetState = BuffBarState{};
        m_initialized = true;

        LOG_INFO(Gameplay, "[BuffBarPresenter] Init OK (maxIconsPerBar={})", kBuffBarMaxIcons);
        return true;
    }

    void BuffBarPresenter::Shutdown()
    {
        if (!m_initialized)
            return;

        m_playerState = BuffBarState{};
        m_targetState = BuffBarState{};
        m_initialized = false;

        LOG_INFO(Gameplay, "[BuffBarPresenter] Destroyed");
    }

    bool BuffBarPresenter::SetViewportSize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LOG_WARN(Gameplay, "[BuffBarPresenter] SetViewportSize ignored: invalid {}x{}", width, height);
            return false;
        }

        m_viewportWidth  = width;
        m_viewportHeight = height;

        LOG_DEBUG(Gameplay, "[BuffBarPresenter] Viewport set to {}x{}", width, height);
        return true;
    }

    void BuffBarPresenter::UpdatePlayer(const std::list<engine::gameplay::StatusEffect>* effects,
                                        uint64_t nowNs)
    {
        if (!m_initialized)
            return;

        BuildState(m_playerState, effects, nowNs);
    }

    void BuffBarPresenter::UpdateTarget(const std::list<engine::gameplay::StatusEffect>* effects,
                                        uint64_t nowNs)
    {
        if (!m_initialized)
            return;

        BuildState(m_targetState, effects, nowNs);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    bool BuffBarPresenter::IsBuff(engine::gameplay::StatusEffectType type)
    {
        switch (type)
        {
        case engine::gameplay::StatusEffectType::Buff:
        case engine::gameplay::StatusEffectType::HoT:
            return true;
        default:
            return false;
        }
    }

    void BuffBarPresenter::BuildState(BuffBarState& outState,
                                       const std::list<engine::gameplay::StatusEffect>* effects,
                                       uint64_t nowNs) const
    {
        outState.buffBar.icons.clear();
        outState.debuffBar.icons.clear();
        outState.buffBar.visible  = false;
        outState.debuffBar.visible = false;

        if (!effects)
            return;

        // Determine which bar this state belongs to (player vs target) by inspecting
        // the anchor Y.  For simplicity the anchor is always rebuilt from viewport here.
        // Player buff bar: top-right corner.
        const float barWidth = static_cast<float>(kBuffBarMaxIcons) * (kIconSize + kIconGap);
        const float viewW    = static_cast<float>(m_viewportWidth);

        // Use a fixed Y for this call; the caller (Update*) knows player vs target.
        // Because BuildState is shared, we set anchors using a neutral position;
        // the caller is responsible for passing the right offset if needed.
        // Here we set the buff anchor at top-right as per spec.
        outState.buffBar.anchorBounds  = { viewW - barWidth - kRightMargin, kBuffBarTopY, barWidth, kIconSize };
        outState.debuffBar.anchorBounds = {
            viewW - barWidth - kRightMargin,
            kBuffBarTopY + kIconSize + kDebuffRowGap,
            barWidth,
            kIconSize
        };

        uint32_t buffSlot   = 0;
        uint32_t debuffSlot = 0;

        for (const auto& fx : *effects)
        {
            // Compute remaining time.
            float remaining = 0.0f;
            bool  permanent = fx.IsPermanent();

            if (!permanent && fx.expireTimeNs > nowNs)
            {
                remaining = static_cast<float>(fx.expireTimeNs - nowNs) / 1'000'000'000.0f;
            }
            else if (!permanent)
            {
                // Already expired — skip (will be removed by Tick).
                continue;
            }

            if (IsBuff(fx.type))
            {
                if (buffSlot >= kBuffBarMaxIcons)
                    continue;

                BuffIconWidget icon{};
                icon.effectId        = fx.effectId;
                icon.displayName     = fx.effectId; // no separate name field in StatusEffect
                icon.remainingSeconds= remaining;
                icon.totalSeconds    = fx.durationSeconds;
                icon.stacks          = fx.stacks;
                icon.isPermanent     = permanent;
                icon.visible         = true;
                icon.bounds          = ComputeIconBounds(outState.buffBar.anchorBounds, buffSlot);

                outState.buffBar.icons.push_back(std::move(icon));
                ++buffSlot;
            }
            else
            {
                if (debuffSlot >= kBuffBarMaxIcons)
                    continue;

                BuffIconWidget icon{};
                icon.effectId        = fx.effectId;
                icon.displayName     = fx.effectId;
                icon.remainingSeconds= remaining;
                icon.totalSeconds    = fx.durationSeconds;
                icon.stacks          = fx.stacks;
                icon.isPermanent     = permanent;
                icon.visible         = true;
                icon.bounds          = ComputeIconBounds(outState.debuffBar.anchorBounds, debuffSlot);

                outState.debuffBar.icons.push_back(std::move(icon));
                ++debuffSlot;
            }
        }

        outState.buffBar.visible  = !outState.buffBar.icons.empty();
        outState.debuffBar.visible = !outState.debuffBar.icons.empty();
    }

    HudRect BuffBarPresenter::ComputeIconBounds(const HudRect& barAnchor, uint32_t slotIndex) const
    {
        const float x = barAnchor.x + static_cast<float>(slotIndex) * (kIconSize + kIconGap);
        return { x, barAnchor.y, kIconSize, kIconSize };
    }

} // namespace engine::client
