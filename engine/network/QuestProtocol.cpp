/**
 * @file QuestProtocol.cpp
 * @brief AcceptQuest / QuestDelta serialization (M15.1).
 */

#include "engine/network/QuestProtocol.h"
#include <cstring>

namespace engine::network {

size_t SerializeAcceptQuest(uint32_t questId, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1u + 4u);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::AcceptQuest);
    std::memcpy(p + 1, &questId, 4);
    return 5u;
}

bool ParseAcceptQuest(const uint8_t* data, size_t size, uint32_t& outQuestId) {
    if (size < 4u) return false;
    std::memcpy(&outQuestId, data, 4);
    return true;
}

size_t SerializeQuestDelta(uint32_t questId, uint32_t stepIndex, uint32_t counter, bool completed, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1u + 4u + 4u + 4u + 1u);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::QuestDelta);
    std::memcpy(p + 1, &questId, 4);
    std::memcpy(p + 5, &stepIndex, 4);
    std::memcpy(p + 9, &counter, 4);
    p[13] = completed ? 1u : 0u;
    return 14u;
}

bool ParseQuestDelta(const uint8_t* data, size_t size, uint32_t& outQuestId, uint32_t& outStepIndex, uint32_t& outCounter, bool& outCompleted) {
    if (size < 13u) return false;
    std::memcpy(&outQuestId, data, 4);
    std::memcpy(&outStepIndex, data + 4, 4);
    std::memcpy(&outCounter, data + 8, 4);
    outCompleted = (data[12] != 0);
    return true;
}

} // namespace engine::network
