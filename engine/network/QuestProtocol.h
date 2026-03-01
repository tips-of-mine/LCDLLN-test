#pragma once

/** @file QuestProtocol.h — AcceptQuest and QuestDelta serialization (M15.1). */
#include "engine/network/Protocol.h"
#include <cstdint>
#include <vector>

namespace engine::network {

size_t SerializeAcceptQuest(uint32_t questId, std::vector<uint8_t>& outBuffer);
bool ParseAcceptQuest(const uint8_t* data, size_t size, uint32_t& outQuestId);
size_t SerializeQuestDelta(uint32_t questId, uint32_t stepIndex, uint32_t counter, bool completed, std::vector<uint8_t>& outBuffer);
bool ParseQuestDelta(const uint8_t* data, size_t size, uint32_t& outQuestId, uint32_t& outStepIndex, uint32_t& outCounter, bool& outCompleted);

} // namespace engine::network
