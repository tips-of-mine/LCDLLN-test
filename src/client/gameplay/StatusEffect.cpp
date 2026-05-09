#include "engine/gameplay/StatusEffect.h"

#include <algorithm>
#include <cassert>

namespace engine::gameplay
{
    // -------------------------------------------------------------------------
    // StatusEffectManager
    // -------------------------------------------------------------------------

    StatusEffectManager::StatusEffectManager()
        : m_initialized(false)
    {
    }

    StatusEffectManager::~StatusEffectManager()
    {
        if (m_initialized)
        {
            Shutdown();
        }
    }

    bool StatusEffectManager::Init()
    {
        if (m_initialized)
        {
            LOG_WARN(Gameplay, "[StatusEffectManager] Init called more than once – ignored");
            return true;
        }

        m_effectsByEntity.clear();
        m_initialized = true;

        LOG_INFO(Gameplay, "[StatusEffectManager] Init OK (maxEffectsPerEntity={})", kMaxEffectsPerEntity);
        return true;
    }

    void StatusEffectManager::Shutdown()
    {
        if (!m_initialized)
            return;

        const size_t entityCount = m_effectsByEntity.size();
        m_effectsByEntity.clear();
        m_tickCallback         = nullptr;
        m_statsChangedCallback = nullptr;
        m_initialized          = false;

        LOG_INFO(Gameplay, "[StatusEffectManager] Destroyed (was tracking {} entities)", entityCount);
    }

    // -------------------------------------------------------------------------
    // Tick – advances time, fires periodic ticks, expires finished effects
    // -------------------------------------------------------------------------

