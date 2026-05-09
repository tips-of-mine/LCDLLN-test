#pragma once

#include "engine/core/Log.h"
#include "engine/core/Time.h"

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::gameplay
{
    /// Type of status effect, determines how it is applied and removed.
    enum class StatusEffectType : uint8_t
    {
        Buff    = 0, ///< Positive effect (stat bonus, regeneration)
        Debuff  = 1, ///< Negative effect (stat reduction)
        DoT     = 2, ///< Damage over Time (periodic damage)
        HoT     = 3, ///< Heal over Time (periodic healing)
        Stun    = 4, ///< Crowd control: blocks all movement and actions
        Slow    = 5, ///< Crowd control: reduces movement speed
        Root    = 6, ///< Crowd control: blocks movement but allows actions
    };

    /// Controls how multiple applications of the same effect stack.
    enum class StackType : uint8_t
    {
        Refresh    = 0, ///< Resets duration to full on re-application (keeps stack count)
        Extend     = 1, ///< Adds remaining duration on re-application (keeps stack count)
        Independent= 2, ///< Each application is tracked independently (up to maxStacks)
    };

    /// Dispel category used to determine which abilities can remove this effect.
    enum class DispelType : uint8_t
    {
        None    = 0,
        Magic   = 1,
        Curse   = 2,
        Poison  = 3,
        Disease = 4,
    };

    /// One active application of a status effect on an entity.
    struct StatusEffect
    {
        /// Unique definition id (e.g. "poison_dot", "battle_cry").
        std::string effectId;

        /// Entity id of the caster (0 = environment/anonymous).
        uint64_t casterId = 0;

        /// Entity id of the target.
        uint64_t targetId = 0;

        /// Monotonic timestamp (nanoseconds) when this effect was applied/last refreshed.
        uint64_t startTimeNs = 0;

        /// Total duration in seconds. 0 = permanent (until dispelled).
        float durationSeconds = 0.0f;

        /// Interval in seconds between periodic ticks (DoT/HoT). 0 = no ticking.
        float tickIntervalSeconds = 0.0f;

        /// Current number of stacks (1 = not stacked).
        uint32_t stacks = 1;

        /// Maximum allowed stacks for this effect.
        uint32_t maxStacks = 1;

        /// Stacking strategy.
        StackType stackType = StackType::Refresh;

        /// Dispel category.
        DispelType dispelType = DispelType::None;

        /// Effect type.
        StatusEffectType type = StatusEffectType::Buff;

        /// Periodic amount per tick (damage for DoT, healing for HoT).
        /// Multiplied by stacks when applied.
        float tickAmount = 0.0f;

        /// Stat modifier amount (positive = bonus, negative = reduction).
        float statModifier = 0.0f;

        /// Name of the stat being modified (e.g. "strength", "speed").
        std::string statName;

        /// Monotonic timestamp of the next tick (nanoseconds).
        uint64_t nextTickNs = 0;

        /// Monotonic timestamp when the effect expires (nanoseconds). 0 = permanent.
        uint64_t expireTimeNs = 0;

        /// Returns true if the effect is permanent (no expiry).
        bool IsPermanent() const { return durationSeconds <= 0.0f; }

        /// Returns true if this is a crowd-control effect.
        bool IsCrowdControl() const
        {
            return type == StatusEffectType::Stun
                || type == StatusEffectType::Slow
                || type == StatusEffectType::Root;
        }
    };

    /// Callback invoked when a periodic tick fires on an entity.
    /// Parameters: targetId, effectId, amount (scaled by stacks).
    using TickCallback = std::function<void(uint64_t targetId, std::string_view effectId, float amount)>;

    /// Callback invoked when active effects on an entity change (added/removed/stacks changed).
    /// Use to trigger stat recalculation.
    using StatsChangedCallback = std::function<void(uint64_t entityId)>;

    /// Maximum number of active effects allowed per entity.
    static constexpr uint32_t kMaxEffectsPerEntity = 50;

    /// Manages all active status effects for all entities.
    ///
    /// Usage:
    ///   - Call Init() once at boot.
    ///   - Call Tick(nowNs) every server/simulation frame.
    ///   - Call ApplyEffect() to add a buff/debuff.
    ///   - Call Dispel() to remove effects by dispel type.
    ///   - Call QueryCCFlags() to get stun/root/slow state.
    ///   - Call GetStatModifiers() to accumulate stat bonuses for an entity.
    class StatusEffectManager final
    {
    public:
        StatusEffectManager();
        ~StatusEffectManager();

        /// Initialize the manager. Must be called before any other method.
        /// \return true on success.
        bool Init();

        /// Release all state and emit shutdown log.
        void Shutdown();

        /// Advance all active effects: fire periodic ticks, expire finished effects.
        /// \param nowNs Current monotonic time in nanoseconds (from Time::NowTicks()).
        void Tick(uint64_t nowNs);

        /// Apply a status effect to an entity. Respects stacking rules and the
        /// per-entity cap (kMaxEffectsPerEntity).
        /// \return true if the effect was accepted; false if the cap was reached.
        bool ApplyEffect(const StatusEffect& effect, uint64_t nowNs);

        /// Remove all effects of the given dispel type from an entity.
        /// \param entityId Target entity.
        /// \param dispelType Type of dispel (magic, curse, poison, disease).
        /// \return Number of effects removed.
        uint32_t Dispel(uint64_t entityId, DispelType dispelType);

        /// Remove a specific effect by effectId from an entity (e.g. on expire or manual remove).
        /// Removes the first matching entry.
        /// \return true if an entry was found and removed.
        bool RemoveEffect(uint64_t entityId, std::string_view effectId);

        /// Remove all active effects from an entity (e.g. on death).
        void RemoveAllEffects(uint64_t entityId);

        /// Crowd-control query result.
        struct CCFlags
        {
            bool isStunned = false; ///< Entity cannot move or act.
            bool isRooted  = false; ///< Entity cannot move but can act.
            bool isSlowed  = false; ///< Entity movement speed is reduced.
        };

        /// Query current crowd-control state for an entity.
        CCFlags QueryCCFlags(uint64_t entityId) const;

        /// Accumulated stat modifier for one stat.
        struct StatModifier
        {
            std::string statName;
            float totalAmount = 0.0f; ///< Sum of all active modifiers (stack-scaled).
        };

        /// Compute the total stat modifiers applied to an entity from all active effects.
        /// Returns one entry per unique stat name.
        std::vector<StatModifier> GetStatModifiers(uint64_t entityId) const;

        /// Returns a read-only view of all active effects for one entity.
        const std::list<StatusEffect>* GetEffects(uint64_t entityId) const;

        /// Returns the number of active effects on an entity.
        uint32_t GetEffectCount(uint64_t entityId) const;

        /// Set a callback invoked on every periodic tick (DoT/HoT).
        void SetTickCallback(TickCallback cb);

        /// Set a callback invoked whenever effects change on an entity.
        void SetStatsChangedCallback(StatsChangedCallback cb);

        /// Returns true if the entity has an active effect with the given id.
        bool HasEffect(uint64_t entityId, std::string_view effectId) const;

    private:
        /// Find an existing effect entry by effectId for a given entity.
        /// Returns nullptr if not found.
        StatusEffect* FindEffect(uint64_t entityId, std::string_view effectId);

        /// Apply stacking logic for an existing effect re-application.
        void ApplyStacking(StatusEffect& existing, const StatusEffect& incoming, uint64_t nowNs);

        bool m_initialized = false;

        /// Active effects per entity: entityId -> list of effects.
        /// Using std::list so iterators/references remain stable during Tick().
        std::unordered_map<uint64_t, std::list<StatusEffect>> m_effectsByEntity;

        TickCallback         m_tickCallback;
        StatsChangedCallback m_statsChangedCallback;
    };

} // namespace engine::gameplay
