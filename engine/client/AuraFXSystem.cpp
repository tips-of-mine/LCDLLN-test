#include "engine/client/AuraFXSystem.h"

#include "engine/core/Log.h"

#include <algorithm>

namespace engine::client
{
    AuraFXSystem::~AuraFXSystem()
    {
        if (m_initialized)
            Shutdown();
    }

    bool AuraFXSystem::Init()
    {
        if (m_initialized)
        {
            LOG_WARN(Gameplay, "[AuraFXSystem] Init ignored: already initialized");
            return true;
        }

        m_auras.clear();
        m_indexByEntity.clear();
        m_initialized = true;

        LOG_INFO(Gameplay, "[AuraFXSystem] Init OK");
        return true;
    }

    void AuraFXSystem::Shutdown()
    {
        if (!m_initialized)
            return;

        const size_t count = m_auras.size();
        m_auras.clear();
        m_indexByEntity.clear();
        m_initialized = false;

        LOG_INFO(Gameplay, "[AuraFXSystem] Destroyed (auras={})", count);
    }

    // -------------------------------------------------------------------------
    // Sync – reconcile auras against current effect list
    // -------------------------------------------------------------------------

    void AuraFXSystem::Sync(uint64_t entityId,
                             const std::list<engine::gameplay::StatusEffect>* effects,
                             float posX, float posY, float posZ)
    {
        if (!m_initialized)
            return;

        // Build a set of effectIds currently active on this entity.
        std::vector<std::string> activeIds;
        if (effects)
        {
            for (const auto& fx : *effects)
                activeIds.push_back(fx.effectId);
        }

        // --- Remove auras whose effect is no longer active ---
        auto& indices = m_indexByEntity[entityId];
        for (size_t idx : indices)
        {
            if (idx >= m_auras.size())
                continue;

            ActiveAura& aura = m_auras[idx];
            if (!aura.alive)
                continue;

            const bool stillActive = std::any_of(
                activeIds.begin(), activeIds.end(),
                [&](const std::string& id) { return id == aura.effectId; });

            if (!stillActive)
            {
                LOG_DEBUG(Gameplay,
                    "[AuraFXSystem] Removing aura: effect={} entity={}",
                    aura.effectId, entityId);
                aura.alive = false;
            }
        }

        // --- Add auras for newly active effects ---
        if (effects)
        {
            for (const auto& fx : *effects)
            {
                // Skip if an aura for this effect already exists.
                const bool exists = std::any_of(
                    indices.begin(), indices.end(),
                    [&](size_t idx) {
                        return idx < m_auras.size()
                            && m_auras[idx].alive
                            && m_auras[idx].effectId == fx.effectId;
                    });

                if (exists)
                    continue;

                std::string particleId;
                AuraColor   color{};
                ResolveAuraVisuals(fx.type, fx.effectId, particleId, color);

                ActiveAura aura{};
                aura.effectId  = fx.effectId;
                aura.entityId  = entityId;
                aura.particleId= particleId;
                aura.glowColor = color;
                aura.positionX = posX;
                aura.positionY = posY;
                aura.positionZ = posZ;
                aura.alive     = true;

                const size_t newIdx = m_auras.size();
                m_auras.push_back(std::move(aura));
                indices.push_back(newIdx);

                LOG_DEBUG(Gameplay,
                    "[AuraFXSystem] Spawned aura: effect={} entity={} particle={}",
                    fx.effectId, entityId, particleId);
            }
        }

        // Update positions for live auras belonging to this entity.
        for (size_t idx : indices)
        {
            if (idx < m_auras.size() && m_auras[idx].alive)
            {
                m_auras[idx].positionX = posX;
                m_auras[idx].positionY = posY;
                m_auras[idx].positionZ = posZ;
            }
        }

        // Compact dead entries periodically: rebuild when >25% are dead.
        const size_t deadCount = std::count_if(
            m_auras.begin(), m_auras.end(),
            [](const ActiveAura& a) { return !a.alive; });

        if (deadCount > 0 && deadCount * 4 >= m_auras.size())
        {
            // Rebuild m_auras and m_indexByEntity without dead entries.
            std::vector<ActiveAura> live;
            live.reserve(m_auras.size() - deadCount);

            std::unordered_map<uint64_t, std::vector<size_t>> newIndex;
            for (auto& aura : m_auras)
            {
                if (!aura.alive)
                    continue;
                const size_t idx = live.size();
                newIndex[aura.entityId].push_back(idx);
                live.push_back(std::move(aura));
            }

            m_auras        = std::move(live);
            m_indexByEntity = std::move(newIndex);

            LOG_DEBUG(Gameplay, "[AuraFXSystem] Compacted: live={}", m_auras.size());
        }
    }

