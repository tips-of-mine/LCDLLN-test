#pragma once

#include "engine/gameplay/StatusEffect.h"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::client
{
    /// RGBA color (0..1 per channel) used for aura glow/tint.
    struct AuraColor
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    /// One active persistent particle aura attached to an entity.
    struct ActiveAura
    {
        /// Effect id that spawned this aura.
        std::string effectId;

        /// Entity the aura is attached to.
        uint64_t entityId = 0;

        /// Particle emitter id passed to the particle system (e.g. "aura_poison").
        std::string particleId;

        /// Model glow/tint color applied to the entity shader.
        AuraColor glowColor{};

        /// World-space position of the entity root (updated each Tick).
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;

        /// True when this aura is still live (false = scheduled for destruction).
        bool alive = true;
    };

    /// Manages persistent visual auras (particle emitters + model glow) for active
    /// status effects.  Call Sync() to reconcile with the current effect list,
    /// and Tick() to advance positions.
    ///
    /// Design: data-only; the render layer reads GetAuras() and drives the actual
    /// GPU particle system and shader tint upload.
    class AuraFXSystem final
    {
    public:
        AuraFXSystem() = default;
        ~AuraFXSystem();

        /// Initialize the system.
        /// \return true on success.
        bool Init();

        /// Release all active auras and emit shutdown log.
        void Shutdown();

        /// Reconcile the active aura list against a fresh StatusEffect snapshot for
        /// one entity.  New effects gain an aura; expired/removed effects lose theirs.
        ///
        /// \param entityId   Target entity.
        /// \param effects    Current effect list (from StatusEffectManager). May be nullptr.
        /// \param posX/Y/Z   Current world-space root position of the entity.
        void Sync(uint64_t entityId,
                  const std::list<engine::gameplay::StatusEffect>* effects,
                  float posX, float posY, float posZ);

        /// Update world-space positions of auras attached to one entity.
        void SetEntityPosition(uint64_t entityId, float posX, float posY, float posZ);

        /// Remove all auras for one entity (call on death or despawn).
        void RemoveEntity(uint64_t entityId);

        /// Remove all auras for all entities.
        void Clear();

        /// Read-only view of all live auras for rendering.
        const std::vector<ActiveAura>& GetAuras() const { return m_auras; }

    private:
        /// Map a StatusEffectType to a particle emitter id and glow color.
        static void ResolveAuraVisuals(engine::gameplay::StatusEffectType type,
                                        std::string_view effectId,
                                        std::string& outParticleId,
                                        AuraColor& outColor);

        bool m_initialized = false;

        /// Flat list of live auras; dead entries (alive=false) are compacted in Sync.
        std::vector<ActiveAura> m_auras;

        /// Index: entityId → indices into m_auras (for fast per-entity lookup).
        std::unordered_map<uint64_t, std::vector<size_t>> m_indexByEntity;
    };

} // namespace engine::client
