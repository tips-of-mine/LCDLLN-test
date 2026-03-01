/**
 * @file EventProtocol.cpp
 * @brief EventState serialization (M15.3).
 */

#include "engine/network/EventProtocol.h"
#include <cstring>

namespace engine::network {

constexpr size_t kMaxEventTextLen = 255u;

size_t SerializeEventState(uint32_t eventId, EventStateEnum state, uint32_t phaseIndex, uint32_t phaseCount,
    const char* text, std::vector<uint8_t>& outBuffer) {
    size_t textLen = 0u;
    if (text) {
        while (text[textLen] != '\0' && textLen < kMaxEventTextLen) ++textLen;
    }
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1u + 4u + 1u + 4u + 4u + 1u + textLen);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::EventState);
    std::memcpy(p + 1, &eventId, 4);
    p[5] = static_cast<uint8_t>(state);
    std::memcpy(p + 6, &phaseIndex, 4);
    std::memcpy(p + 10, &phaseCount, 4);
    p[14] = static_cast<uint8_t>(textLen);
    if (text && textLen > 0u)
        std::memcpy(p + 15, text, textLen);
    return 15u + textLen;
}

bool ParseEventState(const uint8_t* data, size_t size, uint32_t& outEventId, EventStateEnum& outState,
    uint32_t& outPhaseIndex, uint32_t& outPhaseCount, std::string& outText) {
    if (size < 15u) return false;
    std::memcpy(&outEventId, data, 4);
    outState = static_cast<EventStateEnum>(data[4]);
    std::memcpy(&outPhaseIndex, data + 5, 4);
    std::memcpy(&outPhaseCount, data + 9, 4);
    uint8_t textLen = data[14];
    outText.clear();
    if (textLen > 0u && size >= 15u + textLen) {
        outText.assign(reinterpret_cast<const char*>(data + 15), textLen);
    }
    return true;
}

} // namespace engine::network