    // -------------------------------------------------------------------------
    // SetEntityPosition
    // -------------------------------------------------------------------------

    void AuraFXSystem::SetEntityPosition(uint64_t entityId, float posX, float posY, float posZ)
    {
        if (!m_initialized)
            return;

        const auto it = m_indexByEntity.find(entityId);
        if (it == m_indexByEntity.end())
            return;

        for (size_t idx : it->second)
        {
            if (idx < m_auras.size() && m_auras[idx].alive)
            {
                m_auras[idx].positionX = posX;
                m_auras[idx].positionY = posY;
                m_auras[idx].positionZ = posZ;
            }
        }
    }

    // -------------------------------------------------------------------------
    // RemoveEntity
    // -------------------------------------------------------------------------

    void AuraFXSystem::RemoveEntity(uint64_t entityId)
    {
        if (!m_initialized)
            return;

        const auto it = m_indexByEntity.find(entityId);
        if (it == m_indexByEntity.end())
            return;

        uint32_t removed = 0;
        for (size_t idx : it->second)
        {
            if (idx < m_auras.size() && m_auras[idx].alive)
            {
                m_auras[idx].alive = false;
                ++removed;
            }
        }

        m_indexByEntity.erase(it);

        LOG_INFO(Gameplay,
            "[AuraFXSystem] RemoveEntity: entity={} auras removed={}",
            entityId, removed);
    }

    // -------------------------------------------------------------------------
    // Clear
    // -------------------------------------------------------------------------

    void AuraFXSystem::Clear()
    {
        if (!m_initialized)
            return;

        const size_t count = m_auras.size();
        m_auras.clear();
        m_indexByEntity.clear();

        LOG_INFO(Gameplay, "[AuraFXSystem] Cleared {} auras", count);
    }

    // -------------------------------------------------------------------------
    // ResolveAuraVisuals
    // -------------------------------------------------------------------------

    void AuraFXSystem::ResolveAuraVisuals(engine::gameplay::StatusEffectType type,
                                           std::string_view /*effectId*/,
                                           std::string& outParticleId,
                                           AuraColor& outColor)
    {
        switch (type)
        {
        case engine::gameplay::StatusEffectType::HoT:
            // Heal over time → green particles
            outParticleId = "aura_hot";
            outColor = { 0.0f, 1.0f, 0.2f, 0.8f };
            break;

        case engine::gameplay::StatusEffectType::DoT:
            // Damage over time → purple (poison-like)
            outParticleId = "aura_dot";
            outColor = { 0.6f, 0.0f, 1.0f, 0.8f };
            break;

        case engine::gameplay::StatusEffectType::Stun:
            // Stun → yellow glow, no particles
            outParticleId = "";
            outColor = { 1.0f, 0.95f, 0.0f, 0.9f };
            break;

        case engine::gameplay::StatusEffectType::Slow:
            // Slow → pale blue
            outParticleId = "aura_slow";
            outColor = { 0.4f, 0.6f, 1.0f, 0.7f };
            break;

        case engine::gameplay::StatusEffectType::Root:
            // Root → brown/earth tone
            outParticleId = "aura_root";
            outColor = { 0.5f, 0.3f, 0.1f, 0.8f };
            break;

        case engine::gameplay::StatusEffectType::Debuff:
            // Generic debuff → red tint
            outParticleId = "aura_debuff";
            outColor = { 1.0f, 0.1f, 0.1f, 0.6f };
            break;

        case engine::gameplay::StatusEffectType::Buff:
        default:
            // Generic buff → soft white/gold glow
            outParticleId = "aura_buff";
            outColor = { 1.0f, 0.9f, 0.5f, 0.6f };
            break;
        }
    }

} // namespace engine::client