    void StatusEffectManager::Tick(uint64_t nowNs)
    {
        if (!m_initialized)
            return;

        // Collect entities that have had effects expire or tick this frame.
        // We process a copy of the key set to allow safe removal during iteration.
        for (auto& [entityId, effects] : m_effectsByEntity)
        {
            bool statsChanged = false;

            auto it = effects.begin();
            while (it != effects.end())
            {
                StatusEffect& fx = *it;

                // --- Periodic tick (DoT / HoT) ---
                if (fx.tickIntervalSeconds > 0.0f && nowNs >= fx.nextTickNs)
                {
                    const float scaledAmount = fx.tickAmount * static_cast<float>(fx.stacks);
                    if (m_tickCallback)
                    {
                        m_tickCallback(fx.targetId, fx.effectId, scaledAmount);
                    }

                    LOG_DEBUG(Gameplay,
                        "[StatusEffectManager] Tick effect={} entity={} amount={:.2f} stacks={}",
                        fx.effectId, entityId, scaledAmount, fx.stacks);

                    // Schedule next tick.
                    const uint64_t intervalNs = static_cast<uint64_t>(
                        fx.tickIntervalSeconds * 1'000'000'000.0f);
                    fx.nextTickNs = nowNs + intervalNs;
                }

                // --- Expiry check ---
                if (!fx.IsPermanent() && nowNs >= fx.expireTimeNs)
                {
                    LOG_DEBUG(Gameplay,
                        "[StatusEffectManager] Effect expired: id={} entity={}",
                        fx.effectId, entityId);

                    it = effects.erase(it);
                    statsChanged = true;
                    continue;
                }

                ++it;
            }

            if (statsChanged && m_statsChangedCallback)
            {
                m_statsChangedCallback(entityId);
            }
        }
    }

    // -------------------------------------------------------------------------
    // ApplyEffect – applies stacking rules then inserts or updates
    // -------------------------------------------------------------------------

    bool StatusEffectManager::ApplyEffect(const StatusEffect& effect, uint64_t nowNs)
    {
        if (!m_initialized)
        {
            LOG_WARN(Gameplay, "[StatusEffectManager] ApplyEffect called before Init");
            return false;
        }

        auto& effects = m_effectsByEntity[effect.targetId];

        // --- Per-entity cap ---
        if (effects.size() >= kMaxEffectsPerEntity)
        {
            LOG_WARN(Gameplay,
                "[StatusEffectManager] ApplyEffect: entity={} reached cap={}, effect={} rejected",
                effect.targetId, kMaxEffectsPerEntity, effect.effectId);
            return false;
        }

        // --- Check for existing effect with same id ---
        StatusEffect* existing = FindEffect(effect.targetId, effect.effectId);
        if (existing)
        {
            ApplyStacking(*existing, effect, nowNs);

            LOG_DEBUG(Gameplay,
                "[StatusEffectManager] ApplyEffect (stack): id={} entity={} stacks={}",
                effect.effectId, effect.targetId, existing->stacks);

            if (m_statsChangedCallback)
                m_statsChangedCallback(effect.targetId);

            return true;
        }

        // --- New effect: compute timestamps and insert ---
        StatusEffect newEffect = effect;
        newEffect.startTimeNs  = nowNs;
        newEffect.stacks       = 1;

        if (!newEffect.IsPermanent())
        {
            const uint64_t durationNs = static_cast<uint64_t>(
                newEffect.durationSeconds * 1'000'000'000.0f);
            newEffect.expireTimeNs = nowNs + durationNs;
        }
        else
        {
            newEffect.expireTimeNs = 0;
        }

        if (newEffect.tickIntervalSeconds > 0.0f)
        {
            const uint64_t intervalNs = static_cast<uint64_t>(
                newEffect.tickIntervalSeconds * 1'000'000'000.0f);
            newEffect.nextTickNs = nowNs + intervalNs;
        }
        else
        {
            newEffect.nextTickNs = 0;
        }

        effects.push_back(std::move(newEffect));

        LOG_DEBUG(Gameplay,
            "[StatusEffectManager] ApplyEffect (new): id={} entity={} duration={:.1f}s",
            effect.effectId, effect.targetId, effect.durationSeconds);

        if (m_statsChangedCallback)
            m_statsChangedCallback(effect.targetId);

        return true;
    }

    // -------------------------------------------------------------------------
    // ApplyStacking – mutates an existing effect according to its stack type
    // -------------------------------------------------------------------------

    void StatusEffectManager::ApplyStacking(StatusEffect& existing,
                                             const StatusEffect& incoming,
                                             uint64_t nowNs)
    {
        switch (existing.stackType)
        {
        case StackType::Refresh:
        {
            // Reset duration to full; add stack if below cap.
            if (!existing.IsPermanent())
            {
                const uint64_t durationNs = static_cast<uint64_t>(
                    existing.durationSeconds * 1'000'000'000.0f);
                existing.expireTimeNs = nowNs + durationNs;
            }
            existing.startTimeNs = nowNs;

            if (existing.stacks < existing.maxStacks)
                ++existing.stacks;

            break;
        }
        case StackType::Extend:
        {
            // Add remaining duration; add stack if below cap.
            if (!existing.IsPermanent())
            {
                const uint64_t addNs = static_cast<uint64_t>(
                    incoming.durationSeconds * 1'000'000'000.0f);
                existing.expireTimeNs += addNs;
            }

            if (existing.stacks < existing.maxStacks)
                ++existing.stacks;

            break;
        }
        case StackType::Independent:
        {
            // Each application is independent – just increment stacks; do not change
            // existing duration.  The original entry tracks the "oldest" instance.
            if (existing.stacks < existing.maxStacks)
                ++existing.stacks;

            break;
        }
        }

        // Carry over tick/stat values from incoming if they differ.
        // (Allows upgraded versions of the same spell to override.)
        if (incoming.tickAmount > 0.0f)
            existing.tickAmount = incoming.tickAmount;
        if (incoming.statModifier != 0.0f)
            existing.statModifier = incoming.statModifier;

        (void)nowNs; // used in Refresh branch above
    }

    // -------------------------------------------------------------------------
    // Dispel
    // -------------------------------------------------------------------------

    uint32_t StatusEffectManager::Dispel(uint64_t entityId, DispelType dispelType)
    {
        if (!m_initialized)
            return 0;

        auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return 0;

        auto& effects = it->second;
        uint32_t removed = 0;

        auto efIt = effects.begin();
        while (efIt != effects.end())
        {
            if (efIt->dispelType == dispelType)
            {
                LOG_DEBUG(Gameplay,
                    "[StatusEffectManager] Dispel: removed effect={} from entity={}",
                    efIt->effectId, entityId);
                efIt = effects.erase(efIt);
                ++removed;
            }
            else
            {
                ++efIt;
            }
        }

        if (removed > 0)
        {
            LOG_INFO(Gameplay,
                "[StatusEffectManager] Dispel: entity={} dispelType={} removed={}",
                entityId, static_cast<int>(dispelType), removed);

            if (m_statsChangedCallback)
                m_statsChangedCallback(entityId);
        }

        return removed;
    }

    // -------------------------------------------------------------------------
    // RemoveEffect
    // -------------------------------------------------------------------------

    bool StatusEffectManager::RemoveEffect(uint64_t entityId, std::string_view effectId)
    {
        if (!m_initialized)
            return false;

        auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return false;

        auto& effects = it->second;
        for (auto efIt = effects.begin(); efIt != effects.end(); ++efIt)
        {
            if (efIt->effectId == effectId)
            {
                effects.erase(efIt);

                LOG_DEBUG(Gameplay,
                    "[StatusEffectManager] RemoveEffect: id={} from entity={}",
                    effectId, entityId);

                if (m_statsChangedCallback)
                    m_statsChangedCallback(entityId);

                return true;
            }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // RemoveAllEffects
    // -------------------------------------------------------------------------

    void StatusEffectManager::RemoveAllEffects(uint64_t entityId)
    {
        if (!m_initialized)
            return;

        auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return;

        const size_t count = it->second.size();
        m_effectsByEntity.erase(it);

        LOG_INFO(Gameplay,
            "[StatusEffectManager] RemoveAllEffects: entity={} removed={}",
            entityId, count);

        if (count > 0 && m_statsChangedCallback)
            m_statsChangedCallback(entityId);
    }

    // -------------------------------------------------------------------------
    // QueryCCFlags
    // -------------------------------------------------------------------------

    StatusEffectManager::CCFlags StatusEffectManager::QueryCCFlags(uint64_t entityId) const
    {
        CCFlags flags{};

        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return flags;

        for (const auto& fx : it->second)
        {
            if (fx.type == StatusEffectType::Stun)
                flags.isStunned = true;
            else if (fx.type == StatusEffectType::Root)
                flags.isRooted = true;
            else if (fx.type == StatusEffectType::Slow)
                flags.isSlowed = true;
        }

        return flags;
    }

    // -------------------------------------------------------------------------
    // GetStatModifiers
    // -------------------------------------------------------------------------

    std::vector<StatusEffectManager::StatModifier>
    StatusEffectManager::GetStatModifiers(uint64_t entityId) const
    {
        std::vector<StatModifier> result;

        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return result;

        // Accumulate per-stat totals.
        std::unordered_map<std::string, float> totals;
        for (const auto& fx : it->second)
        {
            if (fx.statModifier != 0.0f && !fx.statName.empty())
            {
                totals[fx.statName] += fx.statModifier * static_cast<float>(fx.stacks);
            }
        }

        result.reserve(totals.size());
        for (auto& [name, total] : totals)
        {
            result.push_back({ name, total });
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // GetEffects / GetEffectCount
    // -------------------------------------------------------------------------

    const std::list<StatusEffect>* StatusEffectManager::GetEffects(uint64_t entityId) const
    {
        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return nullptr;
        return &it->second;
    }

    uint32_t StatusEffectManager::GetEffectCount(uint64_t entityId) const
    {
        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return 0;
        return static_cast<uint32_t>(it->second.size());
    }

    // -------------------------------------------------------------------------
    // HasEffect
    // -------------------------------------------------------------------------

    bool StatusEffectManager::HasEffect(uint64_t entityId, std::string_view effectId) const
    {
        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return false;

        for (const auto& fx : it->second)
        {
            if (fx.effectId == effectId)
                return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Callbacks
    // -------------------------------------------------------------------------

    void StatusEffectManager::SetTickCallback(TickCallback cb)
    {
        m_tickCallback = std::move(cb);
    }

    void StatusEffectManager::SetStatsChangedCallback(StatsChangedCallback cb)
    {
        m_statsChangedCallback = std::move(cb);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    StatusEffect* StatusEffectManager::FindEffect(uint64_t entityId, std::string_view effectId)
    {
        const auto it = m_effectsByEntity.find(entityId);
        if (it == m_effectsByEntity.end())
            return nullptr;

        for (auto& fx : it->second)
        {
            if (fx.effectId == effectId)
                return &fx;
        }
        return nullptr;
    }

} // namespace engine::gameplay
