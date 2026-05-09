#pragma once

#include "engine/client/CombatHud.h"
#include "engine/gameplay/StatusEffect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
    /// Maximum number of icons displayed per bar (buffs or debuffs).
    static constexpr uint32_t kBuffBarMaxIcons = 10;

    /// One buff/debuff icon widget ready for rendering.
    struct BuffIconWidget
    {
        /// Effect identifier (maps to icon atlas key or filename).
        std::string effectId;

        /// Display name of the effect.
        std::string displayName;

        /// Remaining duration in seconds (0 = expired or permanent).
        float remainingSeconds = 0.0f;

        /// Total duration in seconds (0 = permanent).
        float totalSeconds = 0.0f;

        /// Current stack count.
        uint32_t stacks = 1;

        /// Pixel-space bounds computed by the presenter.
        HudRect bounds{};

        /// True if the effect is permanent (no timer shown).
        bool isPermanent = false;

        /// True if this slot is visible.
        bool visible = false;
    };

    /// Horizontal bar of buff or debuff icons with timers.
    struct BuffBarWidget
    {
        /// Top-left anchor computed from viewport and config.
        HudRect anchorBounds{};

        /// Sorted icon slots (buffs left-to-right, debuffs left-to-right).
        std::vector<BuffIconWidget> icons;

        /// True when the bar has at least one visible icon.
        bool visible = false;
    };

    /// Full buff/debuff HUD state for one unit (player or target).
    struct BuffBarState
    {
        BuffBarWidget buffBar;    ///< Row for beneficial effects (Buff, HoT).
        BuffBarWidget debuffBar;  ///< Row for detrimental effects (Debuff, DoT, CC).
    };

    /// Builds buff/debuff bar display state from a StatusEffectManager snapshot.
    ///
    /// Usage:
    ///   - Call Init() once after construction.
    ///   - Call SetViewportSize() when the viewport changes.
    ///   - Call Update() each frame with the current time and effect list.
    ///   - Read GetPlayerState() / GetTargetState() for rendering.
    class BuffBarPresenter final
    {
    public:
        BuffBarPresenter() = default;
        ~BuffBarPresenter();

        /// Initialize the presenter.
        /// \return true on success.
        bool Init();

        /// Release presenter resources.
        void Shutdown();

        /// Notify the presenter of a viewport size change; recomputes layout.
        /// \return true if dimensions are valid.
        bool SetViewportSize(uint32_t width, uint32_t height);

        /// Rebuild the player buff bar from the given effect list and current time.
        /// \param effects  Active effects list returned by StatusEffectManager::GetEffects().
        ///                 May be nullptr (treated as empty).
        /// \param nowNs    Current monotonic time in nanoseconds.
        void UpdatePlayer(const std::list<engine::gameplay::StatusEffect>* effects,
                          uint64_t nowNs);

        /// Rebuild the target buff bar from the given effect list and current time.
        void UpdateTarget(const std::list<engine::gameplay::StatusEffect>* effects,
                          uint64_t nowNs);

        /// Read-only access to the player buff/debuff bars.
        const BuffBarState& GetPlayerState() const { return m_playerState; }

        /// Read-only access to the target buff/debuff bars.
        const BuffBarState& GetTargetState() const { return m_targetState; }

    private:
        /// Returns true when the effect type belongs to the buff bar.
        static bool IsBuff(engine::gameplay::StatusEffectType type);

        /// Rebuild one BuffBarState from an effect list and current timestamp.
        void BuildState(BuffBarState& outState,
                        const std::list<engine::gameplay::StatusEffect>* effects,
                        uint64_t nowNs) const;

        /// Compute pixel-space bounds for one icon slot within a bar.
        HudRect ComputeIconBounds(const HudRect& barAnchor, uint32_t slotIndex) const;

        uint32_t m_viewportWidth  = 0;
        uint32_t m_viewportHeight = 0;
        bool     m_initialized    = false;

        BuffBarState m_playerState{};
        BuffBarState m_targetState{};
    };

} // namespace engine::client
