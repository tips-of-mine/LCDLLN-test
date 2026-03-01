#pragma once

/** @file EventProtocol.h — EventState serialization (M15.3). */
#include "engine/network/Protocol.h"
#include <cstdint>
#include <string>
#include <vector>

namespace engine::network {

/** @brief Event runtime state for notification: 0=idle, 1=active, 2=completed. */
enum class EventStateEnum : uint8_t {
    Idle = 0,
    Active = 1,
    Completed = 2,
};

/** @brief Serializes EventState (type + eventId 4 + state 1 + phaseIndex 4 + phaseCount 4 + textLen 1 + text). Returns bytes written. */
size_t SerializeEventState(uint32_t eventId, EventStateEnum state, uint32_t phaseIndex, uint32_t phaseCount,
    const char* text, std::vector<uint8_t>& outBuffer);

/** @brief Parses EventState payload (type already consumed). Returns true if size valid. */
bool ParseEventState(const uint8_t* data, size_t size, uint32_t& outEventId, EventStateEnum& outState,
    uint32_t& outPhaseIndex, uint32_t& outPhaseCount, std::string& outText);

} // namespace engine::network
