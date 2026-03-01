#pragma once

/**
 * @file AiState.h
 * @brief AI state machine states for mobs (M14.2): Idle, Patrol, Aggro, Return.
 */

#include <cstdint>

namespace engine::ai {

/** @brief Idle: at spawn, no threat. Patrol: optional waypoints. Aggro: targeting max-threat. Return: out of leash, moving back. */
enum class MobAiState : uint8_t {
    Idle = 0,
    Patrol = 1,
    Aggro = 2,
    Return = 3,
};

constexpr float kDefaultLeashDistance = 30.f;
constexpr float kDefaultMobMoveSpeed = 4.f;
constexpr uint32_t kAiUpdateTickStep = 2u;

} // namespace engine::ai
